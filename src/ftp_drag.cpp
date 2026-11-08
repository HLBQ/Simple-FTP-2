#include "ftp_drag.h"
#include "ftp_client.h"
#include "ftp_progress.h"
#include "ftp_fsops.h"
#include "winlongpath.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <objidl.h>
#include <mutex>
#include <atomic>
#include <thread>
#include <filesystem>

#pragma comment(lib, "shlwapi.lib")

namespace fs = std::filesystem;

// ============================================================================
//  FTP 虚拟文件拖拽实现
//
//  原理：
//   拖拽时通过 IDataObject 提供两种剪贴板格式：
//     - CFSTR_FILEDESCRIPTORW : 描述每个文件/文件夹（名字、大小、属性）
//     - CFSTR_FILECONTENTS    : 每个文件对应一个 IStream，按需拉取数据
//   资源管理器（explorer）在用户放下文件后，会：
//     1) 读取 FILEDESCRIPTOR 列表，为文件夹创建目录、为文件创建目标文件；
//     2) 对每个文件调用 GetData(CFSTR_FILECONTENTS, index) 拿到 IStream，
//        然后循环 Read() 把数据写入目标位置。
//   我们的 IStream::Read 直接从 FTP 数据连接读取，因此数据是“边拖边传”，
//   不经过本地临时文件，不占用 C 盘空间，支持超大文件与文件夹。
// ============================================================================

namespace {

// ---------------------------------------------------------------------------
//  FtpStream：实现 IStream，Read 时从 FTP 拉取数据
// ---------------------------------------------------------------------------
class FtpStream : public IStream {
public:
    // mtx：保护 client_ 的外部互斥量（与 GUI 共用同一把锁）。
    // 修复：原实现使用本模块私有的全局 g_ftpMutex，与 GUI 持有的
    // g_clientMutex 不是同一把锁，拖出下载与 GUI 传输会互相损坏数据。
    FtpStream(FtpClient* client, const std::string& remotePath, std::mutex* mtx)
        : client_(client), remotePath_(remotePath), mtx_(mtx) {}

    // ---- IUnknown ----
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IStream ||
            riid == IID_ISequentialStream) {
            *ppv = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --ref_;
        if (n == 0) delete this;
        return n;
    }

    // ---- ISequentialStream ----
    HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* pcbRead) override {
        if (pcbRead) *pcbRead = 0;
        if (!pv) return STG_E_INVALIDPOINTER;
        if (cb == 0) return S_OK;

        std::lock_guard<std::mutex> lk(*mtx_);
        if (!opened_) {
            stream_ = client_->OpenDownloadStream(remotePath_);
            if (stream_ == (uintptr_t)-1) return E_FAIL;
            opened_ = true;
        }
        if (finished_) return S_OK;

        ULONG total = 0;
        char* out = (char*)pv;
        while (total < cb) {
            int n = client_->ReadStreamData(stream_, out + total,
                                            (int)(cb - total));
            if (n < 0) return E_FAIL;      // 读取错误
            if (n == 0) {                  // 传输结束
                finished_ = true;
                client_->CloseDownloadStream(stream_);
                stream_ = (uintptr_t)-1;
                break;
            }
            total += (ULONG)n;
        }
        if (pcbRead) *pcbRead = total;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override {
        return STG_E_ACCESSDENIED;
    }

    // ---- IStream ----
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER, DWORD, ULARGE_INTEGER*) override {
        // FTP 下载流不支持随机定位
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override {
        return STG_E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*,
                                     ULARGE_INTEGER*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* pstatstg, DWORD) override {
        if (!pstatstg) return E_POINTER;
        ZeroMemory(pstatstg, sizeof(*pstatstg));
        pstatstg->type = STGTY_STREAM;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }

private:
    ~FtpStream() {
        if (opened_ && stream_ != (uintptr_t)-1) {
            std::lock_guard<std::mutex> lk(*mtx_);
            client_->CloseDownloadStream(stream_);
        }
    }

    std::atomic<ULONG> ref_{1};
    FtpClient* client_;
    std::string remotePath_;
    std::mutex* mtx_ = nullptr;
    uintptr_t stream_ = (uintptr_t)-1;
    bool opened_ = false;
    bool finished_ = false;
};

// ---------------------------------------------------------------------------
//  FtpEnumFormatEtc：枚举支持的剪贴板格式
// ---------------------------------------------------------------------------
class FtpEnumFormatEtc : public IEnumFORMATETC {
public:
    FtpEnumFormatEtc(const FORMATETC* fmts, ULONG count)
        : fmts_(fmts), count_(count), pos_(0) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
            *ppv = static_cast<IEnumFORMATETC*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --ref_;
        if (n == 0) delete this;
        return n;
    }

    HRESULT STDMETHODCALLTYPE Next(ULONG celt, FORMATETC* rgelt,
                                   ULONG* pceltFetched) override {
        ULONG fetched = 0;
        while (fetched < celt && pos_ < count_) {
            rgelt[fetched] = fmts_[pos_];
            ++fetched;
            ++pos_;
        }
        if (pceltFetched) *pceltFetched = fetched;
        return (fetched == celt) ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG celt) override {
        pos_ += celt;
        return (pos_ <= count_) ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override { pos_ = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** ppenum) override {
        if (!ppenum) return E_POINTER;
        *ppenum = new FtpEnumFormatEtc(fmts_, count_);
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_{1};
    const FORMATETC* fmts_;
    ULONG count_;
    ULONG pos_;
};

// ---------------------------------------------------------------------------
//  FtpDataObject：实现 IDataObject，提供虚拟文件
// ---------------------------------------------------------------------------
class FtpDataObject : public IDataObject {
public:
    FtpDataObject(FtpClient* client, const std::vector<FtpDragItem>& items,
                  std::mutex* mtx)
        : client_(client), items_(items), mtx_(mtx) {
        // 支持的两种格式
        fmt_[0].cfFormat = (CLIPFORMAT)RegisterClipboardFormatW(L"FileGroupDescriptorW");
        fmt_[0].ptd = nullptr;
        fmt_[0].dwAspect = DVASPECT_CONTENT;
        fmt_[0].lindex = -1;
        fmt_[0].tymed = TYMED_HGLOBAL;

        fmt_[1].cfFormat = (CLIPFORMAT)RegisterClipboardFormatW(L"FileContents");
        fmt_[1].ptd = nullptr;
        fmt_[1].dwAspect = DVASPECT_CONTENT;
        fmt_[1].lindex = -1;   // 具体索引在 GetData 时由调用方指定
        fmt_[1].tymed = TYMED_ISTREAM;
    }

    // ---- IUnknown ----
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --ref_;
        if (n == 0) delete this;
        return n;
    }

    // ---- IDataObject ----
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pformatetc,
                                      STGMEDIUM* pmedium) override {
        if (!pformatetc || !pmedium) return E_POINTER;
        ZeroMemory(pmedium, sizeof(*pmedium));

        CLIPFORMAT cfDesc = (CLIPFORMAT)RegisterClipboardFormatW(L"FileGroupDescriptorW");
        CLIPFORMAT cfCont = (CLIPFORMAT)RegisterClipboardFormatW(L"FileContents");

        if (pformatetc->cfFormat == cfDesc) {
            // 返回 FILEGROUPDESCRIPTORW（文件描述列表）
            return GetFileDescriptor(pmedium);
        }
        if (pformatetc->cfFormat == cfCont) {
            // 返回指定索引文件的 IStream
            LONG idx = pformatetc->lindex;
            if (idx < 0 || idx >= (LONG)items_.size()) return DV_E_LINDEX;
            if (items_[idx].isDir) return DV_E_LINDEX;   // 文件夹无内容流
            FtpStream* st = new FtpStream(client_, items_[idx].remotePath, mtx_);
            pmedium->tymed = TYMED_ISTREAM;
            pmedium->pstm = st;
            pmedium->pUnkForRelease = nullptr;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pformatetc) override {
        if (!pformatetc) return E_POINTER;
        CLIPFORMAT cfDesc = (CLIPFORMAT)RegisterClipboardFormatW(L"FileGroupDescriptorW");
        CLIPFORMAT cfCont = (CLIPFORMAT)RegisterClipboardFormatW(L"FileContents");
        if (pformatetc->cfFormat == cfDesc) return S_OK;
        if (pformatetc->cfFormat == cfCont) {
            if (pformatetc->lindex < 0 ||
                pformatetc->lindex >= (LONG)items_.size()) return DV_E_LINDEX;
            if (items_[pformatetc->lindex].isDir) return DV_E_LINDEX;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* pOut) override {
        if (pOut) pOut->ptd = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dwDirection,
                                            IEnumFORMATETC** ppenum) override {
        if (!ppenum) return E_POINTER;
        if (dwDirection != DATADIR_GET) return E_NOTIMPL;
        *ppenum = new FtpEnumFormatEtc(fmt_, 2);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    ~FtpDataObject() {}

    // 构造 FILEGROUPDESCRIPTORW 并放入 HGLOBAL
    HRESULT GetFileDescriptor(STGMEDIUM* pmedium) {
        UINT count = (UINT)items_.size();
        SIZE_T size = sizeof(FILEGROUPDESCRIPTORW) +
                      (count > 0 ? (count - 1) : 0) * sizeof(FILEDESCRIPTORW);
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
        if (!hg) return E_OUTOFMEMORY;
        FILEGROUPDESCRIPTORW* fgd = (FILEGROUPDESCRIPTORW*)GlobalLock(hg);
        if (!fgd) { GlobalFree(hg); return E_OUTOFMEMORY; }
        fgd->cItems = count;
        for (UINT i = 0; i < count; ++i) {
            FILEDESCRIPTORW& fd = fgd->fgd[i];
            fd.dwFlags = FD_ATTRIBUTES | FD_FILESIZE | FD_PROGRESSUI;
            fd.dwFileAttributes = items_[i].isDir
                ? FILE_ATTRIBUTE_DIRECTORY
                : FILE_ATTRIBUTE_NORMAL;
            fd.nFileSizeHigh = (DWORD)(items_[i].size >> 32);
            fd.nFileSizeLow  = (DWORD)(items_[i].size & 0xFFFFFFFF);
            // 文件名（仅文件名，不含路径）
            wcsncpy_s(fd.cFileName, MAX_PATH, items_[i].name.c_str(), _TRUNCATE);
        }
        GlobalUnlock(hg);
        pmedium->tymed = TYMED_HGLOBAL;
        pmedium->hGlobal = hg;
        pmedium->pUnkForRelease = nullptr;
        return S_OK;
    }

    std::atomic<ULONG> ref_{1};
    FtpClient* client_;
    std::vector<FtpDragItem> items_;
    std::mutex* mtx_ = nullptr;
    FORMATETC fmt_[2];
};

// ---------------------------------------------------------------------------
//  FtpDropSource：实现 IDropSource
// ---------------------------------------------------------------------------
class FtpDropSource : public IDropSource {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --ref_;
        if (n == 0) delete this;
        return n;
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscapePressed,
                                                DWORD grfKeyState) override {
        if (fEscapePressed) return DRAGDROP_S_CANCEL;
        if (!(grfKeyState & (MK_LBUTTON | MK_RBUTTON)))
            return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    std::atomic<ULONG> ref_{1};
};

} // namespace

void StartFtpDrag(HWND hwnd, FtpClient* client,
                  const std::vector<FtpDragItem>& items, std::mutex* mtx) {
    if (!client || items.empty() || !mtx) return;

    FtpDataObject* dataObj = new FtpDataObject(client, items, mtx);
    FtpDropSource* dropSrc = new FtpDropSource();

    DWORD effect = DROPEFFECT_COPY;
    DoDragDrop(dataObj, dropSrc, DROPEFFECT_COPY, &effect);

    dataObj->Release();
    dropSrc->Release();
}

// ============================================================================
//  拖入（本地文件 -> FTP 上传）实现
//
//  原理：
//    在客户端文件列表窗口上注册 IDropTarget。当用户从资源管理器把本地
//    文件/文件夹拖到列表上时，Drop() 会收到 CF_HDROP 格式的数据（本地路径
//    列表）。我们遍历这些路径，在后台线程中递归上传到 FTP 当前目录。
// ============================================================================

namespace {

// ---------------------------------------------------------------------------
//  FtpDropTarget：实现 IDropTarget，接收本地文件拖入并上传
// ---------------------------------------------------------------------------
class FtpDropTarget : public IDropTarget {
public:
    FtpDropTarget(HWND hwnd, HWND hitHwnd, FtpClient* client, std::mutex* mtx)
        : hwnd_(hwnd), hitHwnd_(hitHwnd), client_(client), mtx_(mtx) {}

    // ---- IUnknown ----
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --ref_;
        if (n == 0) delete this;
        return n;
    }

    // ---- IDropTarget ----
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pDataObj, DWORD,
                                        POINTL pt, DWORD* pdwEffect) override {
        // 仅当数据包含 CF_HDROP（本地文件）、客户端已连接、
        // 且鼠标位于目标控件（文件列表）范围内时接受
        bool ok = HasHdrop(pDataObj) && client_ && client_->IsConnected() &&
                  HitTest(pt);
        if (pdwEffect) *pdwEffect = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL pt, DWORD* pdwEffect) override {
        bool ok = client_ && client_->IsConnected() && HitTest(pt);
        if (pdwEffect) *pdwEffect = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override { return S_OK; }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* pDataObj, DWORD,
                                   POINTL pt, DWORD* pdwEffect) override {
        if (pdwEffect) *pdwEffect = DROPEFFECT_NONE;
        if (!pDataObj || !client_ || !client_->IsConnected()) return S_OK;
        if (!HitTest(pt)) return S_OK;   // 不在目标控件范围内，忽略

        // 已有传输进行中时拒绝新拖放（进度弹窗与客户端都是单实例，
        // 并发会互相损坏传输状态）
        if (ProgressDialog::IsBusy()) {
            MessageBoxW(hwnd_,
                        L"已有传输正在进行，请等待完成后再拖入文件。",
                        L"FtpTool", MB_OK | MB_ICONINFORMATION);
            return S_OK;
        }

        // 取出 CF_HDROP 中的本地路径列表
        std::vector<std::wstring> paths = GetHdropPaths(pDataObj);
        if (paths.empty()) return S_OK;

        if (pdwEffect) *pdwEffect = DROPEFFECT_COPY;

        // 预计算待上传总字节数（含子目录递归），用于进度百分比
        uint64_t totalBytes = 0;
        {
            std::error_code ec;
            for (const auto& wp : paths) {
                fs::path p(ToLongPathW(wp));
                if (fs::is_regular_file(p, ec)) {
                    totalBytes += fs::file_size(p, ec);
                } else if (fs::is_directory(p, ec)) {
                    for (fs::recursive_directory_iterator rit(p, ec), rend;
                         rit != rend; rit.increment(ec)) {
                        if (ec) break;
                        std::error_code ec2;
                        if (rit->is_regular_file(ec2))
                            totalBytes += rit->file_size(ec2);
                    }
                }
            }
        }

        // 在 UI 线程同步创建进度弹窗（拖放回调运行在 UI 线程），
        // 显示第一个待上传项的名字；工作线程负责更新进度与结束。
        std::wstring firstName = fs::path(paths[0]).filename().wstring();
        ProgressDialog::Begin(hwnd_, L"上传", firstName, totalBytes);

        // 在后台线程执行上传，避免阻塞 UI（拖放回调运行在 UI 线程）
        FtpClient* client = client_;
        std::mutex* mtx = mtx_;
        HWND hwnd = hwnd_;
        std::vector<fs::path> localPaths;
        for (const auto& wp : paths) localPaths.push_back(fs::path(wp));
        std::thread([client, mtx, hwnd, localPaths, totalBytes]() {
            std::string err;
            bool ok = false;
            {
                std::lock_guard<std::mutex> lk(*mtx);
                // 复用统一的递归上传（与右键菜单同一实现）
                ok = FtpOps::Upload(*client, localPaths, totalBytes,
                    [](const FtpOps::Progress& p) {
                        ProgressDialog::Update(p.done, p.total, p.verifying,
                                               p.verifyDone, p.verifyTotal);
                        if (!p.current.empty())
                            ProgressDialog::SetFileName(p.current);
                    },
                    [] { return ProgressDialog::IsCancelled(); });
                if (!ok) err = client->LastError();
            }
            bool cancelled = !ok && ProgressDialog::IsCancelled();
            ProgressDialog::End();
            // 通知 UI 线程重新拉取目录并刷新列表（WM_APP + 6 = WM_APP_CLI_RELIST）
            if (hwnd) PostMessage(hwnd, WM_APP + 6, 0, 0);
            // 拖入上传失败：以前完全静默。这里复用 GUI 的
            // WM_APP_CLI_STATUS（WM_APP + 5, wParam=0 表示失败），
            // 由 UI 线程统一记日志并弹出错误框（权限不足/磁盘满/550 等）。
            if (!ok && !cancelled) {
                std::string text = "拖入上传失败";
                if (!err.empty()) text += "：" + err;
                else text += "：未知错误";
                std::string* msg = new std::string(text);
                if (hwnd) PostMessage(hwnd, WM_APP + 5, 0, (LPARAM)msg);
                else delete msg;
            }
        }).detach();
        return S_OK;
    }

private:
    ~FtpDropTarget() {}

    // 判断拖放点（屏幕坐标）是否落在目标控件（文件列表）范围内
    bool HitTest(POINTL pt) const {
        if (!hitHwnd_) return true;   // 未指定目标控件则始终接受
        POINT p{ pt.x, pt.y };
        RECT rc;
        if (!GetWindowRect(hitHwnd_, &rc)) return false;
        return PtInRect(&rc, p) != FALSE;
    }

    // 判断数据对象是否包含 CF_HDROP
    static bool HasHdrop(IDataObject* pDataObj) {
        FORMATETC fe{};
        fe.cfFormat = CF_HDROP;
        fe.ptd = nullptr;
        fe.dwAspect = DVASPECT_CONTENT;
        fe.lindex = -1;
        fe.tymed = TYMED_HGLOBAL;
        return pDataObj->QueryGetData(&fe) == S_OK;
    }

    // 从数据对象取出 CF_HDROP 中的本地路径列表
    static std::vector<std::wstring> GetHdropPaths(IDataObject* pDataObj) {
        std::vector<std::wstring> result;
        FORMATETC fe{};
        fe.cfFormat = CF_HDROP;
        fe.ptd = nullptr;
        fe.dwAspect = DVASPECT_CONTENT;
        fe.lindex = -1;
        fe.tymed = TYMED_HGLOBAL;
        STGMEDIUM stg{};
        if (pDataObj->GetData(&fe, &stg) != S_OK) return result;
        HDROP hDrop = (HDROP)GlobalLock(stg.hGlobal);
        if (hDrop) {
            UINT n = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; ++i) {
                UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
                std::wstring path(len + 1, L'\0');
                DragQueryFileW(hDrop, i, &path[0], len + 1);
                path.resize(len);
                if (!path.empty()) result.push_back(path);
            }
            GlobalUnlock(stg.hGlobal);
        }
        ReleaseStgMedium(&stg);
        return result;
    }

    std::atomic<ULONG> ref_{1};
    HWND hwnd_;       // 用于 PostMessage 通知刷新的窗口
    HWND hitHwnd_;    // 命中判定控件（文件列表）；为空则始终接受
    FtpClient* client_;
    std::mutex* mtx_;
};

} // namespace

void RegisterFtpDropTarget(HWND hwnd, HWND hitHwnd, FtpClient* client,
                           std::mutex* mtx) {
    if (!hwnd || !client || !mtx) return;
    FtpDropTarget* target = new FtpDropTarget(hwnd, hitHwnd, client, mtx);
    if (FAILED(RegisterDragDrop(hwnd, target))) {
        target->Release();
    }
    // 注意：RegisterDragDrop 会持有引用，无需在此 Release
}

void RevokeFtpDropTarget(HWND hwnd) {
    if (hwnd) RevokeDragDrop(hwnd);
}
