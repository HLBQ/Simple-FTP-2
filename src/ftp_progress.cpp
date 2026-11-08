#include "ftp_progress.h"

#include <commctrl.h>
#include <atomic>
#include <mutex>
#include <string>
#include <cstdio>

#pragma comment(lib, "comctl32.lib")

// ============================================================================
//  传输进度弹窗实现
//
//  线程模型：
//    - 工作线程调用 Begin/Update/End，通过 PostMessage 向 UI 线程投递
//      自定义消息（WM_APP_PROG_*），由 UI 线程创建/刷新/销毁窗口。
//    - 进度数据（done/total）用原子变量保存，UI 线程定时器读取并刷新。
//    - 取消标志用原子变量，工作线程轮询 IsCancelled()。
// ============================================================================

namespace {

// 自定义消息
#define WM_APP_PROG_BEGIN  (WM_APP + 20)
#define WM_APP_PROG_UPDATE (WM_APP + 21)
#define WM_APP_PROG_END    (WM_APP + 22)
#define WM_APP_PROG_FILE   (WM_APP + 23)
#define WM_APP_PROG_ACTION (WM_APP + 24)

// 控件 ID
#define IDC_PROG_BAR       5001
#define IDC_PROG_PERCENT   5002
#define IDC_PROG_DETAIL    5003
#define IDC_PROG_SPEED     5004
#define IDC_PROG_REMAIN    5005
#define IDC_PROG_CANCEL    5006
#define IDC_PROG_TIMER     5007
#define IDC_PROG_FILE      5008
#define IDC_PROG_CLOSETIMER 5009

// 快速传输（如本机回环 127.0.0.1 上的小文件）可能在几毫秒内完成，
// 快于一次界面绘制与 200ms 定时刷新，导致进度窗口“创建即销毁”、
// 用户根本看不到任何反馈。因此保证窗口至少可见该时长（毫秒）。
static const DWORD kMinVisibleMs = 600;

// 全局状态
HWND g_hProgWnd = nullptr;          // 进度窗口句柄
HWND g_hOwner = nullptr;            // 父窗口
HWND g_hBar = nullptr;              // 进度条
HWND g_hPercent = nullptr;          // 百分比文字
HWND g_hDetail = nullptr;           // 已传/总大小
HWND g_hSpeed = nullptr;            // 速度
HWND g_hRemain = nullptr;           // 剩余时间
HWND g_hCancel = nullptr;           // 取消按钮

std::wstring g_action;              // 操作名称
std::wstring g_fileName;            // 文件名

std::atomic<uint64_t> g_done{0};    // 已传输
std::atomic<uint64_t> g_total{0};   // 总大小
std::atomic<bool> g_cancelled{false};
std::atomic<bool> g_active{false};  // 是否有进行中的传输
std::atomic<bool> g_verifying{false};  // 是否处于哈希校验阶段
std::atomic<uint64_t> g_verifyDone{0};  // 校验阶段：已校验字节
std::atomic<uint64_t> g_verifyTotal{0}; // 校验阶段：当前文件总字节

// 速度计算（仅 UI 线程访问）
uint64_t g_lastDone = 0;
DWORD g_lastTick = 0;
double g_speed = 0.0;               // 字节/秒（平滑后）

// 上次向 UI 线程投递刷新消息的时间戳（用于节流，工作线程访问）
std::atomic<DWORD> g_lastPostTick{0};

// 本次传输进度窗口的创建时间戳（仅 UI 线程访问），用于保证最短可见时长
DWORD g_beginTick = 0;

// 工作线程待更新的文件名（多文件传输时切换显示），经 UI 消息应用到窗口
std::mutex g_pendingFileMutex;
std::wstring g_pendingFileName;

// 文件名更新节流（避免小文件过多时一闪一闪）
DWORD g_lastFileUpdateTick = 0;
static const DWORD kFileUpdateIntervalMs = 120;  // 文件名最短刷新间隔

// 把长路径做中间省略：保留开头和结尾，中间用 "..." 替换。
// 例如 "D:\\folder1\\folder2\\verylongfilename.txt" → "D:\\...\\verylongfilename.txt"
std::wstring EllipsisMiddle(HWND hCtrl, const std::wstring& text) {
    if (!hCtrl || text.empty()) return text;
    HDC hdc = GetDC(hCtrl);
    if (!hdc) return text;
    HFONT hFont = (HFONT)SendMessage(hCtrl, WM_GETFONT, 0, 0);
    HGDIOBJ oldFont = SelectObject(hdc, hFont ? hFont : GetStockObject(DEFAULT_GUI_FONT));

    RECT rc;
    GetClientRect(hCtrl, &rc);
    int maxW = rc.right - rc.left - 4;  // 留一点边距
    if (maxW <= 0) { SelectObject(hdc, oldFont); ReleaseDC(hCtrl, hdc); return text; }

    SIZE sz{};
    GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &sz);
    if (sz.cx <= maxW) {
        SelectObject(hdc, oldFont);
        ReleaseDC(hCtrl, hdc);
        return text;
    }

    const std::wstring ellipsis = L"...";
    SIZE eSz{};
    GetTextExtentPoint32W(hdc, ellipsis.c_str(), 3, &eSz);

    // 从两端向中间收缩，保留前缀和后缀
    size_t left = 0, right = text.size();
    while (left < right) {
        size_t mid = (left + right + 1) / 2;
        // 取前半和后半各一半
        size_t half = mid / 2;
        size_t takeFront = half;
        size_t takeBack = mid - half;
        if (takeFront > text.size() / 2) takeFront = text.size() / 2;
        if (takeBack > text.size() - takeFront) takeBack = text.size() - takeFront;

        std::wstring test = text.substr(0, takeFront) + ellipsis +
                            text.substr(text.size() - takeBack);
        GetTextExtentPoint32W(hdc, test.c_str(), (int)test.size(), &sz);
        if (sz.cx <= maxW) {
            left = mid;
        } else {
            right = mid - 1;
        }
    }

    std::wstring result;
    if (left > 0) {
        size_t half = left / 2;
        size_t takeFront = half;
        size_t takeBack = left - half;
        if (takeFront > text.size() / 2) takeFront = text.size() / 2;
        if (takeBack > text.size() - takeFront) takeBack = text.size() - takeFront;
        if (takeFront == 0) takeFront = 1;
        if (takeBack == 0) takeBack = 1;
        if (takeFront + takeBack >= text.size()) {
            result = text;
        } else {
            result = text.substr(0, takeFront) + ellipsis +
                     text.substr(text.size() - takeBack);
        }
    } else {
        result = ellipsis;
    }

    SelectObject(hdc, oldFont);
    ReleaseDC(hCtrl, hdc);
    return result;
}

// 把字节数格式化为可读字符串
std::wstring FormatBytes(uint64_t bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB", L"PB"};
    if (bytes < 1024) {
        wchar_t buf[64];
        swprintf_s(buf, L"%llu B", (unsigned long long)bytes);
        return buf;
    }
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 5) { v /= 1024.0; ++u; }
    wchar_t buf[64];
    if (v >= 100.0) swprintf_s(buf, L"%.1f %s", v, units[u]);
    else            swprintf_s(buf, L"%.2f %s", v, units[u]);
    return buf;
}

// 把秒数格式化为“X 时 Y 分 Z 秒”或“Y 分 Z 秒”或“Z 秒”
std::wstring FormatTime(double seconds) {
    if (seconds < 0 || seconds > 86400 * 30) return L"--";
    int s = (int)(seconds + 0.5);
    wchar_t buf[64];
    if (s >= 3600) {
        swprintf_s(buf, L"%d 时 %d 分 %d 秒", s / 3600, (s % 3600) / 60, s % 60);
    } else if (s >= 60) {
        swprintf_s(buf, L"%d 分 %d 秒", s / 60, s % 60);
    } else {
        swprintf_s(buf, L"%d 秒", s);
    }
    return buf;
}

// 更新文件名标签：校验阶段加“校验中: ”前缀，让用户明确知道当前在做什么。
void UpdateFileLabelText() {
    if (!g_hProgWnd) return;
    HWND hFile = GetDlgItem(g_hProgWnd, IDC_PROG_FILE);
    if (!hFile) return;
    std::wstring text = g_verifying.load()
        ? (L"校验中: " + g_fileName)
        : g_fileName;
    std::wstring display = EllipsisMiddle(hFile, text);
    // 文本没变就不 SetWindowText，避免 200ms 定时刷新造成闪烁
    int len = GetWindowTextLengthW(hFile);
    std::wstring cur((size_t)len + 1, L'\0');
    GetWindowTextW(hFile, &cur[0], len + 1);
    cur.resize((size_t)len);
    if (display != cur) SetWindowTextW(hFile, display.c_str());
}

// 仅当标题变化时才 SetWindowText，避免定时刷新造成闪烁
void SetProgressTitle(const std::wstring& title) {
    if (!g_hProgWnd) return;
    wchar_t cur[256] = {0};
    GetWindowTextW(g_hProgWnd, cur, 256);
    if (title != cur) SetWindowTextW(g_hProgWnd, title.c_str());
}

// 刷新界面（UI 线程）
void RefreshUi() {
    if (!g_hProgWnd) return;
    uint64_t done = g_done.load();
    uint64_t total = g_total.load();
    bool verifying = g_verifying.load();
    uint64_t verifyDone = g_verifyDone.load();
    uint64_t verifyTotal = g_verifyTotal.load();

    // 标题 / 文件名：校验阶段明确显示“正在校验”
    SetProgressTitle(verifying ? (g_action + L" - 正在校验")
                               : (g_action + L"进度"));
    UpdateFileLabelText();

    // 整体进度条与百分比：校验阶段也保持在当前已完成位置，绝不回退
    int percent = 0;
    if (total > 0) {
        percent = (int)((done * 100) / total);
        if (percent > 100) percent = 100;
    }
    SendMessage(g_hBar, PBM_SETPOS, percent, 0);

    wchar_t pbuf[32];
    if (total > 0) swprintf_s(pbuf, L"%d%%", percent);
    else           swprintf_s(pbuf, L"--");
    SetWindowTextW(g_hPercent, pbuf);

    // 明细：普通阶段显示“已传 / 总量”；校验阶段显示本文件校验进度
    std::wstring detail;
    if (verifying) {
        if (verifyTotal > 0) {
            int vp = (int)((verifyDone * 100) / verifyTotal);
            if (vp > 100) vp = 100;
            detail = L"校验中 " + std::to_wstring(vp) + L"%  " +
                     FormatBytes(verifyDone) + L" / " + FormatBytes(verifyTotal);
        } else {
            detail = L"校验中...";
        }
    } else {
        detail = FormatBytes(done);
        if (total > 0) detail += L" / " + FormatBytes(total);
    }
    SetWindowTextW(g_hDetail, detail.c_str());

    if (verifying) {
        // 校验阶段：整体进度不动，速度/剩余时间切换为校验提示；
        // 同时更新速度基线，避免校验结束后第一帧用很长的时间间隔计算速度。
        g_lastDone = done;
        g_lastTick = GetTickCount();
        SetWindowTextW(g_hSpeed, L"校验中...");
        SetWindowTextW(g_hRemain, L"--");
        return;
    }

    // 速度：基于两次刷新之间的增量计算，并做平滑
    DWORD now = GetTickCount();
    if (g_lastTick != 0) {
        DWORD dt = now - g_lastTick;
        if (dt >= 200) {
            // done 倒退（阶段切换/文件跳过导致 done 重置）时重置基线，
            // 避免计算出负速度或爆炸式瞬时速度
            if (done < g_lastDone) {
                g_lastDone = done;
                g_lastTick = now;
            } else {
                uint64_t delta = done - g_lastDone;
                double inst = (double)delta * 1000.0 / (double)dt;  // 字节/秒
                // 钳制瞬时速度上限（100 GB/s），防止跳过文件时出现 PB 级假象
                const double MAX_INST = 100.0 * 1024 * 1024 * 1024;
                if (inst > MAX_INST) inst = MAX_INST;
                // 指数平滑，避免速度数字剧烈跳动
                g_speed = (g_speed <= 0.0) ? inst : (g_speed * 0.6 + inst * 0.4);
                g_lastDone = done;
                g_lastTick = now;
            }
        }
    } else {
        g_lastDone = done;
        g_lastTick = now;
    }
    std::wstring speedStr = FormatBytes((uint64_t)g_speed) + L"/s";
    SetWindowTextW(g_hSpeed, speedStr.c_str());

    // 剩余时间
    std::wstring remainStr = L"--";
    if (total > 0 && g_speed > 1.0 && done < total) {
        double remain = (double)(total - done) / g_speed;
        remainStr = FormatTime(remain);
    } else if (total > 0 && done >= total) {
        remainStr = L"0 秒";
    }
    SetWindowTextW(g_hRemain, remainStr.c_str());
}

// 进度窗口过程
LRESULT CALLBACK ProgWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 字体进程级缓存：窗口可被反复创建/销毁（窗口复用），
        // 每次重建都 CreateFont 会累积泄漏 GDI 句柄。
        static HFONT f = nullptr;
        static HFONT fb = nullptr;
        if (!f) {
            f = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
            fb = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0,
                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        }

        // 文件名标签（SS_LEFT 即可，省略由 EllipsisMiddle 手动处理）
        HWND hFile = CreateWindowW(L"STATIC", g_fileName.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            15, 12, 400, 20, hWnd, (HMENU)IDC_PROG_FILE, nullptr, nullptr);
        SendMessage(hFile, WM_SETFONT, (WPARAM)fb, TRUE);
        SetWindowTextW(hFile, EllipsisMiddle(hFile, g_fileName).c_str());

        // 进度条
        g_hBar = CreateWindowExW(0, PROGRESS_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            15, 40, 400, 22, hWnd, (HMENU)IDC_PROG_BAR, nullptr, nullptr);
        SendMessage(g_hBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(g_hBar, PBM_SETPOS, 0, 0);

        // 百分比
        g_hPercent = CreateWindowW(L"STATIC", L"0%",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            15, 68, 60, 20, hWnd, (HMENU)IDC_PROG_PERCENT, nullptr, nullptr);

        // 已传/总大小
        g_hDetail = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            80, 68, 335, 20, hWnd, (HMENU)IDC_PROG_DETAIL, nullptr, nullptr);

        // 速度
        HWND hSpeedLabel = CreateWindowW(L"STATIC", L"速度:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            15, 94, 50, 20, hWnd, nullptr, nullptr, nullptr);
        g_hSpeed = CreateWindowW(L"STATIC", L"--",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            65, 94, 150, 20, hWnd, (HMENU)IDC_PROG_SPEED, nullptr, nullptr);

        // 剩余时间
        HWND hRemainLabel = CreateWindowW(L"STATIC", L"剩余:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            225, 94, 50, 20, hWnd, nullptr, nullptr, nullptr);
        g_hRemain = CreateWindowW(L"STATIC", L"--",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            275, 94, 140, 20, hWnd, (HMENU)IDC_PROG_REMAIN, nullptr, nullptr);

        // 取消按钮
        g_hCancel = CreateWindowW(L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            330, 122, 85, 30, hWnd, (HMENU)IDC_PROG_CANCEL, nullptr, nullptr);
        SendMessage(g_hCancel, WM_SETFONT, (WPARAM)f, TRUE);

        HWND all[] = {hFile, g_hBar, g_hPercent, g_hDetail, hSpeedLabel,
                      g_hSpeed, hRemainLabel, g_hRemain};
        for (HWND h : all) SendMessage(h, WM_SETFONT, (WPARAM)f, TRUE);

        // 定时刷新（每 200ms）
        SetTimer(hWnd, IDC_PROG_TIMER, 200, nullptr);
        return 0;
    }
    case WM_TIMER:
        if (wParam == IDC_PROG_TIMER) {
            RefreshUi();
        } else if (wParam == IDC_PROG_CLOSETIMER) {
            // 最短可见时间已到：真正销毁窗口
            KillTimer(hWnd, IDC_PROG_CLOSETIMER);
            DestroyWindow(hWnd);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_PROG_CANCEL) {
            g_cancelled.store(true);
            // 禁用按钮，提示正在取消
            EnableWindow(g_hCancel, FALSE);
            SetWindowTextW(g_hCancel, L"取消中...");
        }
        return 0;
    case WM_CLOSE:
        // 关闭窗口视为取消
        g_cancelled.store(true);
        return 0;
    case WM_DESTROY:
        KillTimer(hWnd, IDC_PROG_TIMER);
        KillTimer(hWnd, IDC_PROG_CLOSETIMER);
        g_hProgWnd = nullptr;
        g_hBar = g_hPercent = g_hDetail = g_hSpeed = g_hRemain = g_hCancel = nullptr;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// 注册进度窗口类（只注册一次）
void EnsureClassRegistered() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ProgWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"FtpProgressWnd";
    RegisterClassExW(&wc);
    registered = true;
}

// 把进度窗居中：主窗口可见时居中于主窗口；主窗口最小化时居中于屏幕。
void CenterProgressWindow(HWND hWnd) {
    if (!hWnd) return;
    RECT dr;
    GetWindowRect(hWnd, &dr);
    int dw = dr.right - dr.left;
    int dh = dr.bottom - dr.top;

    bool ownerUsable = g_hOwner && IsWindow(g_hOwner) &&
                       IsWindowVisible(g_hOwner) && !IsIconic(g_hOwner);
    HMONITOR mon = g_hOwner
        ? MonitorFromWindow(g_hOwner, MONITOR_DEFAULTTONEAREST)
        : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    RECT work{};
    if (mon && GetMonitorInfoW(mon, &mi)) {
        work = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }

    RECT area = work;
    if (ownerUsable) {
        GetWindowRect(g_hOwner, &area);
        if (area.left < work.left) area.left = work.left;
        if (area.top < work.top) area.top = work.top;
        if (area.right > work.right) area.right = work.right;
        if (area.bottom > work.bottom) area.bottom = work.bottom;
    }

    int x = area.left + ((area.right - area.left) - dw) / 2;
    int y = area.top + ((area.bottom - area.top) - dh) / 2;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
    if (x + dw > work.right) x = work.right - dw;
    if (y + dh > work.bottom) y = work.bottom - dh;
    SetWindowPos(hWnd, HWND_TOPMOST, x, y, 0, 0,
                 SWP_NOSIZE | SWP_SHOWWINDOW);
}

// UI 线程：创建并显示进度窗口
void DoBegin() {
    // 若窗口正处于“最短可见时间”的延迟关闭阶段，直接复用：
    // 取消待执行的销毁，更新文件名即可。
    if (g_hProgWnd) {
        KillTimer(g_hProgWnd, IDC_PROG_CLOSETIMER);
        // 复用窗口时把上一次“正在校验”的标题/前缀恢复成普通传输状态
        SetProgressTitle(g_action + L"进度");
        UpdateFileLabelText();
        // 上次被用户最小化了：新传输开始时恢复显示
        if (IsIconic(g_hProgWnd)) ShowWindow(g_hProgWnd, SW_RESTORE);
        SetForegroundWindow(g_hProgWnd);
        return;
    }
    EnsureClassRegistered();

    RECT rc = {0, 0, 430, 165};
    AdjustWindowRect(&rc, WS_CAPTION | WS_SYSMENU, FALSE);
    g_hProgWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW,
        L"FtpProgressWnd", (g_action + L"进度").c_str(),
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        g_hOwner, nullptr, GetModuleHandleW(nullptr), nullptr);

    // 居中显示（主窗口最小化时居中到屏幕）
    CenterProgressWindow(g_hProgWnd);
    SetForegroundWindow(g_hProgWnd);
    RefreshUi();
    // 强制立即同步绘制（包括所有子控件）。Begin 可能在某个模态对话框/
    // 右键菜单的调用栈内被调用，此刻消息循环尚无法派发 WM_PAINT；
    // 若不主动绘制，快速传输时窗口会在首次绘制前就被销毁。
    RedrawWindow(g_hProgWnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// UI 线程：销毁进度窗口
void DoEnd() {
    if (!g_hProgWnd) return;
    // 结束前强制刷新最后一个文件名（避免节流导致显示的是旧文件名）
    {
        std::wstring name;
        {
            std::lock_guard<std::mutex> lk(g_pendingFileMutex);
            name = g_pendingFileName;
        }
        if (!name.empty() && name != g_fileName) {
            g_fileName = name;
            UpdateFileLabelText();
        }
    }
    // 结束前刷新一次，显示最终进度（100%）
    RefreshUi();
    RedrawWindow(g_hProgWnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    DWORD elapsed = GetTickCount() - g_beginTick;
    if (elapsed < kMinVisibleMs) {
        // 传输过快：延长展示剩余时间后再销毁，保证用户看到反馈
        SetTimer(g_hProgWnd, IDC_PROG_CLOSETIMER,
                 kMinVisibleMs - elapsed, nullptr);
    } else {
        DestroyWindow(g_hProgWnd);
        g_hProgWnd = nullptr;
    }
}

} // namespace

void ProgressDialog::Begin(HWND hOwner, const std::wstring& action,
                           const std::wstring& fileName, uint64_t total) {
    g_hOwner = hOwner;
    g_action = action;
    g_fileName = fileName;
    g_done.store(0);
    g_total.store(total);
    g_cancelled.store(false);
    g_active.store(true);
    g_lastDone = 0;
    g_lastTick = 0;
    g_speed = 0.0;
    g_verifying.store(false);
    g_verifyDone.store(0);
    g_verifyTotal.store(0);
    {
        // 清掉上一次传输遗留的待更新文件名，避免结束时显示旧文件名
        std::lock_guard<std::mutex> lk(g_pendingFileMutex);
        g_pendingFileName.clear();
    }
    g_lastFileUpdateTick = 0;   // 本次传输的第一个文件名立即显示
    g_beginTick = GetTickCount();

    // 本函数在 UI 线程调用：直接同步创建窗口，确保弹窗立即出现，
    // 不依赖消息循环（避免小文件传输时窗口一闪而过或根本不显示）。
    DoBegin();
}

void ProgressDialog::Update(uint64_t done, uint64_t total, bool verifying,
                            uint64_t verifyDone, uint64_t verifyTotal) {
    g_done.store(done);
    if (total > 0) g_total.store(total);
    g_verifying.store(verifying);
    g_verifyDone.store(verifying ? verifyDone : 0);
    g_verifyTotal.store(verifying ? verifyTotal : 0);
    // 主动通知 UI 线程刷新，确保小文件/高速传输时也能看到进度变化。
    // 用时间戳节流：两次刷新间隔至少 50ms，避免消息洪水拖慢传输。
    DWORD now = GetTickCount();
    if (now - g_lastPostTick >= 50) {
        g_lastPostTick = now;
        if (g_hOwner) PostMessage(g_hOwner, WM_APP_PROG_UPDATE, 0, 0);
    }
}

void ProgressDialog::End() {
    g_active.store(false);
    // 结束前先让 UI 刷新一次，显示最终进度（100%），再销毁窗口
    if (g_hOwner) {
        PostMessage(g_hOwner, WM_APP_PROG_UPDATE, 0, 0);
        PostMessage(g_hOwner, WM_APP_PROG_END, 0, 0);
    }
}

bool ProgressDialog::IsCancelled() {
    return g_cancelled.load();
}

bool ProgressDialog::IsBusy() {
    return g_active.load();
}

void ProgressDialog::SetFileName(const std::wstring& name) {
    {
        std::lock_guard<std::mutex> lk(g_pendingFileMutex);
        g_pendingFileName = name;
    }
    if (g_hOwner) PostMessage(g_hOwner, WM_APP_PROG_FILE, 0, 0);
}

void ProgressDialog::SetAction(const std::wstring& action) {
    g_action = action;
    if (g_hOwner) PostMessage(g_hOwner, WM_APP_PROG_ACTION, 0, 0);
}

bool ProgressDialog::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_APP_PROG_BEGIN:
        DoBegin();
        return true;
    case WM_APP_PROG_UPDATE:
        // 工作线程主动请求刷新：立即更新进度显示
        RefreshUi();
        return true;
    case WM_APP_PROG_FILE: {
        // 工作线程切换了当前文件：更新显示名（多文件传输）
        std::wstring name;
        {
            std::lock_guard<std::mutex> lk(g_pendingFileMutex);
            name = g_pendingFileName;
        }
        g_fileName = name;
        if (g_hProgWnd) {
            // 节流：小文件过多时避免一闪一闪，最短 120ms 才刷新一次
            DWORD now = GetTickCount();
            if (now - g_lastFileUpdateTick >= kFileUpdateIntervalMs) {
                g_lastFileUpdateTick = now;
                UpdateFileLabelText();
            }
        }
        return true;
    }
    case WM_APP_PROG_ACTION:
        // 工作线程切换了操作类型（下载/上传/校验）：更新窗口标题
        if (g_hProgWnd) {
            SetProgressTitle(g_action + L"进度");
        }
        return true;
    case WM_APP_PROG_END:
        DoEnd();
        return true;
    }
    return false;
}
