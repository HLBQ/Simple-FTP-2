#include "ftp_fsops.h"

#include "winlongpath.h"

#include <windows.h>
#include <string>
#include <vector>
#include <system_error>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// 内部辅助
// ---------------------------------------------------------------------------
namespace {

// UTF-8 -> UTF-16
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

// ---- 远程绝对路径辅助（全部以 '/' 开头，服务器始终相对共享根解析）----
std::string JoinRemote(const std::string& base, const std::string& name) {
    std::string r = base;
    if (r.empty()) r = "/";
    if (r.back() != '/') r.push_back('/');
    r += name;
    return r;
}
std::string ParentRemote(const std::string& abs) {
    size_t p = abs.find_last_of('/');
    if (p == std::string::npos || p == 0) return "/";
    return abs.substr(0, p);
}
std::string BaseNameRemote(const std::string& abs) {
    size_t p = abs.find_last_of('/');
    if (p == std::string::npos) return abs;
    return abs.substr(p + 1);
}

// 操作结束后把连接恢复到入口目录（统一用绝对路径 CWD）
struct DirGuard {
    FtpClient* client;
    std::string start;
    explicit DirGuard(FtpClient& c) : client(&c), start(c.CurrentDir()) {}
    ~DirGuard() {
        if (client->CurrentDir() != start) client->ChangeDir(start);
    }
};

bool Cancelled(const FtpOps::CancelFn& f) { return f && f(); }

// 触发一次进度回调
void Emit(const FtpOps::ProgressCb& cb, const std::wstring& current,
          uint64_t done, uint64_t total) {
    if (!cb) return;
    FtpOps::Progress p;
    p.current = current;
    p.done = done;
    p.total = total;
    cb(p);
}

// 远程目录的栈帧（后序遍历用）：记录已列出的内容与处理位置
struct DirFrame {
    std::string remoteAbs;                          // 目录自身绝对路径
    std::vector<FtpClient::Entry> entries;
    size_t idx = 0;
    bool listed = false;
    bool isVirtual = false;                         // 顶层虚拟帧：只处理，不删除自身
};

} // namespace

// ===========================================================================
//  上传（迭代）
// ===========================================================================

uint64_t FtpOps::MeasureLocal(const std::vector<fs::path>& paths) {
    // std::filesystem 的递归迭代器本身就是迭代（栈在库的堆上），无栈深问题
    uint64_t total = 0;
    std::error_code ec;
    for (const auto& p : paths) {
        // 统一转 \\?\ 长路径：深层目录/超长路径也能枚举
        fs::path lp = fs::path(ToLongPathW(p.wstring()));
        if (fs::is_regular_file(lp, ec)) {
            total += fs::file_size(lp, ec);
        } else if (fs::is_directory(lp, ec)) {
            for (fs::recursive_directory_iterator it(lp, ec), end; it != end; it.increment(ec)) {
                if (ec) break;
                std::error_code ec2;
                if (it->is_regular_file(ec2))
                    total += it->file_size(ec2);
            }
        }
    }
    return total;
}

namespace {

// 上传工作单元：remoteDir = 所在远程目录（绝对路径），name = 条目名
struct UploadWork {
    fs::path local;
    std::string remoteDir;
    std::string name;
    bool isDir = false;
    fs::path origPath;   // 原始路径（无 \\?\ 前缀），用于计算显示用相对路径
};

} // namespace

bool FtpOps::Upload(FtpClient& client, const std::vector<fs::path>& paths,
                    uint64_t totalBytes, const ProgressCb& onProgress,
                    const CancelFn& isCancelled) {
    DirGuard guard(client);
    const std::string base = client.CurrentDir().empty() ? "/" : client.CurrentDir();

    bool anyFail = false;
    uint64_t done = 0;

    // 显式工作栈（堆）：倒序压入以保持顶层处理顺序
    // 计算所有选中路径的公共基目录，用于显示相对路径
    fs::path baseLocal;
    if (!paths.empty()) {
        baseLocal = paths[0].parent_path();
        for (const auto& p : paths) {
            fs::path pp = p.parent_path();
            // 逐段比较取公共前缀
            auto a = baseLocal.begin(), b = pp.begin();
            fs::path common;
            while (a != baseLocal.end() && b != pp.end() && *a == *b) {
                common /= *a; ++a; ++b;
            }
            baseLocal = common;
        }
        if (baseLocal.empty()) baseLocal = paths[0].parent_path();
    }

    std::vector<UploadWork> stack;
    for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
        std::error_code ec;
        UploadWork w;
        w.local = fs::path(ToLongPathW(it->wstring()));
        w.origPath = *it;
        w.name = it->filename().u8string();
        w.remoteDir = base;
        w.isDir = fs::is_directory(w.local, ec);
        stack.push_back(std::move(w));
    }

    while (!stack.empty()) {
        if (Cancelled(isCancelled)) break;
        UploadWork w = std::move(stack.back());
        stack.pop_back();

        std::error_code ec;
        if (w.isDir) {
            // 创建并进入远程目录（均用绝对路径）
            std::string childAbs = JoinRemote(w.remoteDir, w.name);
            client.MakeDir(childAbs);   // 已存在时失败，忽略；以能否进入为准
            if (!client.ChangeDir(childAbs)) { anyFail = true; continue; }

            // 收集本地子项并倒序压栈（remoteDir 即本目录绝对路径）
            std::vector<UploadWork> children;
            fs::directory_iterator it(w.local, ec), end;
            if (ec) { anyFail = true; continue; }
            for (; it != end; it.increment(ec)) {
                if (ec) { anyFail = true; break; }
                UploadWork c;
                c.local = it->path();
                c.origPath = w.origPath / it->path().filename();
                c.name = it->path().filename().u8string();
                c.remoteDir = childAbs;
                c.isDir = fs::is_directory(it->path(), ec);
                children.push_back(std::move(c));
            }
            for (auto jt = children.rbegin(); jt != children.rend(); ++jt)
                stack.push_back(std::move(*jt));
            // 无需 CDUP：后续单元一律用绝对路径 CWD
        } else {
            uint64_t fileBytes = 0;
            {
                std::error_code ec2;
                fileBytes = fs::file_size(w.local, ec2);
                if (ec2) fileBytes = 0;
            }
            // 显示相对路径（如 subfolder\file），保留反斜杠使 SS_PATHELLIPSIS 生效
            std::wstring wname = w.origPath.lexically_relative(baseLocal).wstring();
            if (wname.empty() || wname == L".") wname = Utf8ToWide(w.name);
            uint64_t baseDone = done;
            // 记录本文件最后一次上报的整体进度。校验阶段沿用它，
            // 保证进度条停在“当前已完成”的位置，绝不回退。
            uint64_t lastDone = baseDone;
            Emit(onProgress, wname, baseDone, totalBytes);
            // CWD 到所在目录后用纯文件名 STOR
            bool inDir = client.ChangeDir(w.remoteDir);
            bool ok = inDir && client.Upload(w.local.u8string(), w.name, fileBytes,
                [&](uint64_t d, uint64_t) {
                    lastDone = baseDone + d;
                    Emit(onProgress, wname, lastDone, totalBytes);
                },
                [&](uint64_t vd, uint64_t vt) {
                    // 哈希校验阶段：整体进度保持在上一次上报的位置，
                    // 校验进度单独显示，避免大文件校验时进度条突然倒退。
                    if (!onProgress) return;
                    FtpOps::Progress p;
                    p.current = wname;
                    p.done = lastDone;
                    p.total = totalBytes;
                    p.verifying = true;
                    p.verifyDone = vd;
                    p.verifyTotal = vt ? vt : fileBytes;
                    onProgress(p);
                });
            // 完成量取“本地大小”和“实际最后一次上报值”的较大者：
            // 文件大小统计失败或服务器 LIST 大小为 0 时，进度条不会在
            // 文件传完后突然回退到文件开头。
            uint64_t completed = baseDone + fileBytes;
            if (lastDone > completed) completed = lastDone;
            done = ok ? completed : baseDone;
            Emit(onProgress, wname, done, totalBytes);
            if (!ok) anyFail = true;
        }
    }
    return !anyFail;
}

// ===========================================================================
//  下载（迭代）
// ===========================================================================

namespace {

// 下载工作单元：remoteDir = 所在远程目录绝对路径；item = 待下载条目
struct DownloadWork {
    FtpOps::RemoteItem item;
    std::string remoteDir;
    fs::path localDir;
};

} // namespace

bool FtpOps::Download(FtpClient& client, const std::vector<RemoteItem>& items,
                      const fs::path& localDir, uint64_t totalBytes,
                      const ProgressCb& onProgress, const CancelFn& isCancelled) {
    DirGuard guard(client);
    const std::string base = client.CurrentDir().empty() ? "/" : client.CurrentDir();

    bool anyFail = false;
    uint64_t done = 0;

    std::vector<DownloadWork> stack;
    for (auto it = items.rbegin(); it != items.rend(); ++it)
        stack.push_back({*it, base, localDir});

    while (!stack.empty()) {
        if (Cancelled(isCancelled)) break;
        DownloadWork w = std::move(stack.back());
        stack.pop_back();

        if (!w.item.isDir) {
            std::error_code ec;
            fs::create_directories(fs::path(ToLongPathW(w.localDir.wstring())), ec);
            // 显示相对路径（如 subfolder\file），便于区分嵌套目录中的同名文件
            // 保留反斜杠分隔符，使进度窗的 SS_PATHELLIPSIS 能正确做路径中间省略
            fs::path fullLocal = w.localDir / fs::path(Utf8ToWide(w.item.name));
            std::wstring wname = fullLocal.lexically_relative(localDir).wstring();
            if (wname.empty() || wname == L".") wname = Utf8ToWide(w.item.name);
            uint64_t baseDone = done;
            // 记录本文件最后一次上报的整体进度；校验阶段沿用它，防止回退
            // （下载列表里大小为 0 时也不能用 w.item.size，否则会跳回 baseDone）
            uint64_t lastDone = baseDone;
            Emit(onProgress, wname, baseDone, totalBytes);
            fs::path localFile = w.localDir / fs::path(Utf8ToWide(w.item.name));
            bool inDir = client.ChangeDir(w.remoteDir);
            bool ok = inDir && client.Download(w.item.name, localFile.u8string(),
                                               w.item.size,
                [&](uint64_t d, uint64_t) {
                    lastDone = baseDone + d;
                    Emit(onProgress, wname, lastDone, totalBytes);
                },
                [&](uint64_t vd, uint64_t vt) {
                    // 哈希校验阶段：整体进度保持在上一次上报的位置，
                    // 校验进度单独显示，避免整体进度条回退。
                    if (!onProgress) return;
                    FtpOps::Progress p;
                    p.current = wname;
                    p.done = lastDone;
                    p.total = totalBytes;
                    p.verifying = true;
                    p.verifyDone = vd;
                    p.verifyTotal = vt ? vt : w.item.size;
                    onProgress(p);
                });
            // 完成量取“列表大小”和“实际最后一次上报值”的较大者：
            // 服务器 LIST 不提供大小时也不会在传完后把进度回退到文件开头。
            uint64_t completed = baseDone + w.item.size;
            if (lastDone > completed) completed = lastDone;
            done = ok ? completed : baseDone;
            Emit(onProgress, wname, done, totalBytes);
            if (!ok) anyFail = true;
        } else {
            // 目录：进入并列出内容，建本地目录，子项压栈
            std::string childAbs = JoinRemote(w.remoteDir, w.item.name);
            if (!client.ChangeDir(childAbs)) { anyFail = true; continue; }
            std::vector<FtpClient::Entry> ents;
            if (!client.List(ents)) { anyFail = true; continue; }

            fs::path sub = w.localDir / fs::path(Utf8ToWide(w.item.name));
            std::error_code ec;
            fs::create_directories(fs::path(ToLongPathW(sub.wstring())), ec);

            std::vector<DownloadWork> children;
            for (const auto& e : ents) {
                DownloadWork c;
                c.item = {e.name, e.isDir, e.size};
                c.remoteDir = childAbs;
                c.localDir = sub;
                children.push_back(std::move(c));
            }
            for (auto jt = children.rbegin(); jt != children.rend(); ++jt)
                stack.push_back(std::move(*jt));
        }
    }
    return !anyFail;
}

namespace {

// 远程内容的迭代统计：同时累计条目数与字节数。
// 用虚拟根帧装下顶层条目，栈展开子目录；帧全部在堆上。
struct StatResult { uint64_t entries = 0; uint64_t bytes = 0; };

StatResult StatRemote(FtpClient& client, const std::vector<FtpOps::RemoteItem>& items) {
    StatResult r;
    std::vector<DirFrame> stack;
    DirFrame root;
    root.isVirtual = true;
    root.listed = true;
    for (const auto& it : items)
        root.entries.push_back({it.name, it.isDir, it.size, ""});
    stack.push_back(std::move(root));

    while (!stack.empty()) {
        DirFrame& fr = stack.back();
        if (!fr.listed) {
            if (!client.ChangeDir(fr.remoteAbs)) {
                stack.pop_back();   // 进不去的目录：跳过其内容（尽力统计）
                continue;
            }
            if (!client.List(fr.entries)) { stack.pop_back(); continue; }
            fr.listed = true;
            fr.idx = 0;
        }

        bool descended = false;
        while (fr.idx < fr.entries.size()) {
            const FtpClient::Entry& e = fr.entries[fr.idx];
            if (e.isDir) {
                ++r.entries;   // 目录自身计一项
                DirFrame nf;
                nf.remoteAbs = JoinRemote(fr.remoteAbs, e.name);
                fr.idx++;              // 先推进（在压栈可能重分配之前）
                stack.push_back(std::move(nf));
                descended = true;
                break;
            }
            ++r.entries;
            r.bytes += e.size;
            ++fr.idx;
        }
        if (!descended) stack.pop_back();   // 本帧内容处理完
    }
    return r;
}

} // namespace

bool FtpOps::MeasureRemote(FtpClient& client, const std::vector<RemoteItem>& items,
                           uint64_t& totalBytes) {
    DirGuard guard(client);
    totalBytes = StatRemote(client, items).bytes;
    return true;   // 尽力统计，部分目录不可达不视为整体失败
}

// ===========================================================================
//  删除（迭代，后序：先清空目录内容，最后删除目录本身）
// ===========================================================================

bool FtpOps::CountRemote(FtpClient& client, const std::vector<RemoteItem>& items,
                         uint64_t& totalEntries) {
    DirGuard guard(client);
    totalEntries = StatRemote(client, items).entries;
    return true;
}

bool FtpOps::Remove(FtpClient& client, const std::vector<RemoteItem>& items,
                    uint64_t totalEntries, const ProgressCb& onProgress,
                    const CancelFn& isCancelled) {
    DirGuard guard(client);
    const std::string base = client.CurrentDir().empty() ? "/" : client.CurrentDir();

    bool anyFail = false;
    uint64_t done = 0;

    // 顶层条目装入虚拟根帧（不删除根帧自身、也不对其 CWD）
    std::vector<DirFrame> stack;
    DirFrame root;
    root.remoteAbs = base;
    root.isVirtual = true;
    root.listed = true;
    for (const auto& it : items)
        root.entries.push_back({it.name, it.isDir, it.size, ""});
    stack.push_back(std::move(root));

    while (!stack.empty()) {
        if (Cancelled(isCancelled)) break;
        DirFrame& fr = stack.back();

        if (!fr.listed) {
            if (!client.ChangeDir(fr.remoteAbs)) {
                anyFail = true;
                stack.pop_back();
                continue;
            }
            if (!client.List(fr.entries)) {
                anyFail = true;
                stack.pop_back();
                continue;
            }
            fr.listed = true;
            fr.idx = 0;
        }

        // 处理子条目：文件直接删；目录压入新帧（后序）
        bool descended = false;
        while (fr.idx < fr.entries.size()) {
            const FtpClient::Entry& e = fr.entries[fr.idx];
            if (e.isDir) {
                DirFrame nf;
                nf.remoteAbs = JoinRemote(fr.remoteAbs, e.name);
                fr.idx++;                    // 先推进（压栈可能触发重分配）
                stack.push_back(std::move(nf));
                descended = true;
                break;
            }
            std::wstring wname = Utf8ToWide(e.name);
            Emit(onProgress, wname, done, totalEntries);
            bool inDir = client.ChangeDir(fr.remoteAbs);
            bool ok = inDir && client.RemoveFile(e.name);
            if (ok) {
                ++done;
                Emit(onProgress, wname, done, totalEntries);
            } else {
                anyFail = true;
            }
            ++fr.idx;
        }
        if (descended) continue;

        // 本帧子内容已处理完：虚拟帧直接出栈；普通帧 RMD 自身
        bool isVirtual = fr.isVirtual;
        std::string abs = fr.remoteAbs;
        stack.pop_back();
        if (!isVirtual) {
            // RMD 时不能停留在被删目录内：CWD 父目录后用名字删除
            std::wstring wname = Utf8ToWide(BaseNameRemote(abs));
            bool inDir = client.ChangeDir(ParentRemote(abs));
            bool ok = inDir && client.RemoveDir(BaseNameRemote(abs));
            if (ok) {
                ++done;
                Emit(onProgress, wname, done, totalEntries);
            } else {
                anyFail = true;   // 通常为内容未清空或权限不足
            }
        }
    }
    return !anyFail;
}
