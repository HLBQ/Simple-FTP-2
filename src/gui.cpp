#include "gui.h"
#include "ftp_server.h"
#include "ftp_client.h"
#include "ftp_drag.h"
#include "ftp_progress.h"
#include "ftp_fsops.h"
#include "winlongpath.h"
#include "config.h"
#include "icon_data.h"
#include "ghicon_data.h"
#include "siteicon_data.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>   // IFileDialog（新版文件夹选择器）
#include <shellapi.h>
#include <commdlg.h>
#include <wincodec.h>
#include <string>
#include <memory>
#include <sstream>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <deque>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <filesystem>
#include <cstdio>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;

// 控件 ID
#define IDC_IP_LABEL      1001
#define IDC_PORT_EDIT     1002
#define IDC_PATH_EDIT     1003
#define IDC_BROWSE_BTN    1004
#define IDC_CHK_READ      1005
#define IDC_CHK_WRITE     1006
#define IDC_CHK_DEL       1007
#define IDC_CHK_MKDIR     1008
#define IDC_CHK_RMDIR     1009
#define IDC_CHK_RENAME    1010
#define IDC_CHK_LIST      1011
#define IDC_TOGGLE_BTN    1012
#define IDC_LOG_EDIT      1013
#define IDC_PASV_START    1014
#define IDC_PASV_END      1015
#define IDC_STATUS        1016
#define IDC_CHK_ALL       1017
#define IDC_IP_LIST       1018
#define IDC_IP_ADD        1019
#define IDC_IP_DEL        1020
#define IDC_IP_EDIT       1021
#define IDC_TAB           1022
#define IDC_BM_LIST       1023
#define IDC_BM_ADD        1024
#define IDC_BM_DEL        1025
#define IDC_BM_OPEN       1026
#define IDC_ADV_ENABLE    1027
#define IDC_ADV_LIST      1028
#define IDC_ADV_REFRESH   1029
#define IDC_ADV_HINT      1030
#define IDC_ADV_ALLOW     1031
#define IDC_ADV_DENY      1032
#define IDC_BM_EDIT       1033
#define IDC_PERF_TIMEOUT  1040
#define IDC_PERF_NODELAY  1041
#define IDC_PERF_SOCKBUF  1042
#define IDC_PERF_APPBUF   1043
#define IDC_PERF_MAXFILE  1044
#define IDC_PERF_MAXCONN  1045
#define IDC_PERF_RESET    1046
#define IDC_PERF_HINT     1047
#define IDC_PERF_SAVE     1048
#define IDC_PERF_XFERMODE 1049
// 滑动条
#define IDC_SLD_TIMEOUT   1050
#define IDC_SLD_SOCKBUF   1051
#define IDC_SLD_APPBUF    1052
#define IDC_SLD_MAXFILE   1053
#define IDC_SLD_MAXCONN   1054
// 安全提醒页
#define IDC_SEC_DELFILE   1060
#define IDC_SEC_DELDIR    1061
#define IDC_SEC_RENAME    1062
#define IDC_SEC_OVERWRITE 1063
#define IDC_SEC_TIMEOUT   1064
#define IDC_SEC_HINT      1065
#define IDC_SEC_SAVE      1066
#define IDC_SEC_DOWNLOAD  1067
#define IDC_SEC_RECYCLE   1068
#define IDC_SLD_SECTIMEOUT 1069
#define IDC_SEC_TIMEOUT_LABEL 1070
#define IDC_SEC_RECYCLE_HINT 1071
#define IDC_SEC_IPMODE    1072   // IP 名单模式下拉框
#define IDC_SEC_RECYCLEMODE 1073 // 删除方式下拉框
#define IDC_BM_CHECK      1074   // 快捷方式：检查连通
#define IDC_SEC_RECYCLE_LABEL 1075 // 删除方式标签
#define IDC_SEC_IPMODE_LABEL  1076 // IP 名单模式标签
#define IDC_ADV_HIDE      1077   // 高级设置：隐藏选中项
#define IDC_ADV_UNHIDE    1078   // 高级设置：取消隐藏
#define IDC_BM_STATUS     1079   // 快捷方式：状态列（占位）
// 运行日志页：日志分类过滤双态按钮
#define IDC_LOG_FILTER_UPLOAD   1080
#define IDC_LOG_FILTER_DOWNLOAD 1081
#define IDC_LOG_FILTER_CLIENT   1082
#define IDC_LOG_FILTER_OTHER    1083
#define IDC_LOG_FILTER_WARN     1084
#define IDC_VIEW_TOGGLE         1085   // 高级模式 / 精简模式切换
#define IDC_CHK_COMPACT_READ    1086   // 精简模式：读取
#define IDC_CHK_COMPACT_WRITE   1087   // 精简模式：写入
#define IDC_CHK_COMPACT_DELETE  1088   // 精简模式：删除
#define IDC_SEC_RESET           1089   // 安全提醒：恢复默认
#define IDC_OTHER_BIND          1100   // 其他设置：绑定网卡
#define IDC_OTHER_ENCODING      1101   // 其他设置：文件名编码
#define IDC_OTHER_SAVE          1102   // 其他设置：保存
#define IDC_OTHER_RESET         1103   // 其他设置：恢复默认
#define IDC_OTHER_AUTHOR        1104   // 其他设置页脚：作者信息（仅显示）
#define IDC_OTHER_GH_ICON       1105   // 其他设置页脚：GitHub 图标（可点击）
#define IDC_OTHER_GH_GO         1106   // 其他设置页脚：前往 GitHub 链接（可点击）
#define IDC_OTHER_SITE_ICON     1107   // 其他设置页脚：网站图标（可点击）
#define IDC_OTHER_SITE_GO       1108   // 其他设置页脚：前往网站链接（可点击）
// 网络发现页
#define IDC_DISC_NET            1110   // 扫描网段类型
#define IDC_DISC_PORTS          1111   // 扫描端口
#define IDC_DISC_SCAN           1112   // 开始扫描
#define IDC_DISC_LIST           1113   // 扫描结果列表
#define IDC_DISC_STATUS         1114   // 扫描状态
// 扫描进度窗口
#define IDC_SCAN_PROGRESS       1200
#define IDC_SCAN_INFO           1201
#define IDC_SCAN_STOP           1202
// 客户端页
#define IDC_CLI_HOST       1090   // 服务器地址输入框
#define IDC_CLI_PORT       1091   // 端口输入框
#define IDC_CLI_ENTER      1092   // 进入按钮
#define IDC_CLI_REFRESH    1093   // 刷新按钮
#define IDC_CLI_BACK       1094   // 返回按钮
#define IDC_CLI_LIST       1095   // 文件列表
#define IDC_CLI_PATH       1096   // 当前路径显示
#define IDC_CLI_USER       1097   // 用户名输入框
#define IDC_CLI_PASS       1098   // 密码输入框
#define IDC_CLI_AUTHMODE   1099   // 登录方式下拉框（匿名 / 账号密码）
// 客户端右键菜单
#define IDM_CLI_BASE       2100
#define IDM_CLI_OPEN       (IDM_CLI_BASE + 0)   // 打开/进入
#define IDM_CLI_DOWNLOAD   (IDM_CLI_BASE + 1)   // 下载
#define IDM_CLI_UPLOAD     (IDM_CLI_BASE + 2)   // 上传
#define IDM_CLI_NEWFOLDER  (IDM_CLI_BASE + 3)   // 新建文件夹
#define IDM_CLI_DELETE     (IDM_CLI_BASE + 4)   // 删除
#define IDM_CLI_RENAME     (IDM_CLI_BASE + 5)   // 重命名
#define IDM_CLI_REFRESH    (IDM_CLI_BASE + 6)   // 刷新
#define IDM_CLI_EXPLORER   (IDM_CLI_BASE + 7)   // 用资源管理器打开
#define IDM_CLI_UPLOAD_DIR (IDM_CLI_BASE + 8)   // 上传文件夹
// 快捷方式右键菜单
#define IDM_BM_BASE        2200
#define IDM_BM_OPEN_CLIENT (IDM_BM_BASE + 0)   // 在内置客户端打开
#define IDM_BM_OPEN_EXPLORER (IDM_BM_BASE + 1) // 用资源管理器打开
#define IDM_BM_EDIT        (IDM_BM_BASE + 2)   // 编辑
#define IDM_BM_DELETE      (IDM_BM_BASE + 3)   // 删除
// 网络发现结果右键菜单
#define IDM_DISC_BASE        2300
#define IDM_DISC_CLIENT      (IDM_DISC_BASE + 0) // 在伪客户端中打开
#define IDM_DISC_EXPLORER    (IDM_DISC_BASE + 1) // 在资源管理器中打开
#define IDM_DISC_ADD_BM      (IDM_DISC_BASE + 2) // 添加到快捷方式页
// 敏感操作确认对话框
#define IDC_CONFIRM_TEXT  3001
#define IDC_CONFIRM_ALLOW 3002
#define IDC_CONFIRM_DENY  3003
#define IDC_CONFIRM_TIMER 3004

// 右键菜单项
#define IDM_PERM_BASE     2000
#define IDM_PERM_READ     (IDM_PERM_BASE + 0)
#define IDM_PERM_WRITE    (IDM_PERM_BASE + 1)
#define IDM_PERM_DEL      (IDM_PERM_BASE + 2)
#define IDM_PERM_MKDIR    (IDM_PERM_BASE + 3)
#define IDM_PERM_RMDIR    (IDM_PERM_BASE + 4)
#define IDM_PERM_RENAME   (IDM_PERM_BASE + 5)
#define IDM_PERM_LIST     (IDM_PERM_BASE + 6)
#define IDM_PERM_ALL      (IDM_PERM_BASE + 7)
#define IDM_PERM_NONE     (IDM_PERM_BASE + 8)
#define IDM_PERM_CLEAR    (IDM_PERM_BASE + 9)
#define IDM_PERM_HIDE     (IDM_PERM_BASE + 10)
#define IDM_PERM_UNHIDE   (IDM_PERM_BASE + 11)

// 自定义消息：从工作线程向 UI 线程投递日志
#define WM_APP_LOG        (WM_APP + 1)
// 网络发现：扫描结果 / 进度 / 完成
#define WM_APP_SCAN_RESULT   (WM_APP + 10)
#define WM_APP_SCAN_PROGRESS (WM_APP + 11)
#define WM_APP_SCAN_DONE     (WM_APP + 12)

namespace {

// 滑动条取值范围（min/max/默认值）
struct PerfRange { int min; int max; int def; };

HINSTANCE g_hInst = nullptr;
HWND g_hWnd = nullptr;
HWND g_hLog = nullptr;
HWND g_hToggle = nullptr;
HWND g_hPort = nullptr;
HWND g_hPath = nullptr;
HWND g_hPasvStart = nullptr;
HWND g_hPasvEnd = nullptr;
HWND g_hIpLabel = nullptr;
HWND g_chkRead = nullptr;
HWND g_chkWrite = nullptr;
HWND g_chkDel = nullptr;
HWND g_chkMkdir = nullptr;
HWND g_chkRmdir = nullptr;
HWND g_chkRename = nullptr;
HWND g_chkList = nullptr;
HWND g_chkAll = nullptr;
HWND g_ipList = nullptr;
HWND g_ipEdit = nullptr;
HWND g_ipGroup = nullptr;      // “IP 限制”分组框
HWND g_viewToggle = nullptr;   // 高级模式 / 精简模式
HWND g_chkCompactRead = nullptr;   // 精简模式权限：读取
HWND g_chkCompactWrite = nullptr;  // 精简模式权限：写入
HWND g_chkCompactDelete = nullptr; // 精简模式权限：删除
HWND g_pasvLabel = nullptr;    // “被动端口:”标签
HWND g_pasvDash = nullptr;     // 被动端口中间的 “-”
HWND g_hTab = nullptr;
HWND g_pagePanel = nullptr;    // 性能/安全/其他设置三页的白色整页底板
// 分页内容区在父窗口中的矩形（判断控件属于哪一页时用）
int g_pageX = 0, g_pageY = 0, g_pageW = 0, g_pageH = 0;
HWND g_bmList = nullptr;
HWND g_advEnable = nullptr;
HWND g_advList = nullptr;     // 高级设置：ListView（报表模式）
HWND g_advHint = nullptr;
HWND g_permGroup = nullptr;   // 权限设置分组框（高级模式下禁用）
HIMAGELIST g_advImages = nullptr;  // 文件图标列表

// 性能设置页控件
HWND g_perfTimeout = nullptr;
HWND g_perfNoDelay = nullptr;
HWND g_perfSockBuf = nullptr;
HWND g_perfAppBuf = nullptr;
HWND g_perfMaxFile = nullptr;
HWND g_perfMaxConn = nullptr;
HWND g_perfHint = nullptr;
HWND g_perfTransferMode = nullptr;   // 传输模式下拉框
HWND g_perfLabels[6] = {nullptr};  // 性能页的标签控件
HWND g_perfResetBtn = nullptr;
HWND g_perfSaveBtn = nullptr;
HWND g_sldTimeout = nullptr;
HWND g_sldSockBuf = nullptr;
HWND g_sldAppBuf = nullptr;
HWND g_sldMaxFile = nullptr;
HWND g_sldMaxConn = nullptr;
// 防止滑动条与输入框互相更新时递归
bool g_perfSyncing = false;
// 性能页是否有未保存的修改
bool g_perfDirty = false;

// 安全提醒页控件
HWND g_secDelFile = nullptr;
HWND g_secDelDir = nullptr;
HWND g_secRename = nullptr;
HWND g_secOverwrite = nullptr;
HWND g_secDownload = nullptr;   // 下载弹窗确认（默认关闭）
HWND g_secRecycle = nullptr;    // 删除方式下拉框
HWND g_secIpMode = nullptr;     // IP 名单模式下拉框
HWND g_secTimeout = nullptr;
HWND g_secTimeoutLabel = nullptr;
HWND g_sldSecTimeout = nullptr;
HWND g_secRecycleHint = nullptr;
HWND g_secHint = nullptr;
HWND g_secSaveBtn = nullptr;
HWND g_secResetBtn = nullptr;   // 安全提醒：恢复默认（与“保存”并排靠右）
bool g_secDirty = false;
// 安全页超时滑动条范围
static const PerfRange kRangeSecTimeout = {5, 600, 30};

// 其他设置页控件
struct BindIpOption {
    std::string ip;
    std::string label;
};
HWND g_otherBindLabel = nullptr;
HWND g_otherEncodingLabel = nullptr;
HWND g_otherBind = nullptr;
HWND g_otherEncoding = nullptr;
HWND g_otherEncodingHint = nullptr;
HWND g_otherSaveBtn = nullptr;
HWND g_otherResetBtn = nullptr;
bool g_otherDirty = false;
std::vector<BindIpOption> g_bindIps;

// 其他设置页页脚：左边作者信息，右边 GitHub 图标 + “前往”链接
HWND g_otherAuthor = nullptr;
HWND g_ghIcon = nullptr;
HWND g_ghGo = nullptr;
HICON g_ghIconHandle = nullptr;   // 内嵌 PNG 生成的图标，由静态控件持有
HICON g_siteIconHandle = nullptr; // 网站图标的 HICON
HFONT g_ghLinkFont = nullptr;     // “前往”用的下划线字体
// “前往”打开的地址：改成自己的仓库地址只需改这一行
const wchar_t* kGitHubUrl = L"https://github.com/HLBQ/Simple-FTP-2";
// 第二个链接：个人网站
HWND g_siteIcon = nullptr;
HWND g_siteGo = nullptr;
const wchar_t* kSiteUrl = L"https://heilibaiqi.top/";

// 网络发现页控件
HWND g_discNetLabel = nullptr;
HWND g_discPortLabel = nullptr;
HWND g_discNet = nullptr;
HWND g_discPorts = nullptr;
HWND g_discScanBtn = nullptr;
HWND g_discList = nullptr;
HWND g_discStatus = nullptr;
struct DiscoveryEntry {
    std::string ip;
    int port = 0;
    std::string host;
};
std::vector<DiscoveryEntry> g_discEntries;
std::atomic<bool> g_scanRunning{false};
std::atomic<bool> g_scanStop{false};
HWND g_scanDlg = nullptr;
HWND g_scanBar = nullptr;
HWND g_scanInfo = nullptr;
HWND g_scanStopBtn = nullptr;
HFONT g_scanFont = nullptr;

std::unique_ptr<FtpServer> g_server;
std::string g_localIp;
AppConfig g_config;
std::string g_configPath;

// 日志缓冲（线程安全）
std::mutex g_logMutex;
std::string g_pendingLog;

// 客户端错误弹窗队列：后台可能同时报多个错误，用队列避免 MessageBox
// 在模态消息循环里被再次触发而层层嵌套。
std::deque<std::wstring> g_errorQueue;
bool g_errorPopupShowing = false;

// 日志分类过滤开关（默认全部启用）。关闭某类后，该类日志不再写入，
// 从而减少 UI 刷新与字符串处理，节约性能。
std::atomic<bool> g_logUpload{true};    // 上传日志
std::atomic<bool> g_logDownload{true};  // 下载日志
std::atomic<bool> g_logClient{true};    // 客户端日志
std::atomic<bool> g_logOther{true};     // 其他日志
std::atomic<bool> g_logWarn{true};      // 警告/错误日志

// 运行日志页顶部的分类过滤双态按钮（默认全部按下=启用）
HWND g_logFilterBtns[5] = {nullptr};

// ---- 客户端页 ----
HWND g_cliHost = nullptr;      // 服务器地址输入框
HWND g_cliPort = nullptr;      // 端口输入框
HWND g_cliHostLabel = nullptr; // "地址:" 标签
HWND g_cliPortLabel = nullptr; // "端口:" 标签
HWND g_cliList = nullptr;      // 文件列表（ListView）
HWND g_cliPath = nullptr;      // 当前路径显示
HWND g_cliUserEdit = nullptr;  // 用户名输入框
HWND g_cliPassEdit = nullptr;  // 密码输入框
HWND g_cliAuthCombo = nullptr; // 登录方式下拉框
HWND g_cliTopPanel = nullptr;  // 客户端顶部两行的白色底板
int g_cliTopY = 0, g_cliTopH = 0; // 白色底板的纵向范围
HWND g_cliUserLabel = nullptr; // "用户名:" 标签
HWND g_cliPassLabel = nullptr; // "密码:" 标签
std::unique_ptr<FtpClient> g_client;   // 内置 FTP 客户端
std::mutex g_clientMutex;              // 保护 g_client 的并发访问
std::vector<FtpClient::Entry> g_cliEntries;  // 当前目录项缓存
std::mutex g_cliEntriesMutex;                // 保护 g_cliEntries（后台线程写入、UI 线程读取）
std::string g_cliUser = "anonymous";   // 登录用户名
std::string g_cliPass = "";            // 登录密码

// ---- UTF-8 <-> UTF-16 转换 ----
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                  nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len,
                        nullptr, nullptr);
    return s;
}

// 把错误文本转换为宽字符（弹窗用）。
// 本程序自己的错误信息是 UTF-8；第三方 FTP 服务器可能返回 GBK，
// 直接按 UTF-8 转换会得到空串，所以失败时回退到本地编码。
std::wstring ErrorTextToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                  nullptr, 0);
    if (len > 0) {
        std::wstring w(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
        return w;
    }
    len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len > 0) {
        std::wstring w(len, L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &w[0], len);
        return w;
    }
    return L"";
}

std::string NowTime() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_s(&tmv, &t);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

// 把字节数格式化为带单位的可读字符串（自动换算 B/KB/MB/GB/TB/PB）。
// 规则：小于 1024 显示 B；否则逐级除以 1024，保留 1~2 位小数。
std::string FormatSize(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    }
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 5) {
        v /= 1024.0;
        ++u;
    }
    char buf[64];
    // 数值 >= 100 时保留 1 位小数，否则保留 2 位小数，更紧凑美观
    if (v >= 100.0)
        std::snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
    else
        std::snprintf(buf, sizeof(buf), "%.2f %s", v, units[u]);
    return buf;
}

// 判断一条日志属于哪一类，并检查对应过滤开关是否启用。
// 分类规则（按消息内容）：
//   上传：包含“上传文件”
//   下载：包含“下载文件”
//   客户端：包含“客户端”或“连接”相关（内置客户端操作）
//   警告：level>=1（警告/错误）
//   其他：其余信息类日志（含删除等）
// 返回 true 表示该日志应被记录（对应开关已启用）。
bool ShouldLog(int level, const std::string& msg) {
    // 错误始终记录：客户端操作失败不能因为“警告日志”被关掉而彻底消失
    if (level >= 2) return true;
    if (level == 1) return g_logWarn.load();
    if (msg.find("上传文件") != std::string::npos) return g_logUpload.load();
    if (msg.find("下载文件") != std::string::npos) return g_logDownload.load();
    // 客户端日志：内置客户端连接/浏览等操作
    if (msg.find("客户端") != std::string::npos ||
        msg.find("连接") != std::string::npos) return g_logClient.load();
    return g_logOther.load();
}

// 线程安全的日志回调：投递到 UI 线程
void LogCallbackFn(int level, const std::string& msg) {
    // 按分类过滤：关闭的类别直接丢弃，减少 UI 刷新与字符串处理
    if (!ShouldLog(level, msg)) return;
    std::string prefix = level == 2 ? "[错误] " : (level == 1 ? "[警告] " : "[信息] ");
    std::string line = NowTime() + " " + prefix + msg + "\r\n";
    {
        std::lock_guard<std::mutex> lk(g_logMutex);
        g_pendingLog += line;
    }
    if (g_hWnd) PostMessage(g_hWnd, WM_APP_LOG, 0, 0);
}

// 日志最多保留的行数：超过后自动删除最旧的行，避免 EDIT 控件内容
// 无限增长导致界面卡顿、影响 FTP 服务响应。
static const int kMaxLogLines = 100;

void AppendLog(const std::string& text) {
    std::wstring w = Utf8ToWide(text);
    int len = GetWindowTextLengthW(g_hLog);
    SendMessageW(g_hLog, EM_SETSEL, len, len);
    SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)w.c_str());

    // 限制日志行数：若超过上限，从头部删除多余的行
    int lineCount = (int)SendMessageW(g_hLog, EM_GETLINECOUNT, 0, 0);
    if (lineCount > kMaxLogLines) {
        int removeLines = lineCount - kMaxLogLines;
        // 找到第 removeLines 行的起始字符位置，删除其之前的所有内容
        int cutPos = (int)SendMessageW(g_hLog, EM_LINEINDEX, removeLines, 0);
        if (cutPos > 0) {
            SendMessageW(g_hLog, EM_SETSEL, 0, cutPos);
            SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)L"");
        }
    }

    // 自动滚动到最新日志
    SendMessageW(g_hLog, EM_SETSEL, GetWindowTextLengthW(g_hLog), GetWindowTextLengthW(g_hLog));
    SendMessageW(g_hLog, EM_SCROLLCARET, 0, 0);
}

void FlushPendingLog() {
    std::string data;
    {
        std::lock_guard<std::mutex> lk(g_logMutex);
        data.swap(g_pendingLog);
    }
    if (!data.empty()) AppendLog(data);
}

FtpPermissions ReadPermissions() {
    FtpPermissions p;
    p.read   = SendMessage(g_chkRead,   BM_GETCHECK, 0, 0) == BST_CHECKED;
    p.write  = SendMessage(g_chkWrite,  BM_GETCHECK, 0, 0) == BST_CHECKED;
    p.del    = SendMessage(g_chkDel,    BM_GETCHECK, 0, 0) == BST_CHECKED;
    p.mkdir  = SendMessage(g_chkMkdir,  BM_GETCHECK, 0, 0) == BST_CHECKED;
    p.rmdir  = SendMessage(g_chkRmdir,  BM_GETCHECK, 0, 0) == BST_CHECKED;
    p.rename = SendMessage(g_chkRename, BM_GETCHECK, 0, 0) == BST_CHECKED;
    p.list   = SendMessage(g_chkList,   BM_GETCHECK, 0, 0) == BST_CHECKED;
    return p;
}

// 精简模式的三类权限：读取、写入、删除。
// 三类都包含列目录；写入=上传/建目录/重命名，删除=删文件/删目录。
// 三项全勾时正好覆盖全部 7 项权限。
FtpPermissions ReadCompactPermissions() {
    FtpPermissions p;
    bool r = g_chkCompactRead &&
             SendMessage(g_chkCompactRead, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool w = g_chkCompactWrite &&
             SendMessage(g_chkCompactWrite, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool d = g_chkCompactDelete &&
             SendMessage(g_chkCompactDelete, BM_GETCHECK, 0, 0) == BST_CHECKED;
    p.read   = r;
    p.write  = w;
    p.mkdir  = w;
    p.rename = w;
    p.del    = d;
    p.rmdir  = d;
    p.list   = r || w || d;
    return p;
}

// 当前界面是否为精简模式（切换按钮弹起 = 精简）
bool IsCompactMode() {
    return g_viewToggle &&
           SendMessage(g_viewToggle, BM_GETCHECK, 0, 0) != BST_CHECKED;
}

// 把界面上的权限设置同步到正在运行的服务，立即生效（不必重启）。
// 修复：原先 Start() 只在启动时读取一次权限，服务运行中再勾“全部权限”、
// 启用高级模式或修改单文件规则都不会生效，客户端仍按旧权限被 550 拒绝，
// 表现就是“明明给了全部权限，往里放文件还是提示权限不够”。
void PushPermissionsToServer() {
    if (!g_server || !g_server->IsRunning()) return;

    bool compact = IsCompactMode();
    bool adv = !compact &&
               SendMessage(g_advEnable, BM_GETCHECK, 0, 0) == BST_CHECKED;
    FtpPermissions perms =
        compact ? ReadCompactPermissions() : ReadPermissions();
    if (adv) {
        // 与 StartServer 保持一致：高级模式下全局默认全部允许，
        // 具体限制由每个文件/文件夹的规则决定。
        perms = FtpPermissions();
        perms.read = perms.write = perms.del = perms.mkdir =
            perms.rmdir = perms.rename = perms.list = true;
    }

    std::vector<FilePermEntry> fpes;
    if (adv) {
        for (const auto& fp : g_config.filePerms) {
            FilePermEntry e;
            e.path = fp.path;
            e.perms = fp.perms;
            e.hidden = fp.hidden;
            fpes.push_back(e);
        }
    }
    g_server->UpdatePermissions(adv, perms, fpes);
}

std::string GetTextUtf8(HWND h) {
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return "";
    std::wstring w(len + 1, L'\0');   // 预留结尾 '\0'，避免越界写
    GetWindowTextW(h, &w[0], len + 1);
    w.resize(len);
    return WideToUtf8(w);
}

// 收集客户端列表中全部选中项（支持多选，文件/文件夹混合）
std::vector<FtpOps::RemoteItem> CollectSelectedItems() {
    std::vector<FtpOps::RemoteItem> items;
    std::lock_guard<std::mutex> lk(g_cliEntriesMutex);
    int sel = -1;
    while ((sel = (int)SendMessage(g_cliList, LVM_GETNEXTITEM, (WPARAM)sel,
                                   MAKELPARAM(LVNI_SELECTED, 0))) >= 0) {
        if (sel < (int)g_cliEntries.size()) {
            const auto& e = g_cliEntries[sel];
            items.push_back({e.name, e.isDir, e.size});
        }
    }
    return items;
}

// 把弹窗居中：优先主窗口中心；主窗口最小化/不可见时居中到屏幕工作区。
// 位置夹取在对应显示器的工作区内，防止多屏/小屏时移出屏幕。
void CenterOverOwner(HWND hD, HWND owner) {
    if (!hD) return;
    RECT dr;
    GetWindowRect(hD, &dr);
    int dw = dr.right - dr.left;
    int dh = dr.bottom - dr.top;

    bool ownerUsable = owner && IsWindow(owner) && IsWindowVisible(owner) &&
                       !IsIconic(owner);
    HMONITOR mon = nullptr;
    if (owner) {
        mon = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    } else {
        mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    }
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    RECT work{};
    if (mon && GetMonitorInfoW(mon, &mi)) {
        work = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }

    RECT area = work;   // 主窗口最小化/不可见：直接以屏幕工作区为中心
    if (ownerUsable) {
        GetWindowRect(owner, &area);
        // 主窗口可能部分在屏幕外，先夹取到所在显示器工作区
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
    SetWindowPos(hD, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// 主窗口最小化时返回 nullptr，避免 owned 对话框跟着主窗口一起被隐藏；
// 这样对话框会作为顶层窗口显示，并居中到屏幕。
HWND DialogOwner() {
    return (g_hWnd && IsWindow(g_hWnd) && !IsIconic(g_hWnd)) ? g_hWnd : nullptr;
}

// 客户端错误对话框：自定义窗口，便于精确控制居中位置，并提供
// 可滚动、可复制的错误详情（MessageBox 无法可靠地按主窗口居中）。
LRESULT CALLBACK ErrorDlgProc(HWND hD, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        const std::wstring* text =
            (const std::wstring*)((CREATESTRUCTW*)lp)->lpCreateParams;
        static HFONT f = nullptr;
        if (!f) {
            f = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                L"Microsoft YaHei UI");
        }
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            text ? text->c_str() : L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL,
            16, 16, 488, 150, hD, (HMENU)5001, nullptr, nullptr);
        SendMessage(edit, WM_SETFONT, (WPARAM)f, TRUE);
        HWND ok = CreateWindowW(L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            404, 180, 100, 32, hD, (HMENU)IDOK, nullptr, nullptr);
        SendMessage(ok, WM_SETFONT, (WPARAM)f, TRUE);
        SetFocus(ok);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            DestroyWindow(hD);
        }
        return 0;
    case DM_GETDEFID:
        // 让 IsDialogMessage 知道默认按钮，回车可以关闭
        return MAKELRESULT(IDOK, DC_HASDEFID);
    case WM_CLOSE:
        DestroyWindow(hD);
        return 0;
    }
    return DefWindowProcW(hD, m, wp, lp);
}

// 显示一个客户端错误框（UI 线程调用）。
// 主窗口可见时居中于主窗口；最小化时居中于屏幕工作区。
void ShowErrorDialog(const std::wstring& text) {
    static bool registered = false;
    const wchar_t* cls = L"FtpErrorDlg";
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ErrorDlgProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = cls;
        RegisterClassExW(&wc);
        registered = true;
    }

    // 主窗口最小化时不要以它为 owner，否则对话框可能被一起隐藏；
    // 改为无 owner 的顶层窗口，再居中到屏幕。
    HWND owner = DialogOwner();
    RECT rc = {0, 0, 520, 228};
    AdjustWindowRect(&rc, WS_CAPTION | WS_SYSMENU, FALSE);
    HWND hD = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, cls,
        L"FTP 客户端 - 操作失败",
        WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        owner, nullptr, g_hInst,
        (LPVOID)const_cast<std::wstring*>(&text));
    if (!hD) return;

    CenterOverOwner(hD, owner ? owner : g_hWnd);
    ShowWindow(hD, SW_SHOW);
    SetForegroundWindow(hD);

    bool disableOwner = g_hWnd && IsWindow(g_hWnd) && IsWindowEnabled(g_hWnd);
    if (disableOwner) EnableWindow(g_hWnd, FALSE);

    MSG msg;
    while (IsWindow(hD) && GetMessage(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessage(hD, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    if (disableOwner && IsWindow(g_hWnd)) EnableWindow(g_hWnd, TRUE);
    if (g_hWnd && IsWindow(g_hWnd)) SetForegroundWindow(g_hWnd);
}

// 收集 IP 白名单列表
std::vector<std::string> ReadIpList() {
    std::vector<std::string> ips;
    int count = (int)SendMessage(g_ipList, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; ++i) {
        int len = (int)SendMessage(g_ipList, LB_GETTEXTLEN, i, 0);
        std::wstring w(len, L'\0');
        SendMessageW(g_ipList, LB_GETTEXT, i, (LPARAM)&w[0]);
        ips.push_back(WideToUtf8(w));
    }
    return ips;
}

// 从性能设置页读取参数
PerfConfig ReadPerfFromUi() {
    PerfConfig p;
    p.dataTimeoutSec = atoi(GetTextUtf8(g_perfTimeout).c_str());
    p.tcpNoDelay = SendMessage(g_perfNoDelay, BM_GETCHECK, 0, 0) == BST_CHECKED;
    p.sockBufKB = atoi(GetTextUtf8(g_perfSockBuf).c_str());
    p.appBufKB = atoi(GetTextUtf8(g_perfAppBuf).c_str());
    p.maxFileSizeMB = atoi(GetTextUtf8(g_perfMaxFile).c_str());
    p.maxConnPerIp = atoi(GetTextUtf8(g_perfMaxConn).c_str());
    return p;
}

// 性能页各参数的取值范围（滑动条范围）
static const PerfRange kRangeTimeout = {0, 3600, 60};
static const PerfRange kRangeSockBuf = {0, 4096, 256};
static const PerfRange kRangeAppBuf  = {4, 1024, 64};
static const PerfRange kRangeMaxFile = {0, 10240, 0};
static const PerfRange kRangeMaxConn = {0, 500, 20};

// 设置滑动条范围与位置
void SetupSlider(HWND sld, const PerfRange& r, int val) {
    SendMessage(sld, TBM_SETRANGE, TRUE, MAKELPARAM(r.min, r.max));
    SendMessage(sld, TBM_SETPOS, TRUE, val);
}

// 把性能参数写入界面（滑动条 + 输入框），不触发保存
void ApplyPerfToUi(const PerfConfig& p) {
    g_perfSyncing = true;
    SetupSlider(g_sldTimeout, kRangeTimeout, p.dataTimeoutSec);
    SetupSlider(g_sldSockBuf, kRangeSockBuf, p.sockBufKB);
    SetupSlider(g_sldAppBuf,  kRangeAppBuf,  p.appBufKB);
    SetupSlider(g_sldMaxFile, kRangeMaxFile, p.maxFileSizeMB);
    SetupSlider(g_sldMaxConn, kRangeMaxConn, p.maxConnPerIp);
    SetWindowTextW(g_perfTimeout, Utf8ToWide(std::to_string(p.dataTimeoutSec)).c_str());
    SetWindowTextW(g_perfSockBuf, Utf8ToWide(std::to_string(p.sockBufKB)).c_str());
    SetWindowTextW(g_perfAppBuf,  Utf8ToWide(std::to_string(p.appBufKB)).c_str());
    SetWindowTextW(g_perfMaxFile, Utf8ToWide(std::to_string(p.maxFileSizeMB)).c_str());
    SetWindowTextW(g_perfMaxConn, Utf8ToWide(std::to_string(p.maxConnPerIp)).c_str());
    SendMessage(g_perfNoDelay, BM_SETCHECK,
                p.tcpNoDelay ? BST_CHECKED : BST_UNCHECKED, 0);
    if (g_perfTransferMode) {
        int tm = g_config.transferMode;
        if (tm < 0 || tm > 4) tm = 1;
        SendMessage(g_perfTransferMode, CB_SETCURSEL, (WPARAM)tm, 0);
    }
    g_perfSyncing = false;
}

// 标记性能页有未保存修改，并启用保存按钮
void MarkPerfDirty() {
    if (g_perfDirty) return;
    g_perfDirty = true;
    if (g_perfSaveBtn) EnableWindow(g_perfSaveBtn, TRUE);
}

// 校验界面上的性能参数（非法值恢复默认），返回规范化后的配置
PerfConfig NormalizePerfFromUi() {
    PerfConfig p;
    auto clamp = [](int v, const PerfRange& r) {
        if (v < r.min || v > r.max) return r.def;  // 非法/不合理 -> 默认值
        return v;
    };
    p.dataTimeoutSec = clamp(atoi(GetTextUtf8(g_perfTimeout).c_str()), kRangeTimeout);
    p.sockBufKB = clamp(atoi(GetTextUtf8(g_perfSockBuf).c_str()), kRangeSockBuf);
    p.appBufKB = clamp(atoi(GetTextUtf8(g_perfAppBuf).c_str()), kRangeAppBuf);
    p.maxFileSizeMB = clamp(atoi(GetTextUtf8(g_perfMaxFile).c_str()), kRangeMaxFile);
    p.maxConnPerIp = clamp(atoi(GetTextUtf8(g_perfMaxConn).c_str()), kRangeMaxConn);
    p.tcpNoDelay = SendMessage(g_perfNoDelay, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return p;
}

// 校验界面参数并标记为“未保存”（不写配置文件）
void CommitPerfFromUi() {
    if (g_perfSyncing) return;
    PerfConfig p = NormalizePerfFromUi();
    // 回写界面（把非法值纠正为默认值）
    ApplyPerfToUi(p);
    MarkPerfDirty();
}

// 点击“保存”：把性能参数写入配置文件，并清除未保存标记
void SavePerfToConfig() {
    // 先读取传输模式（必须在 ApplyPerfToUi 之前，否则会被旧值覆盖）
    if (g_perfTransferMode) {
        int sel = (int)SendMessage(g_perfTransferMode, CB_GETCURSEL, 0, 0);
        if (sel < 0) sel = 1;
        g_config.transferMode = sel;
    }
    PerfConfig p = NormalizePerfFromUi();
    ApplyPerfToUi(p);
    g_config.perf = p;
    g_config.Save(g_configPath);
    g_perfDirty = false;
    if (g_perfSaveBtn) EnableWindow(g_perfSaveBtn, FALSE);
}

// ---- 安全提醒页 ----

// 从安全提醒页读取配置
SecurityConfig ReadSecurityFromUi() {
    SecurityConfig s;
    s.confirmDeleteFile = SendMessage(g_secDelFile, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.confirmDeleteDir  = SendMessage(g_secDelDir,  BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.confirmRename     = SendMessage(g_secRename,   BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.confirmOverwrite  = SendMessage(g_secOverwrite,BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.confirmDownload   = SendMessage(g_secDownload, BM_GETCHECK, 0, 0) == BST_CHECKED;
    // 删除方式下拉框：0=彻底删除，1=移动到回收站
    s.deleteToRecycleBin = SendMessage(g_secRecycle, CB_GETCURSEL, 0, 0) == 1;
    s.confirmTimeoutSec = atoi(GetTextUtf8(g_secTimeout).c_str());
    if (s.confirmTimeoutSec < kRangeSecTimeout.min) s.confirmTimeoutSec = kRangeSecTimeout.min;
    if (s.confirmTimeoutSec > kRangeSecTimeout.max) s.confirmTimeoutSec = kRangeSecTimeout.max;
    // IP 名单模式下拉框：0=白名单，1=黑名单
    s.ipBlacklist = SendMessage(g_secIpMode, CB_GETCURSEL, 0, 0) == 1;
    return s;
}

// 把安全配置写入界面
void ApplySecurityToUi(const SecurityConfig& s) {
    SendMessage(g_secDelFile,  BM_SETCHECK, s.confirmDeleteFile ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_secDelDir,   BM_SETCHECK, s.confirmDeleteDir  ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_secRename,   BM_SETCHECK, s.confirmRename     ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_secOverwrite,BM_SETCHECK, s.confirmOverwrite  ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_secDownload, BM_SETCHECK, s.confirmDownload   ? BST_CHECKED : BST_UNCHECKED, 0);
    // 删除方式下拉框：0=彻底删除，1=移动到回收站
    SendMessage(g_secRecycle, CB_SETCURSEL, s.deleteToRecycleBin ? 1 : 0, 0);
    // IP 名单模式下拉框：0=白名单，1=黑名单
    SendMessage(g_secIpMode, CB_SETCURSEL, s.ipBlacklist ? 1 : 0, 0);
    SetupSlider(g_sldSecTimeout, kRangeSecTimeout, s.confirmTimeoutSec);
    SetWindowTextW(g_secTimeout, Utf8ToWide(std::to_string(s.confirmTimeoutSec)).c_str());
}

// 保存安全配置到文件
void SaveSecurityToConfig() {
    SecurityConfig s = ReadSecurityFromUi();
    ApplySecurityToUi(s);
    g_config.security = s;
    g_config.Save(g_configPath);
    g_secDirty = false;
    if (g_secSaveBtn) EnableWindow(g_secSaveBtn, FALSE);
}

// 敏感操作确认对话框的数据
struct ConfirmData {
    std::wstring text;       // 已按窗口宽度预换行的最终显示文本
    bool allow = false;
    bool done = false;
    int timeoutSec = 30;
    int remainSec = 30;      // 剩余秒数（用于拒绝按钮倒计时）
    int labelH = 90;         // 文本区高度（随换行结果变化）
    int btnY = 120;          // 按钮纵向位置
};

// 确认框字体（进程级缓存，建窗与换行测量共用）
void GetConfirmFonts(HFONT& normal, HFONT& bold) {
    static HFONT f = nullptr, fb = nullptr;
    if (!f) {
        f = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        fb = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    }
    normal = f;
    bold = fb;
}

// 判断字符是否适合作为换行机会
bool IsWrapOpportunity(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'/' || c == L'\\' ||
           c == L'-' || c == L'、' || c == L'，' || c == L'。' ||
           c == L'：' || c == L';';
}

// 按 maxWidth 把文本换行为多行：
//   先按 \r\n / \n 分段；段内优先在空格、斜杠、标点等合适位置折行；
//   无合适位置（长文件名/路径）时在溢出字符处硬折行；
//   中日韩文字本身可在任意字符间断行。
std::vector<std::wstring> WrapConfirmText(HDC hdc, const std::wstring& text,
                                          int maxWidth) {
    std::vector<std::wstring> lines;
    size_t i = 0, n = text.size();
    while (i < n) {
        size_t end = i;
        while (end < n && text[end] != L'\n') ++end;
        std::wstring para = text.substr(i, end - i);
        if (!para.empty() && para.back() == L'\r') para.pop_back();
        i = end + 1;

        if (para.empty()) { lines.push_back(L""); continue; }

        std::wstring line;
        int breakPos = -1;   // 行内最后一个换行机会下标
        for (size_t k = 0; k < para.size(); ++k) {
            wchar_t ch = para[k];
            std::wstring cand = line + ch;
            SIZE sz{};
            GetTextExtentPoint32W(hdc, cand.c_str(), (int)cand.size(), &sz);
            if (sz.cx <= maxWidth || line.empty()) {
                line = cand;
                if (IsWrapOpportunity(ch)) breakPos = (int)line.size() - 1;
            } else {
                if (breakPos >= 0) {
                    std::wstring rest = line.substr(breakPos + 1);
                    line = line.substr(0, breakPos + 1);
                    lines.push_back(line);
                    line = rest + ch;
                } else {
                    lines.push_back(line);
                    line.assign(1, ch);
                }
                breakPos = IsWrapOpportunity(ch) ? (int)line.size() - 1 : -1;
            }
        }
        lines.push_back(line);
    }
    return lines;
}

LRESULT CALLBACK ConfirmDlgProc(HWND hD, UINT m, WPARAM wp, LPARAM lp) {
    ConfirmData* cd = (ConfirmData*)GetWindowLongPtrW(hD, GWLP_USERDATA);
    switch (m) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        cd = (ConfirmData*)cs->lpCreateParams;
        SetWindowLongPtrW(hD, GWLP_USERDATA, (LONG_PTR)cd);
        HFONT f = nullptr, fb = nullptr;
        GetConfirmFonts(f, fb);
        // 多行文本（建窗前已按宽度预换行）：完整显示，高度仍不够才末行省略
        HWND st = CreateWindowW(L"STATIC", cd->text.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            15, 15, 400, cd->labelH, hD, (HMENU)IDC_CONFIRM_TEXT, nullptr, nullptr);
        SendMessage(st, WM_SETFONT, (WPARAM)f, TRUE);
        HWND b1 = CreateWindowW(L"BUTTON", L"允许", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            200, cd->btnY, 100, 32, hD, (HMENU)IDC_CONFIRM_ALLOW, nullptr, nullptr);
        // 拒绝按钮显示倒计时：拒绝 (30)
        wchar_t denyText[64];
        swprintf_s(denyText, L"拒绝 (%d)", cd->remainSec);
        HWND b2 = CreateWindowW(L"BUTTON", denyText, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            310, cd->btnY, 100, 32, hD, (HMENU)IDC_CONFIRM_DENY, nullptr, nullptr);
        SendMessage(b1, WM_SETFONT, (WPARAM)fb, TRUE);
        SendMessage(b2, WM_SETFONT, (WPARAM)f, TRUE);
        // 每秒刷新一次倒计时；到 0 视为拒绝
        SetTimer(hD, IDC_CONFIRM_TIMER, 1000, nullptr);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_CONFIRM_ALLOW) {
            cd->allow = true; cd->done = true;
            KillTimer(hD, IDC_CONFIRM_TIMER);
            DestroyWindow(hD);
        } else if (LOWORD(wp) == IDC_CONFIRM_DENY) {
            cd->allow = false; cd->done = true;
            KillTimer(hD, IDC_CONFIRM_TIMER);
            DestroyWindow(hD);
        }
        return 0;
    case WM_TIMER:
        if (wp == IDC_CONFIRM_TIMER) {
            cd->remainSec--;
            if (cd->remainSec <= 0) {
                cd->allow = false; cd->done = true;
                KillTimer(hD, IDC_CONFIRM_TIMER);
                DestroyWindow(hD);
            } else {
                wchar_t denyText[64];
                swprintf_s(denyText, L"拒绝 (%d)", cd->remainSec);
                SetDlgItemTextW(hD, IDC_CONFIRM_DENY, denyText);
            }
        }
        return 0;
    case WM_CLOSE:
        cd->allow = false; cd->done = true;
        KillTimer(hD, IDC_CONFIRM_TIMER);
        DestroyWindow(hD);
        return 0;
    }
    return DefWindowProcW(hD, m, wp, lp);
}

// 敏感操作确认回调（在客户端线程中调用）：
// 通过 PostMessage 让 UI 线程弹出模态对话框，并等待用户响应（带超时）。
// 由于客户端线程不能直接操作 UI，这里用自定义消息 + 事件同步。
// 修复（原实现为栈对象 + 全局裸指针，超时后 UI 线程仍在访问已销毁的
// 对象，造成 use-after-free；并发多个确认时会互相覆盖请求）：
//   请求对象用 shared_ptr 持有，经 lParam “信封”传给 UI 线程；
//   任一方先结束，另一方持有的副本仍能安全存活。
struct ConfirmRequest {
    std::wstring text;
    std::atomic<bool> allow{false};
    HANDLE doneEvent = nullptr;
    ~ConfirmRequest() { if (doneEvent) CloseHandle(doneEvent); }
};

// 自定义消息：请求 UI 线程弹出确认框（lParam = new shared_ptr<ConfirmRequest>)
#define WM_APP_CONFIRM (WM_APP + 2)

// UI 线程处理确认请求
void HandleConfirmRequest(const std::shared_ptr<ConfirmRequest>& req) {
    ConfirmData cd;
    cd.text = req->text;
    // 倒计时使用配置的超时值（并钳制到合理范围）
    int toSec = g_config.security.confirmTimeoutSec;
    if (toSec < 5) toSec = 5;
    if (toSec > 600) toSec = 600;
    cd.timeoutSec = toSec;
    cd.remainSec = toSec;

    static bool registered = false;
    const wchar_t* dlgCls = L"FtpConfirmDlg";
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ConfirmDlgProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = dlgCls;
        RegisterClassExW(&wc);
        registered = true;
    }

    // ---- 按文本内容计算弹窗尺寸：先测量换行，再决定窗口高度 ----
    HFONT f = nullptr, fb = nullptr;
    GetConfirmFonts(f, fb);
    const int kTextW = 400;
    const int kMargin = 15, kGap = 12, kBtnH = 32;
    int lineH = 20;
    std::vector<std::wstring> wrapped;
    HDC hdc = GetDC(nullptr);
    HGDIOBJ oldFont = SelectObject(hdc, f);
    wrapped = WrapConfirmText(hdc, cd.text, kTextW);
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    lineH = tm.tmHeight + tm.tmExternalLeading + 3;

    // 高度不能超出屏幕工作区
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int maxTextH = (work.bottom - work.top) - 80 -
                   (kMargin + kGap + kBtnH + kMargin);
    int maxLines = maxTextH / lineH;
    if (maxLines < 1) maxLines = 1;

    std::wstring display;
    int usedLines = (int)wrapped.size();
    if (usedLines <= maxLines) {
        // 全部能放下：逐行拼接
        for (const auto& l : wrapped) { display += l; display += L"\r\n"; }
        if (!display.empty()) display.resize(display.size() - 2);
    } else {
        // 换行后仍放不下：前面整行显示，最后一行截断加省略号
        usedLines = maxLines;
        for (int i = 0; i < maxLines - 1; ++i) {
            display += wrapped[i];
            display += L"\r\n";
        }
        std::wstring last = wrapped[maxLines - 1];
        for (;;) {
            std::wstring cand = last + L"…";
            SIZE sz{};
            GetTextExtentPoint32W(hdc, cand.c_str(), (int)cand.size(), &sz);
            if (sz.cx <= kTextW || last.empty()) { last += L"…"; break; }
            last.pop_back();
        }
        display += last;
    }
    SelectObject(hdc, oldFont);
    ReleaseDC(nullptr, hdc);

    cd.text = display;
    cd.labelH = usedLines * lineH;
    cd.btnY = kMargin + cd.labelH + kGap;
    int clientH = cd.btnY + kBtnH + kMargin;

    RECT rc = {0, 0, 430, clientH};
    AdjustWindowRect(&rc, WS_CAPTION | WS_SYSMENU, FALSE);
    HWND hD = CreateWindowExW(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, dlgCls,
        L"敏感操作确认", WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        DialogOwner(), nullptr, g_hInst, &cd);

    // 居中到主窗口
    CenterOverOwner(hD, g_hWnd);
    SetForegroundWindow(hD);

    // 模态消息循环
    MSG msg;
    while (IsWindow(hD) && GetMessage(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessage(hD, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    req->allow.store(cd.allow);
}

// 确认回调实现：供 FtpServer 调用（运行在客户端线程）
bool ConfirmCallbackFn(const std::string& action, const std::string& target,
                       const std::string& clientIp) {
    auto req = std::make_shared<ConfirmRequest>();
    req->text = L"客户端 " + Utf8ToWide(clientIp) + L" 请求执行敏感操作：\r\n\r\n" +
                L"操作：" + Utf8ToWide(action) + L"\r\n" +
                L"目标：" + Utf8ToWide(target) + L"\r\n\r\n" +
                L"是否允许？（超时将自动拒绝）";
    req->doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!req->doneEvent) return false;

    // 信封：UI 线程接管一份 shared_ptr 后即删除信封
    auto* holder = new std::shared_ptr<ConfirmRequest>(req);
    if (!g_hWnd || !PostMessage(g_hWnd, WM_APP_CONFIRM, 0, (LPARAM)holder)) {
        delete holder;   // 投递失败：信封与请求对象一并销毁
        return false;
    }

    // 等待 UI 线程处理完成（带超时，超时视为拒绝）
    int toSec = g_config.security.confirmTimeoutSec;
    if (toSec < 5) toSec = 5;
    if (toSec > 600) toSec = 600;
    DWORD timeoutMs = (DWORD)toSec * 1000 + 5000;
    WaitForSingleObject(req->doneEvent, timeoutMs);
    return req->allow.load();
}

// 输入框内容变化时，实时把数值同步到对应滑动条（不触发保存）
void SyncSliderFromEdit(int ctrlId) {
    if (g_perfSyncing) return;
    HWND edit = nullptr, sld = nullptr;
    const PerfRange* r = nullptr;
    switch (ctrlId) {
    case IDC_PERF_TIMEOUT: edit = g_perfTimeout; sld = g_sldTimeout; r = &kRangeTimeout; break;
    case IDC_PERF_SOCKBUF: edit = g_perfSockBuf; sld = g_sldSockBuf; r = &kRangeSockBuf; break;
    case IDC_PERF_APPBUF:  edit = g_perfAppBuf;  sld = g_sldAppBuf;  r = &kRangeAppBuf;  break;
    case IDC_PERF_MAXFILE: edit = g_perfMaxFile; sld = g_sldMaxFile; r = &kRangeMaxFile; break;
    case IDC_PERF_MAXCONN: edit = g_perfMaxConn; sld = g_sldMaxConn; r = &kRangeMaxConn; break;
    default: return;
    }
    int v = atoi(GetTextUtf8(edit).c_str());
    if (v < r->min) v = r->min;
    if (v > r->max) v = r->max;
    SendMessage(sld, TBM_SETPOS, TRUE, v);
}

// 把当前界面设置写回 g_config 并保存
// ---- 其他设置页：绑定网卡与文件名编码 ----
void SelectBindIp(const std::string& ip);

std::vector<BindIpOption> EnumBindIps() {
    std::vector<BindIpOption> out;
    auto add = [&](const std::string& ip, const std::string& label) {
        for (const auto& e : out) {
            if (e.ip == ip) return;
        }
        out.push_back({ip, label});
    };

    add("0.0.0.0", "全部网卡（推荐）");
    add("127.0.0.1", "仅本机（127.0.0.1）");

    ULONG size = 0;
    DWORD ret = GetAdaptersAddresses(
        AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                     GAA_FLAG_SKIP_DNS_SERVER,
        nullptr, nullptr, &size);
    if (ret == ERROR_BUFFER_OVERFLOW && size > 0) {
        std::vector<BYTE> buf(size);
        auto* addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
        ret = GetAdaptersAddresses(
            AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                         GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, addrs, &size);
        if (ret == NO_ERROR) {
            for (auto* a = addrs; a; a = a->Next) {
                if (a->OperStatus != IfOperStatusUp) continue;
                std::string name = "网卡";
                if (a->FriendlyName && a->FriendlyName[0])
                    name = WideToUtf8(a->FriendlyName);
                for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
                    if (!u->Address.lpSockaddr ||
                        u->Address.lpSockaddr->sa_family != AF_INET)
                        continue;
                    auto* sin = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
                    char ip[INET_ADDRSTRLEN] = {};
                    if (!inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip)))
                        continue;
                    add(ip, name + "  (" + ip + ")");
                }
            }
        }
    }
    return out;
}

void PopulateBindCombo() {
    if (!g_otherBind) return;
    g_bindIps = EnumBindIps();
    SendMessageW(g_otherBind, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_bindIps.size(); ++i) {
        int idx = (int)SendMessageW(g_otherBind, CB_ADDSTRING, 0,
            (LPARAM)Utf8ToWide(g_bindIps[i].label).c_str());
        if (idx != CB_ERR) SendMessageW(g_otherBind, CB_SETITEMDATA, idx, (LPARAM)i);
    }
    SelectBindIp(g_config.bindIp);
}

void SelectBindIp(const std::string& ip) {
    if (!g_otherBind) return;
    std::string want = ip.empty() ? "0.0.0.0" : ip;
    int found = -1;
    for (size_t i = 0; i < g_bindIps.size(); ++i) {
        if (g_bindIps[i].ip == want) { found = (int)i; break; }
    }
    if (found < 0) {
        int idx = (int)SendMessageW(g_otherBind, CB_ADDSTRING, 0,
            (LPARAM)Utf8ToWide(want + "  (当前配置，可能已断开)").c_str());
        if (idx != CB_ERR) {
            SendMessageW(g_otherBind, CB_SETITEMDATA, idx, (LPARAM)g_bindIps.size());
            g_bindIps.push_back({want, want});
            found = (int)g_bindIps.size() - 1;
        }
    }
    SendMessageW(g_otherBind, CB_SETCURSEL, found < 0 ? 0 : found, 0);
}

std::string GetSelectedBindIp() {
    if (!g_otherBind) return g_config.bindIp.empty() ? "0.0.0.0" : g_config.bindIp;
    int sel = (int)SendMessageW(g_otherBind, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return "0.0.0.0";
    int idx = (int)SendMessageW(g_otherBind, CB_GETITEMDATA, sel, 0);
    if (idx < 0 || idx >= (int)g_bindIps.size()) return "0.0.0.0";
    return g_bindIps[idx].ip;
}

int GetSelectedEncoding() {
    if (!g_otherEncoding) return g_config.fileNameEncoding;
    int sel = (int)SendMessageW(g_otherEncoding, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel > 4) return 0;
    return sel;
}

void UpdateOtherSaveState() {
    if (g_otherSaveBtn) EnableWindow(g_otherSaveBtn, g_otherDirty);
}

void MarkOtherDirty() {
    g_otherDirty = true;
    UpdateOtherSaveState();
}

void OnOtherBindChanged() {
    // 只标记为有改动；点“保存”时才写配置/应用。
    MarkOtherDirty();
}

void OnOtherEncodingChanged() {
    MarkOtherDirty();
}

void OnOtherSave() {
    g_config.bindIp = GetSelectedBindIp();
    g_config.fileNameEncoding = GetSelectedEncoding();
    g_config.Save(g_configPath);
    if (g_server && g_server->IsRunning())
        g_server->SetFileNameEncoding(g_config.fileNameEncoding);
    g_otherDirty = false;
    UpdateOtherSaveState();
}

void OnOtherReset() {
    // 恢复默认只改界面；仍需点“保存”才写入配置。
    if (g_otherBind) SelectBindIp("0.0.0.0");
    if (g_otherEncoding) SendMessageW(g_otherEncoding, CB_SETCURSEL, 0, 0);
    MarkOtherDirty();
}

void SaveConfigFromUi() {
    g_config.port = atoi(GetTextUtf8(g_hPort).c_str());
    g_config.sharePath = GetTextUtf8(g_hPath);
    g_config.pasvStart = atoi(GetTextUtf8(g_hPasvStart).c_str());
    g_config.pasvEnd = atoi(GetTextUtf8(g_hPasvEnd).c_str());
    g_config.perms = ReadPermissions();
    g_config.allowedIps = ReadIpList();
    g_config.advancedMode =
        SendMessage(g_advEnable, BM_GETCHECK, 0, 0) == BST_CHECKED;
    // 界面模式：按下 = 高级模式，弹起 = 精简模式
    if (g_viewToggle) {
        g_config.compactMode =
            SendMessage(g_viewToggle, BM_GETCHECK, 0, 0) != BST_CHECKED;
    }
    // 精简模式的三类权限与高级模式的七项权限分别保存
    if (g_chkCompactRead) {
        g_config.compactPermRead =
            SendMessage(g_chkCompactRead, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    if (g_chkCompactWrite) {
        g_config.compactPermWrite =
            SendMessage(g_chkCompactWrite, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    if (g_chkCompactDelete) {
        g_config.compactPermDelete =
            SendMessage(g_chkCompactDelete, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    g_config.perf = ReadPerfFromUi();
    g_config.security = ReadSecurityFromUi();
    // 其他设置页：绑定网卡与默认文件名编码
    if (g_otherBind) g_config.bindIp = GetSelectedBindIp();
    if (g_otherEncoding) g_config.fileNameEncoding = GetSelectedEncoding();
    // 客户端页：保存上次连接的地址与端口
    if (g_cliHost) g_config.cliHost = GetTextUtf8(g_cliHost);
    if (g_cliPort) g_config.cliPort = atoi(GetTextUtf8(g_cliPort).c_str());
    // 客户端页：记住登录凭据（下次启动直接可用；匿名模式不保存账号密码）
    if (g_cliAuthCombo) {
        g_config.cliAnonymous =
            SendMessageW(g_cliAuthCombo, CB_GETCURSEL, 0, 0) == 0;
    }
    if (g_config.cliAnonymous) {
        g_config.cliUser = "anonymous";
        g_config.cliPass.clear();
    } else {
        if (g_cliUserEdit) g_config.cliUser = GetTextUtf8(g_cliUserEdit);
        if (g_cliPassEdit) g_config.cliPass = GetTextUtf8(g_cliPassEdit);
    }
    // 运行日志页：保存分类过滤开关状态
    g_config.logFilterUpload   = g_logUpload.load();
    g_config.logFilterDownload = g_logDownload.load();
    g_config.logFilterClient   = g_logClient.load();
    g_config.logFilterOther    = g_logOther.load();
    g_config.logFilterWarn     = g_logWarn.load();
    g_config.Save(g_configPath);
}

void StartServer() {
    if (g_server && g_server->IsRunning()) return;

    int port = atoi(GetTextUtf8(g_hPort).c_str());
    if (port < 1 || port > 65535) {
        MessageBoxW(g_hWnd, L"端口号必须在 1-65535 之间", L"错误", MB_ICONERROR);
        return;
    }
    std::string path = GetTextUtf8(g_hPath);
    DWORD attr = GetFileAttributesW(Utf8ToWide(path).c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        MessageBoxW(g_hWnd, L"共享路径无效", L"错误", MB_ICONERROR);
        return;
    }
    int ps = atoi(GetTextUtf8(g_hPasvStart).c_str());
    int pe = atoi(GetTextUtf8(g_hPasvEnd).c_str());
    if (ps < 1 || pe > 65535 || ps > pe) {
        MessageBoxW(g_hWnd, L"被动端口范围无效", L"错误", MB_ICONERROR);
        return;
    }

    bool compact = IsCompactMode();
    bool adv = !compact &&
               SendMessage(g_advEnable, BM_GETCHECK, 0, 0) == BST_CHECKED;
    FtpPermissions perms =
        compact ? ReadCompactPermissions() : ReadPermissions();
    if (adv) {
        // 高级模式：全局默认全部权限，具体由文件规则控制
        perms = FtpPermissions();
        perms.read = perms.write = perms.del = perms.mkdir =
            perms.rmdir = perms.rename = perms.list = true;
    }
    std::vector<std::string> ips = ReadIpList();

    // 组装文件权限规则
    std::vector<FilePermEntry> fpes;
    if (adv) {
        for (const auto& fp : g_config.filePerms) {
            FilePermEntry e;
            e.path = fp.path;
            e.perms = fp.perms;
            e.hidden = fp.hidden;   // 必须传递隐藏标记，否则服务端无法隐藏
            fpes.push_back(e);
        }
    }

    // 性能参数
    PerfConfig pc = ReadPerfFromUi();
    PerfOptions po;
    po.dataTimeoutSec = pc.dataTimeoutSec;
    po.tcpNoDelay = pc.tcpNoDelay;
    po.sockBufKB = pc.sockBufKB;
    po.appBufKB = pc.appBufKB;
    po.maxFileSizeMB = pc.maxFileSizeMB;
    po.maxConnPerIp = pc.maxConnPerIp;

    // 安全提醒参数
    SecurityConfig sc = ReadSecurityFromUi();
    SecurityOptions so;
    so.confirmDeleteFile = sc.confirmDeleteFile;
    so.confirmDeleteDir  = sc.confirmDeleteDir;
    so.confirmRename     = sc.confirmRename;
    so.confirmOverwrite  = sc.confirmOverwrite;
    so.confirmDownload   = sc.confirmDownload;
    so.deleteToRecycleBin= sc.deleteToRecycleBin;
    so.confirmTimeoutSec = sc.confirmTimeoutSec;
    so.ipBlacklist       = sc.ipBlacklist;

    if (!g_server) g_server = std::make_unique<FtpServer>();
    // 绑定网卡：默认 0.0.0.0（所有网卡）。绑定 0.0.0.0 时，PASV 会按
    // 每条控制连接的实际本地地址（getsockname）通告可达 IP，TUN 和多网卡
    // 环境不会拐错；指定具体网卡时则只在该地址上监听。
    std::string bindIp = g_otherBind ? GetSelectedBindIp() : g_config.bindIp;
    int encMode = g_otherEncoding ? GetSelectedEncoding() : g_config.fileNameEncoding;
    bool ok = g_server->Start(bindIp, (uint16_t)port, path, perms,
                              (uint16_t)ps, (uint16_t)pe, ips, LogCallbackFn,
                              adv, fpes, po, so, ConfirmCallbackFn, encMode);
    if (ok) {
        SetWindowTextW(g_hToggle, L"停止服务");
        SaveConfigFromUi();
    }
}

#define WM_APP_STOP_DONE (WM_APP + 7)   // 后台停止服务完成

// 异步停止保护：Stop() 要等所有会话线程退出，大文件传输中可能需要几秒；
// 同步等待会让 UI 未响应（实测传输中途停止服务卡死），因此放到后台线程。
// 退出路径用 StopServerSync 等待完成，防止 g_server 析构后后台线程 UAF。
std::atomic<bool> g_serverStopping{false};
HANDLE g_stopDoneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

void NotifyStopDone() {
    g_serverStopping.store(false);
    SetWindowTextW(g_hToggle, L"启动服务");
}

void StopServer() {
    if (!g_server || !g_server->IsRunning()) {
        NotifyStopDone();
        return;
    }
    bool expect = false;
    if (!g_serverStopping.compare_exchange_strong(expect, true)) return;  // 已在停止
    SetWindowTextW(g_hToggle, L"正在停止...");
    ResetEvent(g_stopDoneEvent);
    std::thread([] {
        g_server->Stop();
        if (g_stopDoneEvent) SetEvent(g_stopDoneEvent);
        // 按钮文字必须回 UI 线程改
        PostMessage(g_hWnd, WM_APP_STOP_DONE, 0, 0);
    }).detach();
}

void StopServerSync() {
    if (!g_server) return;
    if (g_serverStopping.load()) {
        if (g_stopDoneEvent) WaitForSingleObject(g_stopDoneEvent, 10000);
        NotifyStopDone();
        return;
    }
    if (g_server->IsRunning()) g_server->Stop();
    SetWindowTextW(g_hToggle, L"启动服务");
}

void ToggleServer() {
    if (g_serverStopping.load()) return;   // 停止进行中，忽略点击（防止并发 Start）
    if (g_server && g_server->IsRunning()) {
        StopServer();
    } else {
        StartServer();
    }
}

// Common File Dialog coclass：{DC1C5A9C-E88A-4DDE-A5A1-60F82A20AEF7}
// （SDK 头文件未导出 FileDialog coclass 的 C++ 声明，手动定义 GUID）
static const CLSID kCLSID_FileDialog =
    {0xDC1C5A9C, 0xE88A, 0x4DDE, {0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7}};

// 现代文件夹选择器（Vista+ IFileDialog，替换旧版 SHBrowseForFolder 树形对话框）。
// 成功返回 UTF-8 路径，取消/失败返回空串。
// COM 需处于 STA（RunGui 里的 OleInitialize 已保证）。
std::string PickFolderUtf8(HWND owner, const wchar_t* title) {
    std::string result;
    IFileDialog* fd = nullptr;
    HRESULT hr = CoCreateInstance(kCLSID_FileDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof(IFileDialog), (void**)&fd);
    if (SUCCEEDED(hr) && fd) {
        DWORD opts = 0;
        if (SUCCEEDED(fd->GetOptions(&opts)))
            fd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (title) fd->SetTitle(title);
        if (SUCCEEDED(fd->Show(owner))) {
            IShellItem* si = nullptr;
            if (SUCCEEDED(fd->GetResult(&si)) && si) {
                PWSTR path = nullptr;
                if (SUCCEEDED(si->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    result = WideToUtf8(path);
                    CoTaskMemFree(path);
                }
                si->Release();
            }
        }
        fd->Release();
    }
    return result;
}

void BrowseFolder() {
    std::string path = PickFolderUtf8(g_hWnd, L"选择共享文件夹");
    if (!path.empty())
        SetWindowTextW(g_hPath, Utf8ToWide(path).c_str());
}

// 全部权限勾选框：勾选=全选，取消=只留读取+列目录
void OnCheckAll() {
    bool checked = SendMessage(g_chkAll, BM_GETCHECK, 0, 0) == BST_CHECKED;
    HWND boxes[] = {g_chkRead, g_chkWrite, g_chkDel, g_chkMkdir,
                    g_chkRmdir, g_chkRename, g_chkList};
    for (HWND b : boxes) {
        SendMessage(b, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (!checked) {
        // 取消全部权限时，保留读取和列目录
        SendMessage(g_chkRead, BM_SETCHECK, BST_CHECKED, 0);
        SendMessage(g_chkList, BM_SETCHECK, BST_CHECKED, 0);
    }
    PushPermissionsToServer();
}

// 单个权限勾选框变化时的联动：
// 若 7 项全部勾选，则自动勾选“全部权限”；否则取消“全部权限”。
void OnPermItemChanged() {
    HWND boxes[] = {g_chkRead, g_chkWrite, g_chkDel, g_chkMkdir,
                    g_chkRmdir, g_chkRename, g_chkList};
    bool all = true;
    for (HWND b : boxes) {
        if (SendMessage(b, BM_GETCHECK, 0, 0) != BST_CHECKED) { all = false; break; }
    }
    SendMessage(g_chkAll, BM_SETCHECK, all ? BST_CHECKED : BST_UNCHECKED, 0);
    PushPermissionsToServer();
}

// 添加 IP 到白名单
void AddIp() {
    std::string ip = GetTextUtf8(g_ipEdit);
    if (ip.empty()) return;
    for (char c : ip) {
        if (!isdigit((unsigned char)c) && c != '.') {
            MessageBoxW(g_hWnd, L"IP 格式无效", L"错误", MB_ICONWARNING);
            return;
        }
    }
    std::wstring w = Utf8ToWide(ip);
    int count = (int)SendMessage(g_ipList, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; ++i) {
        int len = (int)SendMessage(g_ipList, LB_GETTEXTLEN, i, 0);
        std::wstring cur(len, L'\0');
        SendMessageW(g_ipList, LB_GETTEXT, i, (LPARAM)&cur[0]);
        if (cur == w) return;
    }
    SendMessageW(g_ipList, LB_ADDSTRING, 0, (LPARAM)w.c_str());
    SetWindowTextW(g_ipEdit, L"");
}

void DelIp() {
    int sel = (int)SendMessage(g_ipList, LB_GETCURSEL, 0, 0);
    if (sel != LB_ERR) {
        SendMessage(g_ipList, LB_DELETESTRING, sel, 0);
    }
}

// ---- 快捷方式连接本 ----

void RefreshBookmarkList() {
    SendMessage(g_bmList, LVM_DELETEALLITEMS, 0, 0);
    int row = 0;
    for (const auto& bm : g_config.bookmarks) {
        std::wstring wname = Utf8ToWide(bm.name);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.iSubItem = 0;
        item.pszText = (LPWSTR)wname.c_str();
        int idx = (int)SendMessageW(g_bmList, LVM_INSERTITEMW, 0, (LPARAM)&item);

        std::string addr = bm.host + ":" + std::to_string(bm.port);
        std::wstring waddr = Utf8ToWide(addr);
        LVITEMW sub{};
        sub.mask = LVIF_TEXT;
        sub.iItem = idx;
        sub.iSubItem = 1;
        sub.pszText = (LPWSTR)waddr.c_str();
        SendMessageW(g_bmList, LVM_SETITEMW, 0, (LPARAM)&sub);

        // 状态列：初始为空
        std::wstring wstatus = L"";
        LVITEMW sub2{};
        sub2.mask = LVIF_TEXT;
        sub2.iItem = idx;
        sub2.iSubItem = 2;
        sub2.pszText = (LPWSTR)wstatus.c_str();
        SendMessageW(g_bmList, LVM_SETITEMW, 0, (LPARAM)&sub2);
        ++row;
    }
}

// 设置某行的状态列文字与颜色（color: 0=默认 1=绿 2=红）
void SetBookmarkStatus(int row, const std::wstring& text, int color) {
    LVITEMW sub{};
    sub.mask = LVIF_TEXT;
    sub.iItem = row;
    sub.iSubItem = 2;
    sub.pszText = (LPWSTR)text.c_str();
    SendMessageW(g_bmList, LVM_SETITEMW, 0, (LPARAM)&sub);
    // 通过自定义绘制实现彩色文字
    ListView_RedrawItems(g_bmList, row, row);
}


// 获取快捷方式列表中选中的行号（无选中返回 -1）
int GetSelectedBookmark() {
    return (int)SendMessage(g_bmList, LVM_GETNEXTITEM, (WPARAM)-1,
                            MAKELPARAM(LVNI_SELECTED, 0));
}

struct DlgData {
    std::wstring name, host, user, pass, port;
    bool ok = false;
};

LRESULT CALLBACK BookmarkDlgProc(HWND hD, UINT m, WPARAM wp, LPARAM lp) {
    DlgData* dd = (DlgData*)GetWindowLongPtrW(hD, GWLP_USERDATA);
    switch (m) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        dd = (DlgData*)cs->lpCreateParams;
        SetWindowLongPtrW(hD, GWLP_USERDATA, (LONG_PTR)dd);
        // 字体进程级缓存：书签框可反复创建，避免累积 GDI 泄漏
        static HFONT f = nullptr;
        if (!f) {
            f = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        }
        const wchar_t* labels[] = {L"名称:", L"地址:", L"端口:", L"用户名:", L"密码:"};
        int ys[] = {15, 45, 75, 105, 135};
        for (int i = 0; i < 5; ++i) {
            HWND st = CreateWindowW(L"STATIC", labels[i], WS_CHILD | WS_VISIBLE,
                                    15, ys[i] + 3, 60, 20, hD, nullptr, nullptr, nullptr);
            SendMessage(st, WM_SETFONT, (WPARAM)f, TRUE);
        }
        HWND e1 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 80, ys[0], 200, 24, hD, (HMENU)2001, nullptr, nullptr);
        HWND e2 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 80, ys[1], 200, 24, hD, (HMENU)2002, nullptr, nullptr);
        HWND e3 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"21",
            WS_CHILD | WS_VISIBLE | ES_NUMBER, 80, ys[2], 200, 24, hD, (HMENU)2003, nullptr, nullptr);
        HWND e4 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"anonymous",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 80, ys[3], 200, 24, hD, (HMENU)2004, nullptr, nullptr);
        HWND e5 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD, 80, ys[4], 200, 24, hD, (HMENU)2005, nullptr, nullptr);
        HWND b1 = CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            80, 175, 90, 30, hD, (HMENU)IDOK, nullptr, nullptr);
        HWND b2 = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            190, 175, 90, 30, hD, (HMENU)IDCANCEL, nullptr, nullptr);
        HWND all[] = {e1, e2, e3, e4, e5, b1, b2};
        for (HWND h : all) SendMessage(h, WM_SETFONT, (WPARAM)f, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            auto get = [&](int id) -> std::wstring {
                HWND h = GetDlgItem(hD, id);
                int len = GetWindowTextLengthW(h);
                if (len <= 0) return L"";
                std::wstring w(len + 1, L'\0');   // 预留结尾 '\0'，避免越界
                GetWindowTextW(h, &w[0], len + 1);
                w.resize(len);
                return w;
            };
            dd->name = get(2001);
            dd->host = get(2002);
            dd->port = get(2003);
            dd->user = get(2004);
            dd->pass = get(2005);
            dd->ok = true;
            DestroyWindow(hD);
        } else if (LOWORD(wp) == IDCANCEL) {
            DestroyWindow(hD);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hD);
        return 0;
    }
    return DefWindowProcW(hD, m, wp, lp);
}

// 显示连接编辑对话框；editIndex >= 0 表示编辑已有条目，-1 表示新增
// 返回 true 表示用户确认
bool ShowBookmarkDialog(int editIndex) {
    DlgData data;
    data.port = L"21";
    if (editIndex >= 0 && editIndex < (int)g_config.bookmarks.size()) {
        const FtpBookmark& bm = g_config.bookmarks[editIndex];
        data.name = Utf8ToWide(bm.name);
        data.host = Utf8ToWide(bm.host);
        data.port = Utf8ToWide(std::to_string(bm.port));
        data.user = Utf8ToWide(bm.user);
        data.pass = Utf8ToWide(bm.pass);
    }

    static bool registered = false;
    const wchar_t* dlgCls = L"FtpBookmarkDlg";
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = BookmarkDlgProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = dlgCls;
        RegisterClassExW(&wc);
        registered = true;
    }

    RECT rc = {0, 0, 310, 220};
    AdjustWindowRect(&rc, WS_CAPTION | WS_SYSMENU, FALSE);
    HWND hD = CreateWindowExW(WS_EX_DLGMODALFRAME, dlgCls,
        editIndex >= 0 ? L"编辑 FTP 连接" : L"添加 FTP 连接",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        DialogOwner(), nullptr, g_hInst, &data);

    // 预填已有值
    if (editIndex >= 0) {
        SetDlgItemTextW(hD, 2001, data.name.c_str());
        SetDlgItemTextW(hD, 2002, data.host.c_str());
        SetDlgItemTextW(hD, 2003, data.port.c_str());
        SetDlgItemTextW(hD, 2004, data.user.c_str());
        SetDlgItemTextW(hD, 2005, data.pass.c_str());
    }

    // 居中到主窗口
    CenterOverOwner(hD, g_hWnd);
    EnableWindow(g_hWnd, FALSE);
    MSG msg;
    while (IsWindow(hD) && GetMessage(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessage(hD, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    EnableWindow(g_hWnd, TRUE);
    SetForegroundWindow(g_hWnd);

    if (!data.ok || data.host.empty()) return false;

    FtpBookmark bm;
    bm.name = WideToUtf8(data.name);
    bm.host = WideToUtf8(data.host);
    bm.port = atoi(WideToUtf8(data.port).c_str());
    bm.user = WideToUtf8(data.user);
    bm.pass = WideToUtf8(data.pass);
    if (bm.name.empty()) bm.name = bm.host;

    if (editIndex >= 0 && editIndex < (int)g_config.bookmarks.size()) {
        g_config.bookmarks[editIndex] = bm;
    } else {
        g_config.bookmarks.push_back(bm);
    }
    g_config.Save(g_configPath);
    RefreshBookmarkList();
    return true;
}

void AddBookmarkDialog() {
    ShowBookmarkDialog(-1);
}

// 编辑选中的连接
void EditBookmark() {
    int sel = GetSelectedBookmark();
    if (sel < 0 || sel >= (int)g_config.bookmarks.size()) {
        MessageBoxW(g_hWnd, L"请先选择一个连接", L"提示", MB_ICONINFORMATION);
        return;
    }
    ShowBookmarkDialog(sel);
}

void DelBookmark() {
    int sel = GetSelectedBookmark();
    if (sel >= 0 && sel < (int)g_config.bookmarks.size()) {
        g_config.bookmarks.erase(g_config.bookmarks.begin() + sel);
        g_config.Save(g_configPath);
        RefreshBookmarkList();
    }
}

// 前向声明：客户端页相关函数（定义在“客户端页”部分）
void ShowTabPage(int index);
void ApplyClientAuthMode();
void ClientConnectAndList(const std::string& host, int port,
                          const std::string& user, const std::string& pass);

// 根据“登录方式”下拉框，启用/禁用用户名与密码输入框
void ApplyClientAuthMode() {
    if (!g_cliAuthCombo) return;
    int sel = (int)SendMessageW(g_cliAuthCombo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) sel = 0;
    const bool useAccount = (sel == 1);
    if (g_cliUserEdit) EnableWindow(g_cliUserEdit, useAccount ? TRUE : FALSE);
    if (g_cliPassEdit) EnableWindow(g_cliPassEdit, useAccount ? TRUE : FALSE);
}

// 在内置客户端中打开指定快捷方式
void OpenBookmarkInClient(int index) {
    if (index < 0 || index >= (int)g_config.bookmarks.size()) return;
    const FtpBookmark& bm = g_config.bookmarks[index];
    // 切换到“客户端”分页（索引 5）
    SendMessage(g_hTab, TCM_SETCURSEL, 5, 0);
    ShowTabPage(5);
    // 填入地址与端口
    SetWindowTextW(g_cliHost, Utf8ToWide(bm.host).c_str());
    SetWindowTextW(g_cliPort, Utf8ToWide(std::to_string(bm.port)).c_str());
    // 记录登录凭据
    const bool anonymous = bm.user.empty() ||
        (_stricmp(bm.user.c_str(), "anonymous") == 0 && bm.pass.empty());
    g_cliUser = anonymous ? "anonymous" : bm.user;
    g_cliPass = anonymous ? "" : bm.pass;
    if (g_cliUserEdit) SetWindowTextW(g_cliUserEdit, Utf8ToWide(g_cliUser).c_str());
    if (g_cliPassEdit) SetWindowTextW(g_cliPassEdit, Utf8ToWide(g_cliPass).c_str());
    // 登录方式跟着书签走：无账号密码 → 匿名
    if (g_cliAuthCombo) {
        SendMessageW(g_cliAuthCombo, CB_SETCURSEL, anonymous ? 0 : 1, 0);
        ApplyClientAuthMode();
    }
    // 缓存本次连接的地址与端口到配置，下次启动自动回填
    g_config.cliHost = bm.host;
    g_config.cliPort = bm.port;
    g_config.cliUser = g_cliUser;
    g_config.cliPass = g_cliPass;
    g_config.cliAnonymous = anonymous;
    g_config.Save(g_configPath);
    // 发起连接
    ClientConnectAndList(bm.host, bm.port, g_cliUser, g_cliPass);
}

// URL 的 userinfo 部分做百分号编码：密码里出现 @ : / # ? 空格 或中文时，
// 直接拼进 ftp:// 会把地址结构拼坏（例如 p@ss 会被当成主机名的一部分）。
static std::string PercentEncodeUserInfo(const std::string& s) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

// 用资源管理器打开指定快捷方式
void OpenBookmarkInExplorer(int index) {
    if (index < 0 || index >= (int)g_config.bookmarks.size()) return;
    const FtpBookmark& bm = g_config.bookmarks[index];

    // 资源管理器的 ftp:// 地址不会弹“登录身份”框：没有密码时它只会用
    // anonymous 连一次，失败后直接弹“FTP 文件夹错误”。这里先提醒用户。
    if (bm.pass.empty()) {
        std::wstring tip = L"该快捷方式没有保存密码。\n\n"
            L"Windows 资源管理器打开 ftp:// 地址时不会弹出密码输入框，"
            L"只会以 anonymous 身份连接，服务器要求密码时会直接报“FTP 文件夹错误”。\n\n"
            L"建议：先编辑该快捷方式填入密码，或改用本工具的“打开连接”。\n\n"
            L"仍要用资源管理器打开吗？";
        if (MessageBoxW(g_hWnd, tip.c_str(), L"没有密码",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            return;
        }
    }

    std::string url = "ftp://";
    if (!bm.user.empty()) {
        url += PercentEncodeUserInfo(bm.user);
        if (!bm.pass.empty()) url += ":" + PercentEncodeUserInfo(bm.pass);
        url += "@";
    }
    url += bm.host;
    if (bm.port != 21) url += ":" + std::to_string(bm.port);
    std::wstring wurl = Utf8ToWide(url);
    ShellExecuteW(g_hWnd, L"open", L"explorer.exe", wurl.c_str(),
                  nullptr, SW_SHOWNORMAL);
}

// 一键打开：默认跳转到内置客户端
void OpenBookmark() {
    int sel = GetSelectedBookmark();
    if (sel < 0 || sel >= (int)g_config.bookmarks.size()) {
        MessageBoxW(g_hWnd, L"请先选择一个连接", L"提示", MB_ICONINFORMATION);
        return;
    }
    OpenBookmarkInClient(sel);
}

// 快捷方式列表右键菜单
void ShowBookmarkMenu() {
    int sel = GetSelectedBookmark();
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_BM_OPEN_CLIENT, L"在内置客户端打开");
    AppendMenuW(hMenu, MF_STRING, IDM_BM_OPEN_EXPLORER, L"用资源管理器打开");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_BM_EDIT, L"编辑");
    AppendMenuW(hMenu, MF_STRING, IDM_BM_DELETE, L"删除");

    POINT pt;
    GetCursorPos(&pt);
    int cmd = (int)TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                  pt.x, pt.y, 0, g_hWnd, nullptr);
    DestroyMenu(hMenu);
    switch (cmd) {
    case IDM_BM_OPEN_CLIENT:   OpenBookmarkInClient(sel); break;
    case IDM_BM_OPEN_EXPLORER: OpenBookmarkInExplorer(sel); break;
    case IDM_BM_EDIT:          EditBookmark(); break;
    case IDM_BM_DELETE:        DelBookmark(); break;
    }
}

// 尝试 TCP 连接指定主机:端口，成功返回 true（超时 3 秒）
bool TryConnect(const std::string& host, int port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    DWORD to = 3000;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof(to));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    bool ok = false;
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) == 1) {
        ok = connect(s, (sockaddr*)&addr, sizeof(addr)) == 0;
    } else {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) == 0) {
            for (auto p = res; p; p = p->ai_next) {
                if (connect(s, p->ai_addr, (int)p->ai_addrlen) == 0) { ok = true; break; }
            }
            freeaddrinfo(res);
        }
    }
    closesocket(s);
    return ok;
}

// 自定义消息：后台线程完成一条检测后通知 UI 更新状态列
#define WM_APP_BM_STATUS (WM_APP + 3)

// 检查连通：遍历所有快捷方式条目，逐条检测并更新状态列（无弹窗）
void CheckBookmarkConnect() {
    if (g_config.bookmarks.empty()) return;
    // 先把所有行状态置为“检测中”
    for (int i = 0; i < (int)g_config.bookmarks.size(); ++i) {
        SetBookmarkStatus(i, L"检测中", 0);
    }
    // 复制一份书签，避免后台线程访问 g_config 时被修改
    std::vector<FtpBookmark> bms = g_config.bookmarks;
    std::thread([bms]() {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
        for (int i = 0; i < (int)bms.size(); ++i) {
            bool ok = TryConnect(bms[i].host, bms[i].port);
            // 通过堆分配传递结果给 UI 线程
            int* payload = new int[2];
            payload[0] = i;
            payload[1] = ok ? 1 : 2;  // 1=开放(绿) 2=关闭(红)
            if (g_hWnd) PostMessage(g_hWnd, WM_APP_BM_STATUS, 0, (LPARAM)payload);
        }
        WSACleanup();
    }).detach();
}

// ---- 客户端页 ----

// 前向声明：获取系统文件图标索引（定义在“高级设置页”部分）
int GetFileIconIndex(const std::wstring& path, bool isDir);
int GetFileIconIndexEx(HIMAGELIST images, const std::wstring& path, bool isDir);

// 自定义消息：客户端后台操作完成后通知 UI 线程刷新
#define WM_APP_CLI_REFRESH (WM_APP + 4)   // 用缓存重绘列表
#define WM_APP_CLI_STATUS  (WM_APP + 5)   // 操作结果（成功写日志，失败弹窗）
#define WM_APP_CLI_RELIST  (WM_APP + 6)   // 重新从服务器拉取目录并重绘

// 客户端列表图标列表
HIMAGELIST g_cliImages = nullptr;

// 刷新客户端文件列表（从 g_cliEntries 填充 ListView）
void RefreshClientList() {
    if (!g_cliList) return;
    std::lock_guard<std::mutex> lk(g_cliEntriesMutex);
    SendMessage(g_cliList, LVM_DELETEALLITEMS, 0, 0);
    int row = 0;
    for (const auto& e : g_cliEntries) {
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_IMAGE;
        item.iItem = row;
        item.iSubItem = 0;
        std::wstring wname = Utf8ToWide(e.name);
        item.pszText = (LPWSTR)wname.c_str();
        // 使用客户端列表自己的图标列表（按扩展名/文件夹取系统图标）
        item.iImage = GetFileIconIndexEx(g_cliImages, Utf8ToWide(e.name), e.isDir);
        int idx = (int)SendMessageW(g_cliList, LVM_INSERTITEMW, 0, (LPARAM)&item);

        // 大小列：文件夹显示 <文件夹>，文件显示带单位的可读大小
        std::wstring wsize = e.isDir ? L"<文件夹>" : Utf8ToWide(FormatSize(e.size));
        LVITEMW sub{};
        sub.mask = LVIF_TEXT;
        sub.iItem = idx;
        sub.iSubItem = 1;
        sub.pszText = (LPWSTR)wsize.c_str();
        SendMessageW(g_cliList, LVM_SETITEMW, 0, (LPARAM)&sub);
        ++row;
    }
    // 更新路径显示
    if (g_cliPath && g_client) {
        SetWindowTextW(g_cliPath, Utf8ToWide(g_client->CurrentDir()).c_str());
    }
}

// 后台线程：连接服务器并列出目录
void ClientConnectAndList(const std::string& host, int port,
                          const std::string& user, const std::string& pass) {
    std::thread([host, port, user, pass]() {
        std::vector<FtpClient::Entry> entries;
        std::string err;
        bool ok = false;
        {
            std::lock_guard<std::mutex> lk(g_clientMutex);
            if (!g_client) g_client = std::make_unique<FtpClient>();
            g_client->SetTransferMode(
                (FtpClient::TransferMode)g_config.transferMode);
            ok = g_client->Connect(host, (uint16_t)port, user, pass);
            if (ok) ok = g_client->List(entries);
            if (!ok) err = g_client->LastError();
        }
        if (ok) {
            std::lock_guard<std::mutex> lk(g_cliEntriesMutex);
            g_cliEntries = entries;
            if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_REFRESH, 0, 0);
        } else {
            std::string* msg = new std::string(err.empty() ? "连接失败" : err);
            if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_STATUS, 0, (LPARAM)msg);
        }
    }).detach();
}

// 后台线程：刷新当前目录
void ClientRefresh() {
    std::thread([]() {
        std::vector<FtpClient::Entry> entries;
        std::string err;
        bool ok = false;
        {
            std::lock_guard<std::mutex> lk(g_clientMutex);
            if (g_client && g_client->IsConnected()) {
                ok = g_client->List(entries);
                if (!ok) err = g_client->LastError();
            } else {
                err = "尚未连接";
            }
        }
        if (ok) {
            std::lock_guard<std::mutex> lk(g_cliEntriesMutex);
            g_cliEntries = entries;
            if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_REFRESH, 0, 0);
        } else {
            std::string* msg = new std::string(err);
            if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_STATUS, 0, (LPARAM)msg);
        }
    }).detach();
}

// 进入选中的目录（双击或右键“打开”）
void ClientEnterSelected() {
    int sel = (int)SendMessage(g_cliList, LVM_GETNEXTITEM, (WPARAM)-1,
                               MAKELPARAM(LVNI_SELECTED, 0));
    std::string name;
    {
        std::lock_guard<std::mutex> lk(g_cliEntriesMutex);
        if (sel < 0 || sel >= (int)g_cliEntries.size()) return;
        if (!g_cliEntries[sel].isDir) return;   // 文件不进入
        name = g_cliEntries[sel].name;
    }
    std::thread([name]() {
        std::vector<FtpClient::Entry> entries;
        std::string err;
        bool ok = false;
        {
            std::lock_guard<std::mutex> lk(g_clientMutex);
            if (g_client && g_client->IsConnected()) {
                ok = g_client->ChangeDir(name);
                if (ok) ok = g_client->List(entries);
                if (!ok) err = g_client->LastError();
            } else err = "尚未连接";
        }
        if (ok) {
            std::lock_guard<std::mutex> lk(g_cliEntriesMutex);
            g_cliEntries = entries;
            if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_REFRESH, 0, 0);
        } else {
            std::string* msg = new std::string(err);
            if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_STATUS, 0, (LPARAM)msg);
        }
    }).detach();
}

// 返回上级目录
void ClientGoUp() {
    std::thread([]() {
        std::vector<FtpClient::Entry> entries;
        std::string err;
        bool ok = false;
        {
            std::lock_guard<std::mutex> lk(g_clientMutex);
            if (g_client && g_client->IsConnected()) {
                ok = g_client->ChangeDirUp();
                if (ok) ok = g_client->List(entries);
                if (!ok) err = g_client->LastError();
            } else err = "尚未连接";
        }
        if (ok) {
            std::lock_guard<std::mutex> lk(g_cliEntriesMutex);
            g_cliEntries = entries;
            if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_REFRESH, 0, 0);
        } else {
            std::string* msg = new std::string(err);
            if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_STATUS, 0, (LPARAM)msg);
        }
    }).detach();
}

// 把面包屑输入解析为规范的绝对远程路径：
//   - 统一 '\' 为 '/'；去除首尾空白
//   - 以 '/' 开头：视为绝对路径直达
//   - 否则：拼接到当前目录之后（支持在当前路径末尾直接续输子路径）
//   - 折叠重复的 '/'；'.' 与 '..' 交给服务器规范化
static std::string NormalizeCwdInput(const std::string& current,
                                     const std::string& raw) {
    std::string s = raw;
    std::replace(s.begin(), s.end(), '\\', '/');
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    s = s.substr(a, b - a + 1);

    std::string joined;
    if (!s.empty() && s[0] == '/') {
        joined = s;
    } else {
        joined = current;
        if (joined.empty()) joined = "/";
        if (joined.back() != '/') joined.push_back('/');
        joined += s;
    }

    // 折叠重复斜杠
    std::string out;
    for (char c : joined) {
        if (c == '/' && !out.empty() && out.back() == '/') continue;
        out.push_back(c);
    }
    if (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}

// 跳转到指定路径（面包屑输入框回车触发）
void ClientChangeDirTo(const std::string& path) {
    std::thread([path]() {
        std::vector<FtpClient::Entry> entries;
        std::string err;
        bool ok = false;
        {
            std::lock_guard<std::mutex> lk(g_clientMutex);
            if (g_client && g_client->IsConnected()) {
                std::string target = NormalizeCwdInput(g_client->CurrentDir(), path);
                if (target.empty()) {
                    err = "路径为空";
                } else {
                    ok = g_client->ChangeDir(target);
                    if (ok) ok = g_client->List(entries);
                    if (!ok) err = g_client->LastError();
                }
            } else err = "尚未连接";
        }
        if (ok) {
            std::lock_guard<std::mutex> lk(g_cliEntriesMutex);
            g_cliEntries = entries;
            if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_REFRESH, 0, 0);
        } else {
            std::string* msg = new std::string(err);
            if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_STATUS, 0, (LPARAM)msg);
        }
    }).detach();
}

// 路径输入框子类化：回车跳转到输入的目录，Esc 恢复当前目录
WNDPROC g_cliPathOldProc = nullptr;

LRESULT CALLBACK CliPathSubclassProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            std::string path = GetTextUtf8(hWnd);
            if (!path.empty()) ClientChangeDirTo(path);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            // 恢复为当前实际目录
            if (g_client) {
                SetWindowTextW(hWnd, Utf8ToWide(g_client->CurrentDir()).c_str());
            }
            return 0;
        }
    }
    return CallWindowProcW(g_cliPathOldProc, hWnd, msg, wp, lp);
}

// 地址/端口输入框子类化：回车等同于点击“进入”按钮
WNDPROC g_cliHostOldProc = nullptr;
WNDPROC g_cliPortOldProc = nullptr;

LRESULT CALLBACK CliConnectSubclassProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        SendMessageW(GetParent(hWnd), WM_COMMAND,
                     MAKEWPARAM(IDC_CLI_ENTER, BN_CLICKED), 0);
        return 0;
    }
    WNDPROC old = (hWnd == g_cliHost) ? g_cliHostOldProc : g_cliPortOldProc;
    return CallWindowProcW(old, hWnd, msg, wp, lp);
}

// 客户端文件列表子类化：支持键盘快捷键
//   Delete 删除选中项、Enter 进入目录、Backspace 返回上级、F5 刷新
WNDPROC g_cliListOldProc = nullptr;

// 前向声明：这些函数定义在下方，供子类化过程调用
void ClientDeleteSelected();
void ClientEnterSelected();
void ClientGoUp();
void ClientRefresh();

LRESULT CALLBACK CliListSubclassProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        switch (wp) {
        case VK_DELETE:
            ClientDeleteSelected();
            return 0;
        case VK_RETURN:
            ClientEnterSelected();
            return 0;
        case VK_BACK:
            ClientGoUp();
            return 0;
        case VK_F5:
            ClientRefresh();
            return 0;
        }
    }
    return CallWindowProcW(g_cliListOldProc, hWnd, msg, wp, lp);
}

// 下载选中的文件/文件夹（可多选、混合）到指定本地目录
void ClientDownloadSelected(const std::wstring& localDir) {
    // 已有传输进行中时拒绝新传输（进度弹窗与客户端都是单实例）
    if (ProgressDialog::IsBusy()) {
        MessageBoxW(g_hWnd, L"已有传输正在进行，请等待完成后再试。",
                    L"FtpTool", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::vector<FtpOps::RemoteItem> items = CollectSelectedItems();
    if (items.empty()) return;
    std::wstring firstName = Utf8ToWide(items[0].name);
    // 先同步弹出进度窗；目录总大小统计完后再 Update 总量
    ProgressDialog::Begin(g_hWnd, L"下载", firstName, 0);
    std::thread([items, localDir]() {
        std::string err;
        bool ok = false;
        uint64_t totalBytes = 0;
        {
            std::lock_guard<std::mutex> lk(g_clientMutex);
            if (g_client && g_client->IsConnected()) {
                // 统计全部待下载字节（递归子目录）
                FtpOps::MeasureRemote(*g_client, items, totalBytes);
                ProgressDialog::Update(0, totalBytes);
                ok = FtpOps::Download(*g_client, items, fs::path(localDir),
                                      totalBytes,
                    [](const FtpOps::Progress& p) {
                        ProgressDialog::Update(p.done, p.total, p.verifying,
                                               p.verifyDone, p.verifyTotal);
                        if (!p.current.empty())
                            ProgressDialog::SetFileName(p.current);
                    },
                    [] { return ProgressDialog::IsCancelled(); });
                if (!ok) err = g_client->LastError();
            } else err = "尚未连接";
        }
        bool cancelled = !ok && ProgressDialog::IsCancelled();
        ProgressDialog::End();
        std::string names;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) names += ", ";
            names += items[i].name;
        }
        if (cancelled) {
            LogCallbackFn(0, "已取消下载: " + names);
            return;
        }
        std::string* msg = new std::string(
            ok ? ("下载文件: " + names)
               : ("下载失败: " + names + " | " +
                  (err.empty() ? std::string("未知错误") : err)));
        if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_STATUS, ok ? 1 : 0, (LPARAM)msg);
        else delete msg;
    }).detach();
}

// 上传本地路径（文件/文件夹，可混合多选）到当前远程目录
void ClientUploadPaths(const std::vector<fs::path>& paths) {
    // 已有传输进行中时拒绝新传输（进度弹窗与客户端都是单实例）
    if (ProgressDialog::IsBusy()) {
        MessageBoxW(g_hWnd, L"已有传输正在进行，请等待完成后再试。",
                    L"FtpTool", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (paths.empty()) return;
    uint64_t totalSize = FtpOps::MeasureLocal(paths);
    std::wstring firstName = paths[0].filename().wstring();
    // UI 线程同步创建进度弹窗
    ProgressDialog::Begin(g_hWnd, L"上传", firstName, totalSize);
    std::thread([paths, totalSize]() {
        std::string err;
        bool ok = false;
        {
            std::lock_guard<std::mutex> lk(g_clientMutex);
            if (g_client && g_client->IsConnected()) {
                ok = FtpOps::Upload(*g_client, paths, totalSize,
                    [](const FtpOps::Progress& p) {
                        ProgressDialog::Update(p.done, p.total, p.verifying,
                                               p.verifyDone, p.verifyTotal);
                        if (!p.current.empty())
                            ProgressDialog::SetFileName(p.current);
                    },
                    [] { return ProgressDialog::IsCancelled(); });
                if (!ok) err = g_client->LastError();
            } else err = "尚未连接";
        }
        bool cancelled = !ok && ProgressDialog::IsCancelled();
        ProgressDialog::End();
        std::string names;
        for (size_t i = 0; i < paths.size(); ++i) {
            if (i) names += ", ";
            names += paths[i].filename().u8string();
        }
        if (cancelled) {
            LogCallbackFn(0, "已取消上传: " + names);
            // 已经传了一部分：刷新列表显示实际结果
            if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_RELIST, 0, 0);
            return;
        }
        std::string* msg = new std::string(
            ok ? ("上传文件: " + names)
               : ("上传失败: " + names + " | " +
                  (err.empty() ? std::string("未知错误") : err)));
        if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_STATUS, ok ? 1 : 0, (LPARAM)msg);
        else delete msg;
        // 上传完成后重新拉取目录并刷新文件列表
        if (ok && g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_RELIST, 0, 0);
    }).detach();
}

// 删除选中的文件/文件夹（可多选、混合；文件夹递归删除全部内容）
void ClientDeleteSelected() {
    if (ProgressDialog::IsBusy()) {
        MessageBoxW(g_hWnd, L"已有操作正在进行，请等待完成后再试。",
                    L"FtpTool", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::vector<FtpOps::RemoteItem> items = CollectSelectedItems();
    if (items.empty()) return;

    // 确认文案（多选时列出条目，文件夹明确提示递归）
    std::wstring wmsg;
    if (items.size() == 1) {
        wmsg = L"确定删除 " + Utf8ToWide(items[0].name) + L" 吗？";
        if (items[0].isDir)
            wmsg += L"\n该文件夹及其全部子内容将被递归删除。";
    } else {
        wmsg = L"确定删除选中的 " + std::to_wstring(items.size()) +
               L" 项吗？\n其中的文件夹将被递归删除全部内容。\n\n";
        size_t shown = 0;
        for (const auto& it : items) {
            if (shown >= 12) { wmsg += L"…"; break; }
            wmsg += Utf8ToWide(it.name);
            wmsg += it.isDir ? L" [文件夹]\n" : L"\n";
            ++shown;
        }
    }
    if (MessageBoxW(g_hWnd, wmsg.c_str(), L"确认删除",
                    MB_YESNO | MB_ICONWARNING) != IDYES) return;

    std::wstring firstName = Utf8ToWide(items[0].name);
    ProgressDialog::Begin(g_hWnd, L"删除", firstName, 0);
    std::thread([items]() {
        std::string err;
        bool ok = false;
        uint64_t totalEntries = 0;
        {
            std::lock_guard<std::mutex> lk(g_clientMutex);
            if (g_client && g_client->IsConnected()) {
                // 先统计总条目数用于进度，再递归删除
                FtpOps::CountRemote(*g_client, items, totalEntries);
                ProgressDialog::Update(0, totalEntries);
                ok = FtpOps::Remove(*g_client, items, totalEntries,
                    [](const FtpOps::Progress& p) {
                        ProgressDialog::Update(p.done, p.total);
                        if (!p.current.empty())
                            ProgressDialog::SetFileName(p.current);
                    },
                    [] { return ProgressDialog::IsCancelled(); });
                if (!ok) err = g_client->LastError();
            } else err = "尚未连接";
        }
        bool cancelled = !ok && ProgressDialog::IsCancelled();
        ProgressDialog::End();
        std::string names;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) names += ", ";
            names += items[i].name;
        }
        if (cancelled) {
            LogCallbackFn(0, "已取消删除: " + names);
            // 可能已经删了一部分，刷新列表显示实际结果
            if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_RELIST, 0, 0);
            return;
        }
        std::string* msg = new std::string(
            ok ? ("已删除：" + names)
               : ("删除失败: " + names + " | " +
                  (err.empty() ? std::string("未知错误") : err)));
        if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_STATUS, ok ? 1 : 0, (LPARAM)msg);
        else delete msg;
        // 删除后重新拉取目录并刷新文件列表
        if (ok && g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_RELIST, 0, 0);
    }).detach();
}

// 通用文本输入对话框的数据
struct InputDlgData {
    std::wstring title;
    std::wstring prompt;
    std::wstring text;
    bool ok = false;
};

LRESULT CALLBACK InputDlgProc(HWND hD, UINT m, WPARAM wp, LPARAM lp) {
    InputDlgData* dd = (InputDlgData*)GetWindowLongPtrW(hD, GWLP_USERDATA);
    switch (m) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        dd = (InputDlgData*)cs->lpCreateParams;
        SetWindowLongPtrW(hD, GWLP_USERDATA, (LONG_PTR)dd);
        // 字体进程级缓存：输入框可反复创建，避免累积 GDI 泄漏
        static HFONT f = nullptr;
        if (!f) {
            f = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        }
        HWND st = CreateWindowW(L"STATIC", dd->prompt.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT, 15, 15, 280, 20, hD, nullptr, nullptr, nullptr);
        HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", dd->text.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 15, 40, 280, 24, hD,
            (HMENU)4001, nullptr, nullptr);
        HWND b1 = CreateWindowW(L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 110, 78, 85, 30, hD,
            (HMENU)IDOK, nullptr, nullptr);
        HWND b2 = CreateWindowW(L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 210, 78, 85, 30, hD,
            (HMENU)IDCANCEL, nullptr, nullptr);
        HWND all[] = {st, ed, b1, b2};
        for (HWND h : all) SendMessage(h, WM_SETFONT, (WPARAM)f, TRUE);
        SetFocus(ed);
        SendMessage(ed, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            HWND ed = GetDlgItem(hD, 4001);
            int len = GetWindowTextLengthW(ed);
            std::wstring w(len + 1, L'\0');
            GetWindowTextW(ed, &w[0], len + 1);
            w.resize(len);
            dd->text = w;
            dd->ok = true;
            DestroyWindow(hD);
        } else if (LOWORD(wp) == IDCANCEL) {
            DestroyWindow(hD);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hD);
        return 0;
    }
    return DefWindowProcW(hD, m, wp, lp);
}

// 显示通用文本输入对话框。返回 true 表示用户点击确定。
bool ShowInputDialog(const std::wstring& title, const std::wstring& prompt,
                     std::wstring& out, const std::wstring& initial = L"") {
    InputDlgData data;
    data.title = title;
    data.prompt = prompt;
    data.text = initial;

    static bool registered = false;
    const wchar_t* dlgCls = L"FtpInputDlg";
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = InputDlgProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = dlgCls;
        RegisterClassExW(&wc);
        registered = true;
    }

    RECT rc = {0, 0, 310, 120};
    AdjustWindowRect(&rc, WS_CAPTION | WS_SYSMENU, FALSE);
    HWND hD = CreateWindowExW(WS_EX_DLGMODALFRAME, dlgCls, title.c_str(),
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        DialogOwner(), nullptr, g_hInst, &data);

    // 居中到主窗口
    CenterOverOwner(hD, g_hWnd);
    EnableWindow(g_hWnd, FALSE);
    MSG msg;
    while (IsWindow(hD) && GetMessage(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessage(hD, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    EnableWindow(g_hWnd, TRUE);
    SetForegroundWindow(g_hWnd);

    if (data.ok) out = data.text;
    return data.ok;
}

// 新建文件夹
void ClientNewFolder() {
    std::wstring name;
    if (ShowInputDialog(L"新建文件夹", L"请输入文件夹名称：", name, L"新建文件夹") && !name.empty()) {
        std::string remoteName = WideToUtf8(name);
        std::thread([remoteName]() {
            std::string err;
            bool ok = false;
            {
                std::lock_guard<std::mutex> lk(g_clientMutex);
                if (g_client && g_client->IsConnected()) {
                    ok = g_client->MakeDir(remoteName);
                    if (!ok) err = g_client->LastError();
                } else err = "尚未连接";
            }
            std::string* msg = new std::string(
                ok ? ("已创建：" + remoteName)
                   : ("新建文件夹失败：" + remoteName + " | " +
                      (err.empty() ? std::string("未知错误") : err)));
            if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_STATUS, ok ? 1 : 0, (LPARAM)msg);
            else delete msg;
            // 新建后重新拉取目录并刷新文件列表
            if (ok && g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_RELIST, 0, 0);
        }).detach();
    }
}

// 重命名选中项
void ClientRenameSelected() {
    int sel = (int)SendMessage(g_cliList, LVM_GETNEXTITEM, (WPARAM)-1,
                               MAKELPARAM(LVNI_SELECTED, 0));
    std::string oldName;
    {
        std::lock_guard<std::mutex> lk(g_cliEntriesMutex);
        if (sel < 0 || sel >= (int)g_cliEntries.size()) return;
        oldName = g_cliEntries[sel].name;
    }
    std::wstring newName;
    if (!ShowInputDialog(L"重命名", L"请输入新名称：", newName, Utf8ToWide(oldName)))
        return;
    if (newName.empty() || WideToUtf8(newName) == oldName) return;
    std::string newNameU8 = WideToUtf8(newName);
    std::thread([oldName, newNameU8]() {
        std::string err;
        bool ok = false;
        {
            std::lock_guard<std::mutex> lk(g_clientMutex);
            if (g_client && g_client->IsConnected()) {
                ok = g_client->Rename(oldName, newNameU8);
                if (!ok) err = g_client->LastError();
            } else err = "尚未连接";
        }
        std::string* msg = new std::string(
            ok ? ("已重命名：" + oldName + " -> " + newNameU8)
               : ("重命名失败：" + oldName + " -> " + newNameU8 + " | " +
                  (err.empty() ? std::string("未知错误") : err)));
        if (g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_STATUS, ok ? 1 : 0, (LPARAM)msg);
        else delete msg;
        // 重命名后重新拉取目录并刷新文件列表
        if (ok && g_hWnd) PostMessage(g_hWnd, WM_APP_CLI_RELIST, 0, 0);
    }).detach();
}

// 客户端右键菜单
void ShowClientMenu() {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_CLI_OPEN, L"打开");
    AppendMenuW(hMenu, MF_STRING, IDM_CLI_DOWNLOAD, L"下载到...");
    AppendMenuW(hMenu, MF_STRING, IDM_CLI_UPLOAD, L"上传文件...");
    AppendMenuW(hMenu, MF_STRING, IDM_CLI_UPLOAD_DIR, L"上传文件夹...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_CLI_NEWFOLDER, L"新建文件夹");
    AppendMenuW(hMenu, MF_STRING, IDM_CLI_RENAME, L"重命名");
    AppendMenuW(hMenu, MF_STRING, IDM_CLI_DELETE, L"删除");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_CLI_REFRESH, L"刷新");
    AppendMenuW(hMenu, MF_STRING, IDM_CLI_EXPLORER, L"用资源管理器打开");

    POINT pt;
    GetCursorPos(&pt);
    int cmd = (int)TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                  pt.x, pt.y, 0, g_hWnd, nullptr);
    DestroyMenu(hMenu);
    switch (cmd) {
    case IDM_CLI_OPEN:     ClientEnterSelected(); break;
    case IDM_CLI_DOWNLOAD: {
        // 选择本地目录（新版 IFileDialog 文件夹选择器）
        std::string dir = PickFolderUtf8(g_hWnd, L"选择下载保存目录");
        if (!dir.empty()) ClientDownloadSelected(Utf8ToWide(dir));
        break;
    }
    case IDM_CLI_UPLOAD: {
        // 多选本地文件（大缓冲以容纳多个长路径）
        std::vector<wchar_t> buf(65536, L'\0');
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = g_hWnd;
        ofn.lpstrFile = buf.data();
        ofn.nMaxFile = (DWORD)buf.size();
        ofn.lpstrFilter = L"所有文件\0*.*\0";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                    OFN_ALLOWMULTISELECT | OFN_EXPLORER;
        if (GetOpenFileNameW(&ofn)) {
            std::vector<fs::path> paths;
            const wchar_t* p = buf.data();
            fs::path dir(p);
            // 单选：目录段之后直接就是结尾空字符
            if (*(p + wcslen(p) + 1) == L'\0') {
                paths.push_back(dir);
            } else {
                // 多选布局：目录 \0 文件1 \0 文件2 \0 \0
                p += wcslen(p) + 1;
                while (*p) {
                    paths.push_back(dir / fs::path(p));
                    p += wcslen(p) + 1;
                }
            }
            ClientUploadPaths(paths);
        }
        break;
    }
    case IDM_CLI_UPLOAD_DIR: {
        // 选择整个文件夹上传（新版文件夹选择器）
        std::string dir = PickFolderUtf8(g_hWnd, L"选择要上传的文件夹");
        if (!dir.empty())
            ClientUploadPaths({fs::path(Utf8ToWide(dir))});
        break;
    }
    case IDM_CLI_NEWFOLDER: ClientNewFolder(); break;
    case IDM_CLI_RENAME:    ClientRenameSelected(); break;
    case IDM_CLI_DELETE:    ClientDeleteSelected(); break;
    case IDM_CLI_REFRESH:   ClientRefresh(); break;
    case IDM_CLI_EXPLORER: {
        // 用资源管理器打开当前 FTP 地址
        std::string host = GetTextUtf8(g_cliHost);
        std::string port = GetTextUtf8(g_cliPort);
        std::string url = "ftp://" + host;
        if (port != "21" && !port.empty()) url += ":" + port;
        if (g_client) url += g_client->CurrentDir();
        ShellExecuteW(g_hWnd, L"open", L"explorer.exe",
                      Utf8ToWide(url).c_str(), nullptr, SW_SHOWNORMAL);
        break;
    }
    }
}

// ---- 高级设置页 ----

// 获取系统文件图标索引（加入指定图标列表，返回索引）
int GetFileIconIndexEx(HIMAGELIST images, const std::wstring& path, bool isDir) {
    if (!images) return -1;
    SHFILEINFOW sfi{};
    DWORD flags = SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES;
    DWORD attr = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if (SHGetFileInfoW(path.c_str(), attr, &sfi, sizeof(sfi), flags)) {
        int idx = ImageList_AddIcon(images, sfi.hIcon);
        DestroyIcon(sfi.hIcon);
        return idx;
    }
    return -1;
}

// 获取系统文件图标索引（加入高级设置页图标列表，返回索引）
int GetFileIconIndex(const std::wstring& path, bool isDir) {
    return GetFileIconIndexEx(g_advImages, path, isDir);
}

// 高级设置列表的子类化：支持键盘快捷键
//   Ctrl+A 全选、Ctrl+D 取消选择、空格 切换当前项选中状态
WNDPROC g_advListOldProc = nullptr;

LRESULT CALLBACK AdvListSubclassProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ctrl && (wp == 'A' || wp == 'a')) {
            // 全选
            ListView_SetItemState(hWnd, -1, LVIS_SELECTED, LVIS_SELECTED);
            return 0;
        }
        if (ctrl && (wp == 'D' || wp == 'd')) {
            // 取消选择
            ListView_SetItemState(hWnd, -1, 0, LVIS_SELECTED);
            return 0;
        }
        if (wp == VK_SPACE) {
            // 切换当前焦点项的选中状态
            int focus = (int)SendMessage(hWnd, LVM_GETNEXTITEM, (WPARAM)-1,
                                         MAKELPARAM(LVNI_FOCUSED, 0));
            if (focus >= 0) {
                UINT st = (UINT)SendMessage(hWnd, LVM_GETITEMSTATE, focus, LVIS_SELECTED);
                ListView_SetItemState(hWnd, focus,
                    (st & LVIS_SELECTED) ? 0 : LVIS_SELECTED, LVIS_SELECTED);
            }
            return 0;
        }
    }
    return CallWindowProcW(g_advListOldProc, hWnd, msg, wp, lp);
}

// 规范化 FTP 路径：以 / 开头，去掉结尾 /
std::string NormalizeFtpPath(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    if (p.empty() || p[0] != '/') p = "/" + p;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

// 计算某路径的有效权限（与服务器端 EffectivePerms 逻辑保持一致）：
// 高级模式下，取匹配该路径（精确或作为父目录前缀）的最具体规则；
// 无匹配规则时返回全局权限。
FtpPermissions EffectivePermsFor(const std::string& ftpPath) {
    if (!g_config.advancedMode) return g_config.perms;
    std::string p = NormalizeFtpPath(ftpPath);
    const FilePermission* best = nullptr;
    size_t bestLen = 0;
    for (const auto& fp : g_config.filePerms) {
        std::string rp = NormalizeFtpPath(fp.path);
        bool match = (p == rp) ||
                     (p.size() > rp.size() && p.compare(0, rp.size(), rp) == 0 &&
                      (rp == "/" || p[rp.size()] == '/'));
        if (match && rp.size() >= bestLen) {
            best = &fp;
            bestLen = rp.size();
        }
    }
    if (best) return best->perms;
    return g_config.perms;
}

// 刷新高级设置的文件列表（ListView：图标 + 名称 + 权限）
void RefreshAdvList() {
    if (!g_advList) return;
    SendMessage(g_advList, LVM_DELETEALLITEMS, 0, 0);
    std::string root = GetTextUtf8(g_hPath);
    if (root.empty()) return;
    try {
        std::error_code ec;
        fs::path rootLong = fs::path(ToLongPathW(Utf8ToWide(root)));
        if (!fs::is_directory(rootLong, ec)) return;
        int row = 0;
        for (auto& e : fs::directory_iterator(rootLong, ec)) {
            std::string name = e.path().filename().u8string();
            std::string rel = "/" + name;
            std::error_code ec2;
            bool isDir = e.is_directory(ec2);

            // 是否被标记为隐藏（仅界面提示，服务端仍可访问）
            bool hidden = false;
            for (const auto& fp : g_config.filePerms) {
                if (NormalizeFtpPath(fp.path) == rel && fp.hidden) { hidden = true; break; }
            }

            // 权限简写：隐藏项显示 <HIDE>，否则按有效权限显示。
            // 注意：文件没有“列目录”概念，L 位仅对文件夹有意义，
            // 因此文件一律不显示 L 位（显示为 -）。
            std::string permStr;
            if (hidden) {
                permStr = "<HIDE>";
            } else {
                FtpPermissions ep = EffectivePermsFor(rel);
                if (!isDir) ep.list = false;   // 文件不显示列目录位
                permStr = ep.ToShortString();
            }

            LVITEMW item{};
            item.mask = LVIF_TEXT | LVIF_IMAGE;
            item.iItem = row;
            item.iSubItem = 0;
            std::wstring wname = Utf8ToWide(name);
            item.pszText = (LPWSTR)wname.c_str();
            item.iImage = GetFileIconIndex(e.path().filename().wstring(), isDir);
            int idx = (int)SendMessageW(g_advList, LVM_INSERTITEMW, 0, (LPARAM)&item);

            std::wstring wperm = Utf8ToWide(permStr);
            LVITEMW sub{};
            sub.mask = LVIF_TEXT;
            sub.iItem = idx;
            sub.iSubItem = 1;
            sub.pszText = (LPWSTR)wperm.c_str();
            SendMessageW(g_advList, LVM_SETITEMW, 0, (LPARAM)&sub);

            ++row;
        }
    } catch (...) {
        // 忽略遍历异常，避免崩溃
    }
}

// 获取当前选中项的相对路径（单选，返回第一个选中项）
std::string GetSelectedAdvPath() {
    int sel = (int)SendMessage(g_advList, LVM_GETNEXTITEM, (WPARAM)-1,
                               MAKELPARAM(LVNI_SELECTED, 0));
    if (sel < 0) return "";
    wchar_t buf[512] = {0};
    LVITEMW item{};
    item.iSubItem = 0;
    item.pszText = buf;
    item.cchTextMax = 512;
    SendMessageW(g_advList, LVM_GETITEMTEXTW, sel, (LPARAM)&item);
    return "/" + WideToUtf8(buf);
}

// 获取所有选中项的相对路径（支持多选）
std::vector<std::string> GetSelectedAdvPaths() {
    std::vector<std::string> paths;
    int sel = -1;
    while ((sel = (int)SendMessage(g_advList, LVM_GETNEXTITEM, (WPARAM)sel,
                                   MAKELPARAM(LVNI_SELECTED, 0))) >= 0) {
        wchar_t buf[512] = {0};
        LVITEMW item{};
        item.iSubItem = 0;
        item.pszText = buf;
        item.cchTextMax = 512;
        SendMessageW(g_advList, LVM_GETITEMTEXTW, sel, (LPARAM)&item);
        paths.push_back("/" + WideToUtf8(buf));
    }
    return paths;
}

// 查找或创建某路径的规则，返回其在 vector 中的下标（避免持有指针）
int FindOrCreateFilePermIndex(const std::string& path) {
    for (size_t i = 0; i < g_config.filePerms.size(); ++i) {
        if (g_config.filePerms[i].path == path) return (int)i;
    }
    FilePermission fp;
    fp.path = path;
    // 默认全部权限
    fp.perms.read = fp.perms.write = fp.perms.del = fp.perms.mkdir =
        fp.perms.rmdir = fp.perms.rename = fp.perms.list = true;
    g_config.filePerms.push_back(fp);
    return (int)g_config.filePerms.size() - 1;
}

// 右键菜单：设置选中项的权限（支持多选）
void ShowPermMenu() {
    std::vector<std::string> paths = GetSelectedAdvPaths();
    if (paths.empty()) {
        MessageBoxW(g_hWnd, L"请先选择一个文件或文件夹", L"提示", MB_ICONINFORMATION);
        return;
    }
    // 以第一个选中项的当前权限作为菜单勾选状态
    FtpPermissions cur = EffectivePermsFor(paths[0]);

    // 判断选中项是否全部为文件（文件没有“列目录”概念，不显示该菜单项）
    std::string root = GetTextUtf8(g_hPath);
    bool allFiles = true;
    for (const auto& p : paths) {
        std::error_code ec;
        std::string full = root + p;
        std::replace(full.begin(), full.end(), '/', '\\');
        if (fs::is_directory(fs::path(Utf8ToWide(full)), ec)) { allFiles = false; break; }
    }

    HMENU hMenu = CreatePopupMenu();
    struct Item { const wchar_t* text; int id; bool checked; };
    Item items[] = {
        {L"读取（下载）",   IDM_PERM_READ,   cur.read},
        {L"写入（上传）",   IDM_PERM_WRITE,  cur.write},
        {L"删除文件",       IDM_PERM_DEL,    cur.del},
        {L"创建目录",       IDM_PERM_MKDIR,  cur.mkdir},
        {L"删除目录",       IDM_PERM_RMDIR,  cur.rmdir},
        {L"重命名",         IDM_PERM_RENAME, cur.rename},
        {L"列目录",         IDM_PERM_LIST,   cur.list},
    };
    for (auto& it : items) {
        // 文件不显示“列目录”项
        if (allFiles && it.id == IDM_PERM_LIST) continue;
        UINT flags = MF_STRING | (it.checked ? MF_CHECKED : MF_UNCHECKED);
        AppendMenuW(hMenu, flags, it.id, it.text);
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_PERM_ALL, L"全部允许");
    AppendMenuW(hMenu, MF_STRING, IDM_PERM_NONE, L"全部禁止");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_PERM_HIDE, L"隐藏（客户端不可见）");
    AppendMenuW(hMenu, MF_STRING, IDM_PERM_UNHIDE, L"取消隐藏");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_PERM_CLEAR, L"清除此规则（恢复默认）");

    POINT pt;
    GetCursorPos(&pt);
    int cmd = (int)TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                  pt.x, pt.y, 0, g_hWnd, nullptr);
    DestroyMenu(hMenu);
    if (cmd == 0) return;

    // 对每个选中路径应用操作（菜单返回后再按下标查找，避免指针失效）
    for (const auto& path : paths) {
        if (cmd == IDM_PERM_CLEAR) {
            for (size_t i = 0; i < g_config.filePerms.size(); ++i) {
                if (g_config.filePerms[i].path == path) {
                    g_config.filePerms.erase(g_config.filePerms.begin() + i);
                    break;
                }
            }
            continue;
        }
        if (cmd == IDM_PERM_HIDE) {
            int idx = FindOrCreateFilePermIndex(path);
            g_config.filePerms[idx].hidden = true;
            continue;
        }
        if (cmd == IDM_PERM_UNHIDE) {
            for (auto& fp : g_config.filePerms) {
                if (fp.path == path) { fp.hidden = false; break; }
            }
            continue;
        }
        int idx = FindOrCreateFilePermIndex(path);
        FtpPermissions& pm = g_config.filePerms[idx].perms;
        switch (cmd) {
        case IDM_PERM_READ:   pm.read   = !pm.read; break;
        case IDM_PERM_WRITE:  pm.write  = !pm.write; break;
        case IDM_PERM_DEL:    pm.del    = !pm.del; break;
        case IDM_PERM_MKDIR:  pm.mkdir  = !pm.mkdir; break;
        case IDM_PERM_RMDIR:  pm.rmdir  = !pm.rmdir; break;
        case IDM_PERM_RENAME: pm.rename = !pm.rename; break;
        case IDM_PERM_LIST:   pm.list   = !pm.list; break;
        case IDM_PERM_ALL:
            pm.read = pm.write = pm.del = pm.mkdir =
                pm.rmdir = pm.rename = pm.list = true;
            break;
        case IDM_PERM_NONE:
            pm.read = pm.write = pm.del = pm.mkdir =
                pm.rmdir = pm.rename = pm.list = false;
            break;
        }
    }
    g_config.Save(g_configPath);
    PushPermissionsToServer();
    RefreshAdvList();
}

// 对选中项设置全部允许/禁止（支持多选）
void SetSelectedPermAll(bool allow) {
    std::vector<std::string> paths = GetSelectedAdvPaths();
    if (paths.empty()) {
        MessageBoxW(g_hWnd, L"请先选择一个文件或文件夹", L"提示", MB_ICONINFORMATION);
        return;
    }
    for (const auto& path : paths) {
        int idx = FindOrCreateFilePermIndex(path);
        FtpPermissions& pm = g_config.filePerms[idx].perms;
        pm.read = pm.write = pm.del = pm.mkdir =
            pm.rmdir = pm.rename = pm.list = allow;
    }
    g_config.Save(g_configPath);
    PushPermissionsToServer();
    RefreshAdvList();
}

// 隐藏选中项：在配置中标记为隐藏（仅管理端界面提示，服务端仍可访问）
void HideSelectedAdv() {
    std::vector<std::string> paths = GetSelectedAdvPaths();
    if (paths.empty()) {
        MessageBoxW(g_hWnd, L"请先选择一个文件或文件夹", L"提示", MB_ICONINFORMATION);
        return;
    }
    for (const auto& path : paths) {
        int idx = FindOrCreateFilePermIndex(path);
        g_config.filePerms[idx].hidden = true;
    }
    g_config.Save(g_configPath);
    PushPermissionsToServer();
    RefreshAdvList();
}

// 取消隐藏选中项
void UnhideSelectedAdv() {
    std::vector<std::string> paths = GetSelectedAdvPaths();
    if (paths.empty()) {
        MessageBoxW(g_hWnd, L"请先选择一个文件或文件夹", L"提示", MB_ICONINFORMATION);
        return;
    }
    for (const auto& path : paths) {
        for (auto& fp : g_config.filePerms) {
            if (fp.path == path) { fp.hidden = false; break; }
        }
    }
    g_config.Save(g_configPath);
    PushPermissionsToServer();
    RefreshAdvList();
}

// 高级模式启用开关
void OnAdvEnableToggle() {
    bool on = SendMessage(g_advEnable, BM_GETCHECK, 0, 0) == BST_CHECKED;
    // 启用时禁用左侧权限设置分组
    EnableWindow(g_permGroup, !on);
    HWND permBoxes[] = {g_chkAll, g_chkRead, g_chkWrite, g_chkDel,
                        g_chkMkdir, g_chkRmdir, g_chkRename, g_chkList};
    for (HWND b : permBoxes) EnableWindow(b, !on);
    EnableWindow(g_advList, on);
    EnableWindow(GetDlgItem(g_hWnd, IDC_ADV_REFRESH), on);
    EnableWindow(GetDlgItem(g_hWnd, IDC_ADV_ALLOW), on);
    EnableWindow(GetDlgItem(g_hWnd, IDC_ADV_DENY), on);
    if (on) RefreshAdvList();
    g_config.advancedMode = on;
    g_config.Save(g_configPath);
    PushPermissionsToServer();
}

// 从任意 PNG 字节创建指定尺寸的 HICON（用 WIC 解码，支持透明通道）
static HICON CreateIconFromPngData(const unsigned char* png,
                                   unsigned int len, int size) {
    HICON hIcon = nullptr;
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    IWICBitmapScaler* scaler = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return nullptr;

    do {
        if (FAILED(factory->CreateStream(&stream))) break;
        if (FAILED(stream->InitializeFromMemory(
                (BYTE*)png, len))) break;
        if (FAILED(factory->CreateDecoderFromStream(
                stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder))) break;
        if (FAILED(decoder->GetFrame(0, &frame))) break;
        if (FAILED(factory->CreateFormatConverter(&converter))) break;
        if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.0,
                WICBitmapPaletteTypeCustom))) break;
        if (FAILED(factory->CreateBitmapScaler(&scaler))) break;
        if (FAILED(scaler->Initialize(converter, size, size,
                WICBitmapInterpolationModeFant))) break;

        // 取出 BGRA 像素
        UINT w = size, h = size;
        UINT stride = w * 4;
        std::vector<BYTE> pixels(stride * h);
        if (FAILED(scaler->CopyPixels(nullptr, stride, (UINT)pixels.size(),
                pixels.data()))) break;

        // 创建 32bpp DIB，生成 HICON
        BITMAPV5HEADER bi{};
        bi.bV5Size = sizeof(BITMAPV5HEADER);
        bi.bV5Width = w;
        bi.bV5Height = -(LONG)h;  // 自上而下
        bi.bV5Planes = 1;
        bi.bV5BitCount = 32;
        bi.bV5Compression = BI_BITFIELDS;
        bi.bV5RedMask = 0x00FF0000;
        bi.bV5GreenMask = 0x0000FF00;
        bi.bV5BlueMask = 0x000000FF;
        bi.bV5AlphaMask = 0xFF000000;

        HDC hdc = GetDC(nullptr);
        void* bits = nullptr;
        HBITMAP hColor = CreateDIBSection(hdc, (BITMAPINFO*)&bi,
            DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, hdc);
        if (!hColor) break;
        memcpy(bits, pixels.data(), pixels.size());

        HBITMAP hMask = CreateBitmap(w, h, 1, 1, nullptr);
        ICONINFO ii{};
        ii.fIcon = TRUE;
        ii.hbmColor = hColor;
        ii.hbmMask = hMask;
        hIcon = CreateIconIndirect(&ii);
        DeleteObject(hColor);
        DeleteObject(hMask);
    } while (false);

    if (scaler) scaler->Release();
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    return hIcon;
}

// 窗口/任务栏图标：用内嵌的应用图标 PNG
HICON CreateIconFromPng(int size) {
    return CreateIconFromPngData(kIconPng, kIconPngSize, size);
}

// 链接样式字体：系统默认 UI 字体 + 下划线（用于“前往”这类可点击文字）
static HFONT CreateLinkFont() {
    LOGFONTW lf{};
    HFONT base = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!base || !GetObjectW(base, sizeof(lf), &lf)) {
        ZeroMemory(&lf, sizeof(lf));
        lf.lfHeight = -12;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    }
    lf.lfUnderline = TRUE;
    return CreateFontIndirectW(&lf);
}

// 创建字体
HFONT CreateUiFont(int size, bool bold) {
    return CreateFontW(-size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

// 左侧配置面板宽度
const int kLeftWidth = 340;

// 显示/隐藏分页内容
void ShowTabPage(int index) {
    // 性能设置(3) / 安全提醒(4) / 其他设置(6)：整页白底，先显示白色底板
    if (g_pagePanel) {
        const bool whitePage = (index == 3 || index == 4 || index == 6);
        ShowWindow(g_pagePanel, whitePage ? SW_SHOW : SW_HIDE);
        if (whitePage) InvalidateRect(g_pagePanel, nullptr, TRUE);
    }
    // 页5：客户端顶部两行（地址/端口、用户名/密码）单独一块白底
    if (g_cliTopPanel) {
        ShowWindow(g_cliTopPanel, index == 5 ? SW_SHOW : SW_HIDE);
        if (index == 5) InvalidateRect(g_cliTopPanel, nullptr, TRUE);
    }
    ShowWindow(g_hLog, index == 0 ? SW_SHOW : SW_HIDE);
    // 日志分类过滤按钮栏：仅运行日志页显示
    for (HWND h : g_logFilterBtns) {
        if (h) ShowWindow(h, index == 0 ? SW_SHOW : SW_HIDE);
    }
    ShowWindow(g_bmList, index == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(g_hWnd, IDC_BM_ADD), index == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(g_hWnd, IDC_BM_DEL), index == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(g_hWnd, IDC_BM_EDIT), index == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(g_hWnd, IDC_BM_OPEN), index == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(g_hWnd, IDC_BM_CHECK), index == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_advEnable, index == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_advHint, index == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_advList, index == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(g_hWnd, IDC_ADV_REFRESH), index == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(g_hWnd, IDC_ADV_ALLOW), index == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(g_hWnd, IDC_ADV_DENY), index == 2 ? SW_SHOW : SW_HIDE);

    // 页3：性能设置
    HWND perfCtrls[] = {g_perfTimeout, g_perfNoDelay, g_perfSockBuf,
                        g_perfAppBuf, g_perfMaxFile, g_perfMaxConn, g_perfHint,
                        g_perfResetBtn, g_perfSaveBtn, g_sldTimeout, g_sldSockBuf,
                        g_sldAppBuf, g_sldMaxFile, g_sldMaxConn, g_perfTransferMode};
    for (HWND h : perfCtrls) ShowWindow(h, index == 3 ? SW_SHOW : SW_HIDE);
    for (HWND h : g_perfLabels) ShowWindow(h, index == 3 ? SW_SHOW : SW_HIDE);

    // 页4：安全提醒
    HWND secCtrls[] = {g_secDelFile, g_secDelDir, g_secRename, g_secOverwrite,
                       g_secDownload, g_secRecycle, g_secIpMode, g_secTimeout,
                       g_secTimeoutLabel, g_sldSecTimeout, g_secHint, g_secSaveBtn,
                       g_secResetBtn,
                       GetDlgItem(g_hWnd, IDC_SEC_RECYCLE_LABEL),
                       GetDlgItem(g_hWnd, IDC_SEC_IPMODE_LABEL)};
    for (HWND h : secCtrls) ShowWindow(h, index == 4 ? SW_SHOW : SW_HIDE);

    // 页5：客户端
    HWND cliCtrls[] = {g_cliHost, g_cliPort, g_cliList, g_cliPath,
                       g_cliUserEdit, g_cliPassEdit, g_cliAuthCombo,
                       GetDlgItem(g_hWnd, IDC_CLI_ENTER),
                       GetDlgItem(g_hWnd, IDC_CLI_REFRESH),
                       GetDlgItem(g_hWnd, IDC_CLI_BACK)};
    for (HWND h : cliCtrls) ShowWindow(h, index == 5 ? SW_SHOW : SW_HIDE);
    // 客户端页的"地址:""端口:"静态标签（仅控制客户端页自己的标签，
    // 不影响服务器配置区的"端口:"标签）
    if (g_cliHostLabel) ShowWindow(g_cliHostLabel, index == 5 ? SW_SHOW : SW_HIDE);
    if (g_cliPortLabel) ShowWindow(g_cliPortLabel, index == 5 ? SW_SHOW : SW_HIDE);
    if (g_cliUserLabel) ShowWindow(g_cliUserLabel, index == 5 ? SW_SHOW : SW_HIDE);
    if (g_cliPassLabel) ShowWindow(g_cliPassLabel, index == 5 ? SW_SHOW : SW_HIDE);

    // 页6：其他设置
    HWND otherCtrls[] = {g_otherBindLabel, g_otherEncodingLabel,
                         g_otherBind, g_otherEncoding, g_otherEncodingHint,
                         g_otherAuthor, g_ghIcon, g_ghGo,
                         g_siteIcon, g_siteGo,
                         g_otherResetBtn, g_otherSaveBtn};
    for (HWND h : otherCtrls) ShowWindow(h, index == 6 ? SW_SHOW : SW_HIDE);

    // 页7：网络发现
    HWND discCtrls[] = {g_discNetLabel, g_discPortLabel, g_discNet,
                        g_discPorts, g_discScanBtn, g_discList,
                        g_discStatus};
    for (HWND h : discCtrls) ShowWindow(h, index == 7 ? SW_SHOW : SW_HIDE);
}

// 切换高级模式 / 精简模式。
// 精简模式：右侧分页、IP 限制、被动端口和 7 项权限全部隐藏，
//            只保留“读取/写入”两类权限，窗口缩到 352x210；
// 高级模式：恢复 920x570 和原来的 7 项权限/被动端口/IP/分页。
// 只隐藏/显示控件，不销毁，两种模式的权限设置互不覆盖。
void ApplyCompactMode(bool compact) {
    if (!g_hWnd) return;
    g_config.compactMode = compact;

    if (g_viewToggle) {
        SetWindowTextW(g_viewToggle, compact ? L"精简模式" : L"高级模式");
        SendMessage(g_viewToggle, BM_SETCHECK,
                    compact ? BST_UNCHECKED : BST_CHECKED, 0);
    }

    // 高级模式专用的 7 项权限控件
    HWND advPerms[] = {g_permGroup, g_chkAll, g_chkRead, g_chkWrite,
                       g_chkDel, g_chkMkdir, g_chkRmdir, g_chkRename,
                       g_chkList};
    // 被动端口控件：精简模式下由“读取/写入”两个勾选框占据这一行
    HWND pasvCtrls[] = {g_pasvLabel, g_hPasvStart, g_pasvDash, g_hPasvEnd};

    if (compact) {
        // 右侧分页控件不是 Tab 的子窗口，必须逐个隐藏；
        // ShowTabPage(-1) 会隐藏所有分页内容与日志过滤按钮。
        ShowTabPage(-1);
        ShowWindow(g_hTab, SW_HIDE);
        ShowWindow(g_ipGroup, SW_HIDE);
        ShowWindow(g_ipEdit, SW_HIDE);
        ShowWindow(g_ipList, SW_HIDE);
        ShowWindow(GetDlgItem(g_hWnd, IDC_IP_ADD), SW_HIDE);
        ShowWindow(GetDlgItem(g_hWnd, IDC_IP_DEL), SW_HIDE);
        for (HWND h : pasvCtrls) ShowWindow(h, SW_HIDE);
        for (HWND h : advPerms) ShowWindow(h, SW_HIDE);
        ShowWindow(g_chkCompactRead, SW_SHOW);
        ShowWindow(g_chkCompactWrite, SW_SHOW);
        ShowWindow(g_chkCompactDelete, SW_SHOW);
    }

    int clientW = compact ? 352 : 920;
    int clientH = compact ? 210 : 570;
    RECT wr;
    GetWindowRect(g_hWnd, &wr);
    RECT rc = {0, 0, clientW, clientH};
    AdjustWindowRectEx(&rc,
        (DWORD)GetWindowLongPtrW(g_hWnd, GWL_STYLE), FALSE,
        (DWORD)GetWindowLongPtrW(g_hWnd, GWL_EXSTYLE));
    int outerW = rc.right - rc.left;
    int outerH = rc.bottom - rc.top;

    HMONITOR mon = MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    RECT work{};
    if (mon && GetMonitorInfoW(mon, &mi)) work = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    int wx = wr.left, wy = wr.top;
    if (wx + outerW > work.right) wx = work.right - outerW;
    if (wy + outerH > work.bottom) wy = work.bottom - outerH;
    if (wx < work.left) wx = work.left;
    if (wy < work.top) wy = work.top;
    SetWindowPos(g_hWnd, nullptr, wx, wy, outerW, outerH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    if (!compact) {
        ShowWindow(g_ipGroup, SW_SHOW);
        ShowWindow(g_ipEdit, SW_SHOW);
        ShowWindow(g_ipList, SW_SHOW);
        ShowWindow(GetDlgItem(g_hWnd, IDC_IP_ADD), SW_SHOW);
        ShowWindow(GetDlgItem(g_hWnd, IDC_IP_DEL), SW_SHOW);
        for (HWND h : pasvCtrls) ShowWindow(h, SW_SHOW);
        for (HWND h : advPerms) ShowWindow(h, SW_SHOW);
        ShowWindow(g_chkCompactRead, SW_HIDE);
        ShowWindow(g_chkCompactWrite, SW_HIDE);
        ShowWindow(g_chkCompactDelete, SW_HIDE);
        int sel = (int)SendMessage(g_hTab, TCM_GETCURSEL, 0, 0);
        if (sel < 0) sel = 0;
        ShowTabPage(sel);
        ShowWindow(g_hTab, SW_SHOW);
    }

    // 启动按钮：精简 y=118、高 80；高级 y=488、高 70
    int toggleY = compact ? 118 : 488;
    int toggleH = clientH - toggleY - 12;
    if (g_hToggle) {
        MoveWindow(g_hToggle, 12, toggleY, kLeftWidth - 24, toggleH, TRUE);
    }

    // 模式切换后，立即把当前模式对应的权限同步到运行中的服务
    PushPermissionsToServer();
}

// ---- 网络发现 ----

struct ScanProgressMsg {
    uint64_t done = 0;
    uint64_t total = 0;
    double speed = 0.0;
    double remain = 0.0;
    bool stopped = false;
};

struct ScanResultMsg {
    std::string ip;
    int port = 0;
    std::string host;
};

// 把秒数转成简短的中文时长
std::wstring FormatScanDuration(double secs) {
    if (secs < 1.0) return L"<1 秒";
    int total = (int)(secs + 0.5);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    wchar_t buf[64];
    if (h > 0) swprintf_s(buf, L"%d 小时 %d 分", h, m);
    else if (m > 0) swprintf_s(buf, L"%d 分 %d 秒", m, s);
    else swprintf_s(buf, L"%d 秒", s);
    return buf;
}

// 根据本机 IPv4 构造待扫描主机列表。0=C 类(/24)，1=B 类(/16)。
std::vector<uint32_t> BuildScanHosts(int type, std::string& err) {
    std::vector<uint32_t> hosts;
    std::string local = g_localIp;
    if (local.empty() || local == "0.0.0.0")
        local = FtpServer::GetLocalIp();
    in_addr addr{};
    if (local.empty() || inet_pton(AF_INET, local.c_str(), &addr) != 1) {
        err = "无法获取本机 IPv4 地址，请先连接网络或手动绑定网卡。";
        return hosts;
    }
    uint32_t base = ntohl(addr.S_un.S_addr);
    if (type == 0) {
        base &= 0xFFFFFF00u;
        for (uint32_t i = 1; i < 255; ++i) hosts.push_back(base | i);
    } else {
        base &= 0xFFFF0000u;
        for (uint32_t i = 1; i < 65535; ++i) hosts.push_back(base + i);
    }
    return hosts;
}

// 解析端口列表：支持 21;2121、逗号/空格分隔，* 代表全部端口。
std::vector<uint16_t> ParseScanPorts(const std::string& text, std::string& err) {
    std::vector<uint16_t> ports;
    std::string t = text;
    auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (!t.empty() && isSpace(t.front())) t.erase(t.begin());
    while (!t.empty() && isSpace(t.back())) t.pop_back();
    if (t.empty()) {
        err = "请输入扫描端口，例如 21;2121，或输入 * 扫描全部端口。";
        return ports;
    }
    if (t.find('*') != std::string::npos) {
        if (t != "*") {
            err = "* 只能单独使用，不能和其他端口混写。";
            return ports;
        }
        for (int p = 1; p <= 65535; ++p) ports.push_back((uint16_t)p);
        return ports;
    }

    std::vector<bool> seen(65536, false);
    size_t i = 0;
    while (i < t.size()) {
        while (i < t.size() &&
               (t[i] == ';' || t[i] == ',' || isSpace(t[i]))) ++i;
        if (i >= t.size()) break;
        size_t j = i;
        while (j < t.size() && t[j] != ';' && t[j] != ',' && !isSpace(t[j])) ++j;
        std::string tok = t.substr(i, j - i);
        char* end = nullptr;
        long v = strtol(tok.c_str(), &end, 10);
        if (!end || *end != '\0' || v < 1 || v > 65535) {
            err = "端口格式无效：" + tok;
            return {};
        }
        if (!seen[(size_t)v]) {
            seen[(size_t)v] = true;
            ports.push_back((uint16_t)v);
        }
        i = j;
    }
    if (ports.empty()) {
        err = "没有解析到有效端口。";
    }
    return ports;
}

bool IsLocalAddress(const std::string& ip) {
    if (ip == "127.0.0.1") return true;
    if (!g_localIp.empty() && ip == g_localIp) return true;
    for (const auto& b : g_bindIps) {
        if (b.ip == ip && b.ip != "0.0.0.0") return true;
    }
    return false;
}

// 反向解析 IP 的主机名；本机地址统一显示“计算机名 (你自己)”。
std::string LookupHostName(const std::string& ip) {
    if (IsLocalAddress(ip)) {
        wchar_t name[MAX_COMPUTERNAME_LENGTH + 1]{};
        DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
        std::wstring host = GetComputerNameW(name, &n)
            ? std::wstring(name, n) : std::wstring(L"本机");
        host += L" (你自己)";
        return WideToUtf8(host);
    }

    in_addr ia{};
    if (inet_pton(AF_INET, ip.c_str(), &ia) != 1) return "";
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr = ia;
    wchar_t host[NI_MAXHOST]{};
    int rc = GetNameInfoW((SOCKADDR*)&sa, sizeof(sa),
                          host, NI_MAXHOST, nullptr, 0, NI_NAMEREQD);
    if (rc == 0 && host[0]) return WideToUtf8(host);
    return "";
}

const wchar_t* ScanServiceName(int port) {
    switch (port) {
    case 21: case 2121: return L"FTP";
    case 22:   return L"SSH";
    case 23:   return L"Telnet";
    case 25:   return L"SMTP";
    case 53:   return L"DNS";
    case 80: case 8080: return L"HTTP";
    case 110:  return L"POP3";
    case 143:  return L"IMAP";
    case 443: case 8443: return L"HTTPS";
    case 445:  return L"SMB";
    case 1433: return L"SQL Server";
    case 3306: return L"MySQL";
    case 3389: return L"RDP";
    case 5900: return L"VNC";
    default:   return L"开放";
    }
}

int GetSelectedDiscovery() {
    if (!g_discList) return -1;
    return (int)SendMessageW(g_discList, LVM_GETNEXTITEM, (WPARAM)-1,
                             MAKELPARAM(LVNI_SELECTED, 0));
}

void ClearDiscoveryResults() {
    g_discEntries.clear();
    if (g_discList) ListView_DeleteAllItems(g_discList);
}

void AddDiscoveryResult(const std::string& ip, int port,
                        const std::string& host) {
    DiscoveryEntry e;
    e.ip = ip;
    e.port = port;
    e.host = host;
    g_discEntries.push_back(e);

    if (!g_discList) return;
    int row = ListView_GetItemCount(g_discList);
    std::wstring wip = Utf8ToWide(ip);
    std::wstring wport = std::to_wstring(port);

    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = 0;
    item.pszText = (LPWSTR)wip.c_str();
    int idx = (int)SendMessageW(g_discList, LVM_INSERTITEMW, 0, (LPARAM)&item);

    LVITEMW sub{};
    sub.mask = LVIF_TEXT;
    sub.iItem = idx;
    sub.iSubItem = 1;
    sub.pszText = (LPWSTR)wport.c_str();
    SendMessageW(g_discList, LVM_SETITEMW, 0, (LPARAM)&sub);

    sub.iSubItem = 2;
    sub.pszText = (LPWSTR)ScanServiceName(port);
    SendMessageW(g_discList, LVM_SETITEMW, 0, (LPARAM)&sub);

    std::wstring whost = Utf8ToWide(host);
    sub.iSubItem = 3;
    sub.pszText = (LPWSTR)(whost.empty() ? L"--" : whost.c_str());
    SendMessageW(g_discList, LVM_SETITEMW, 0, (LPARAM)&sub);
}

void UpdateScanProgress(const ScanProgressMsg& p) {
    if (g_scanBar) {
        int pos = 0;
        if (p.total) pos = (int)((long double)p.done * 1000.0 / (long double)p.total);
        SendMessageW(g_scanBar, PBM_SETPOS, (WPARAM)pos, 0);
    }
    if (!g_scanInfo) return;

    std::wstring text = p.stopped ? L"正在终止扫描...\r\n" : L"正在扫描网络...\r\n";
    text += L"已完成 " + std::to_wstring(p.done) + L" / " + std::to_wstring(p.total);
    if (p.total) {
        wchar_t pct[64];
        swprintf_s(pct, L"（%.1f%%）",
                   (double)p.done * 100.0 / (double)p.total);
        text += pct;
    }
    text += L"\r\n";
    if (p.stopped) {
        text += L"扫描速度：--    预计剩余：--";
    } else {
        wchar_t speed[64];
        swprintf_s(speed, L"扫描速度：%.0f 个/秒", p.speed);
        text += speed;
        text += L"    预计剩余：";
        text += (p.remain > 0.0 ? FormatScanDuration(p.remain) : L"--");
    }
    SetWindowTextW(g_scanInfo, text.c_str());
}

void OpenDiscoveryInClient(int row) {
    if (row < 0 || row >= (int)g_discEntries.size()) return;
    const DiscoveryEntry& e = g_discEntries[row];
    if (g_cliHost) SetWindowTextW(g_cliHost, Utf8ToWide(e.ip).c_str());
    if (g_cliPort) SetWindowTextW(g_cliPort, std::to_wstring(e.port).c_str());
    SendMessage(g_hTab, TCM_SETCURSEL, 5, 0);
    ShowTabPage(5);
    ClientConnectAndList(e.ip, e.port, g_cliUser, g_cliPass);
}

void OpenDiscoveryInExplorer(int row) {
    if (row < 0 || row >= (int)g_discEntries.size()) return;
    const DiscoveryEntry& e = g_discEntries[row];
    std::wstring url = L"ftp://" + Utf8ToWide(e.ip) + L":" +
                       std::to_wstring(e.port);
    ShellExecuteW(g_hWnd, L"open", L"explorer.exe", url.c_str(),
                  nullptr, SW_SHOWNORMAL);
}

// 把网络发现里的一条结果加进“快捷方式”页
// 已存在相同地址（IP + 端口）时先询问，确认覆盖后只更新地址，保留用户自定义的名称与账号
void AddDiscoveryToBookmarks(int row) {
    if (row < 0 || row >= (int)g_discEntries.size()) return;
    const DiscoveryEntry& e = g_discEntries[row];

    int exist = -1;
    for (int i = 0; i < (int)g_config.bookmarks.size(); ++i) {
        const FtpBookmark& bm = g_config.bookmarks[i];
        if (bm.port == e.port && _stricmp(bm.host.c_str(), e.ip.c_str()) == 0) {
            exist = i;
            break;
        }
    }

    if (exist >= 0) {
        const FtpBookmark& bm = g_config.bookmarks[exist];
        std::wstring msg = L"快捷方式页中已存在该连接：\n\n    ";
        msg += Utf8ToWide(bm.name) + L"    " + Utf8ToWide(bm.host) +
               L":" + std::to_wstring(bm.port);
        msg += L"\n\n是否覆盖？";
        if (MessageBoxW(g_hWnd, msg.c_str(), L"已存在",
                        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return;

        FtpBookmark& dst = g_config.bookmarks[exist];
        // 名字是按地址自动生成的（或为空）才跟着改，用户手动改过的名字保留
        std::string oldHost = dst.host;
        dst.host = e.ip;
        dst.port = e.port;
        if (dst.name.empty() || _stricmp(dst.name.c_str(), oldHost.c_str()) == 0)
            dst.name = e.ip;
        g_config.Save(g_configPath);
        RefreshBookmarkList();
        return;
    }

    FtpBookmark bm;
    bm.name = e.ip;
    bm.host = e.ip;
    bm.port = e.port;
    bm.user = "anonymous";
    // 同名但端口不同的条目并排显示时用 “IP:端口” 区分
    for (const FtpBookmark& b : g_config.bookmarks) {
        if (_stricmp(b.name.c_str(), bm.name.c_str()) == 0) {
            bm.name = e.ip + ":" + std::to_string(e.port);
            break;
        }
    }
    g_config.bookmarks.push_back(bm);
    g_config.Save(g_configPath);
    RefreshBookmarkList();
    MessageBoxW(g_hWnd, L"已添加到快捷方式页。", L"提示",
                MB_OK | MB_ICONINFORMATION);
}

void ShowDiscoveryMenu() {
    if (!g_discList) return;
    POINT pt;
    GetCursorPos(&pt);
    POINT client = pt;
    ScreenToClient(g_discList, &client);
    LVHITTESTINFO hit{};
    hit.pt = client;
    int row = ListView_SubItemHitTest(g_discList, &hit);
    if (row < 0) return;
    ListView_SetItemState(g_discList, row,
        LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_DISC_EXPLORER, L"在资源管理器中打开");
    AppendMenuW(hMenu, MF_STRING, IDM_DISC_CLIENT, L"在伪客户端中打开");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_DISC_ADD_BM, L"添加到快捷方式页");
    int cmd = (int)TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                  pt.x, pt.y, 0, g_hWnd, nullptr);
    DestroyMenu(hMenu);
    if (cmd == IDM_DISC_CLIENT) OpenDiscoveryInClient(row);
    else if (cmd == IDM_DISC_EXPLORER) OpenDiscoveryInExplorer(row);
    else if (cmd == IDM_DISC_ADD_BM) AddDiscoveryToBookmarks(row);
}

void RequestScanStop() {
    g_scanStop.store(true);
    if (g_scanStopBtn) {
        EnableWindow(g_scanStopBtn, FALSE);
        SetWindowTextW(g_scanStopBtn, L"正在终止...");
    }
}

LRESULT CALLBACK ScanDlgProc(HWND hD, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        g_scanDlg = hD;
        if (!g_scanFont) {
            g_scanFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                L"Microsoft YaHei UI");
        }
        HFONT f = g_scanFont;
        HWND title = CreateWindowW(L"STATIC",
            L"正在扫描局域网，请稍候...",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 18, 472, 22, hD, nullptr, nullptr, nullptr);
        g_scanBar = CreateWindowExW(0, PROGRESS_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            24, 46, 472, 24, hD, (HMENU)IDC_SCAN_PROGRESS, nullptr, nullptr);
        SendMessageW(g_scanBar, PBM_SETRANGE32, 0, 1000);
        g_scanInfo = CreateWindowW(L"STATIC", L"正在准备扫描...\r\n",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 80, 472, 66, hD, (HMENU)IDC_SCAN_INFO, nullptr, nullptr);
        g_scanStopBtn = CreateWindowW(L"BUTTON", L"终止",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            376, 158, 120, 32, hD, (HMENU)IDC_SCAN_STOP, nullptr, nullptr);
        SendMessageW(title, WM_SETFONT, (WPARAM)f, TRUE);
        SendMessageW(g_scanBar, WM_SETFONT, (WPARAM)f, TRUE);
        SendMessageW(g_scanInfo, WM_SETFONT, (WPARAM)f, TRUE);
        SendMessageW(g_scanStopBtn, WM_SETFONT, (WPARAM)f, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_SCAN_STOP) {
            RequestScanStop();
        }
        return 0;
    case WM_CLOSE:
        RequestScanStop();
        DestroyWindow(hD);
        return 0;
    case WM_DESTROY:
        if (g_scanDlg == hD) {
            g_scanDlg = nullptr;
            g_scanBar = nullptr;
            g_scanInfo = nullptr;
            g_scanStopBtn = nullptr;
        }
        if (g_scanFont) {
            DeleteObject(g_scanFont);
            g_scanFont = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hD, m, wp, lp);
}

bool ShowScanDialog() {
    static bool registered = false;
    const wchar_t* cls = L"FtpScanProgressDlg";
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ScanDlgProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = cls;
        RegisterClassExW(&wc);
        registered = true;
    }

    RECT rc = {0, 0, 520, 210};
    AdjustWindowRect(&rc, WS_CAPTION | WS_SYSMENU, FALSE);
    HWND owner = DialogOwner();
    HWND hD = CreateWindowExW(WS_EX_DLGMODALFRAME, cls,
        L"网络发现 - 扫描中",
        WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        owner, nullptr, g_hInst, nullptr);
    if (!hD) return false;
    CenterOverOwner(hD, owner ? owner : g_hWnd);
    ShowWindow(hD, SW_SHOW);
    SetForegroundWindow(hD);
    return true;
}

// 在一次扫描中最多同时挂起的非阻塞连接数
const int kMaxScanProbes = 64;
const int kScanConnectTimeoutMs = 250;

void StartNetworkScan() {
    if (g_scanRunning.load()) {
        MessageBoxW(g_hWnd, L"已有扫描任务正在运行。", L"网络发现",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    int type = 0;
    if (g_discNet) {
        int sel = (int)SendMessageW(g_discNet, CB_GETCURSEL, 0, 0);
        if (sel == 1) type = 1;
    }
    std::string err;
    std::vector<uint16_t> ports =
        ParseScanPorts(GetTextUtf8(g_discPorts), err);
    if (ports.empty()) {
        MessageBoxW(g_hWnd, Utf8ToWide(err).c_str(), L"网络发现",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    std::vector<uint32_t> hosts = BuildScanHosts(type, err);
    if (hosts.empty()) {
        MessageBoxW(g_hWnd, Utf8ToWide(err).c_str(), L"网络发现",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    ClearDiscoveryResults();
    if (!ShowScanDialog()) {
        MessageBoxW(g_hWnd, L"无法创建扫描进度窗口。", L"网络发现",
                    MB_OK | MB_ICONERROR);
        return;
    }
    g_scanRunning.store(true);
    g_scanStop.store(false);
    if (g_discScanBtn) EnableWindow(g_discScanBtn, FALSE);
    if (g_discStatus) SetWindowTextW(g_discStatus, L"正在扫描...");

    try {
        std::thread([hosts, ports]() {
            WSADATA wsa{};
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
                if (g_hWnd) PostMessageW(g_hWnd, WM_APP_SCAN_DONE, 2, 0);
                return;
            }

            const uint64_t total =
                (uint64_t)hosts.size() * (uint64_t)ports.size();
            uint64_t next = 0;
            uint64_t completed = 0;
            ULONGLONG started = GetTickCount64();
            ULONGLONG lastPost = started;

            struct Probe {
                SOCKET s = INVALID_SOCKET;
                uint64_t task = 0;
                ULONGLONG start = 0;
            };
            std::vector<Probe> active;
            active.reserve(kMaxScanProbes);

            std::map<std::string, std::string> hostCache;
            auto post_result = [&](const std::string& ip, int port) {
                std::string host;
                auto it = hostCache.find(ip);
                if (it != hostCache.end()) {
                    host = it->second;
                } else {
                    host = LookupHostName(ip);
                    hostCache[ip] = host;
                }
                auto* msg = new ScanResultMsg();
                msg->ip = ip;
                msg->port = port;
                msg->host = host;
                if (!g_hWnd ||
                    !PostMessageW(g_hWnd, WM_APP_SCAN_RESULT, 0, (LPARAM)msg)) {
                    delete msg;
                }
            };

            auto post_progress = [&](bool stopped) {
                double elapsed = (GetTickCount64() - started) / 1000.0;
                double speed = elapsed > 0.001 ? (double)completed / elapsed : 0.0;
                double remain = speed > 0.01
                    ? (double)(total - completed) / speed : 0.0;
                auto* msg = new ScanProgressMsg();
                msg->done = completed;
                msg->total = total;
                msg->speed = speed;
                msg->remain = remain;
                msg->stopped = stopped;
                if (!g_hWnd ||
                    !PostMessageW(g_hWnd, WM_APP_SCAN_PROGRESS, 0, (LPARAM)msg)) {
                    delete msg;
                }
            };

            auto finish = [&](size_t i, bool ok) {
                SOCKET s = active[i].s;
                uint64_t task = active[i].task;
                closesocket(s);
                active[i] = active.back();
                active.pop_back();
                if (ok) {
                    uint32_t ip = hosts[(size_t)(task / ports.size())];
                    uint16_t port = ports[(size_t)(task % ports.size())];
                    in_addr ia{};
                    ia.S_un.S_addr = htonl(ip);
                    char ipbuf[INET_ADDRSTRLEN] = {0};
                    if (inet_ntop(AF_INET, &ia, ipbuf, sizeof(ipbuf)))
                        post_result(ipbuf, port);
                }
                ++completed;
            };

            while (!g_scanStop.load() && (next < total || !active.empty())) {
                while (!g_scanStop.load() &&
                       active.size() < (size_t)kMaxScanProbes && next < total) {
                    uint32_t ip = hosts[(size_t)(next / ports.size())];
                    uint16_t port = ports[(size_t)(next % ports.size())];
                    ++next;

                    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                    if (s == INVALID_SOCKET) {
                        ++completed;
                        continue;
                    }
                    u_long nb = 1;
                    ioctlsocket(s, FIONBIO, &nb);
                    sockaddr_in addr{};
                    addr.sin_family = AF_INET;
                    addr.sin_port = htons(port);
                    addr.sin_addr.S_un.S_addr = htonl(ip);
                    int r = connect(s, (sockaddr*)&addr, sizeof(addr));
                    if (r == 0) {
                        closesocket(s);
                        in_addr ia{};
                        ia.S_un.S_addr = htonl(ip);
                        char ipbuf[INET_ADDRSTRLEN] = {0};
                        if (inet_ntop(AF_INET, &ia, ipbuf, sizeof(ipbuf)))
                            post_result(ipbuf, port);
                        ++completed;
                    } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
                        Probe p;
                        p.s = s;
                        p.task = next - 1;
                        p.start = GetTickCount64();
                        active.push_back(p);
                    } else {
                        closesocket(s);
                        ++completed;
                    }
                }

                if (g_scanStop.load()) break;
                if (active.empty()) continue;

                fd_set wf;
                FD_ZERO(&wf);
                for (const auto& p : active) FD_SET(p.s, &wf);
                timeval tv{};
                tv.tv_sec = 0;
                tv.tv_usec = 10000;
                int n = select(0, nullptr, &wf, nullptr, &tv);
                if (n > 0) {
                    for (size_t i = 0; i < active.size(); ) {
                        if (FD_ISSET(active[i].s, &wf)) {
                            int soerr = 0;
                            int len = sizeof(soerr);
                            bool ok = getsockopt(active[i].s, SOL_SOCKET, SO_ERROR,
                                                 (char*)&soerr, &len) == 0 &&
                                      soerr == 0;
                            finish(i, ok);
                        } else {
                            ++i;
                        }
                    }
                }

                ULONGLONG now = GetTickCount64();
                for (size_t i = 0; i < active.size(); ) {
                    if (now - active[i].start >= (ULONGLONG)kScanConnectTimeoutMs)
                        finish(i, false);
                    else
                        ++i;
                }

                if (now - lastPost >= 120) {
                    post_progress(false);
                    lastPost = now;
                }
            }

            for (auto& p : active) closesocket(p.s);
            active.clear();
            post_progress(g_scanStop.load());
            if (g_hWnd)
                PostMessageW(g_hWnd, WM_APP_SCAN_DONE,
                             g_scanStop.load() ? 1 : 0, 0);
            WSACleanup();
        }).detach();
    } catch (...) {
        g_scanRunning.store(false);
        if (g_scanDlg) DestroyWindow(g_scanDlg);
        if (g_discScanBtn) EnableWindow(g_discScanBtn, TRUE);
        MessageBoxW(g_hWnd, L"无法创建扫描线程。", L"网络发现",
                    MB_OK | MB_ICONERROR);
    }
}

void CreateControls(HWND hWnd) {
    HFONT hFont = CreateUiFont(14, false);
    HFONT hFontBold = CreateUiFont(15, true);

    // 客户区尺寸，用于让控件占满剩余空间
    RECT client{};
    GetClientRect(hWnd, &client);
    int clientW = client.right;
    int clientH = client.bottom;

    int x = 12;
    int y = 12;

    // ---- 第一行：IP + 端口 ----
    CreateWindowW(L"STATIC", L"本机 IP:", WS_CHILD | WS_VISIBLE,
                  x, y + 3, 60, 20, hWnd, nullptr, g_hInst, nullptr);
    g_hIpLabel = CreateWindowW(L"STATIC", Utf8ToWide(g_localIp).c_str(),
                               WS_CHILD | WS_VISIBLE | SS_LEFT,
                               x + 62, y + 3, 110, 20, hWnd, (HMENU)IDC_IP_LABEL, g_hInst, nullptr);

    CreateWindowW(L"STATIC", L"端口:", WS_CHILD | WS_VISIBLE,
                  x + 178, y + 3, 40, 20, hWnd, nullptr, g_hInst, nullptr);
    g_hPort = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"21",
                              WS_CHILD | WS_VISIBLE | ES_NUMBER,
                              x + 218, y, 60, 24, hWnd, (HMENU)IDC_PORT_EDIT, g_hInst, nullptr);

    y += 34;
    // ---- 共享路径（输入框占满整行）----
    CreateWindowW(L"STATIC", L"共享路径:", WS_CHILD | WS_VISIBLE,
                  x, y + 3, 70, 20, hWnd, nullptr, g_hInst, nullptr);
    g_hPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                              x + 72, y, 200, 24, hWnd, (HMENU)IDC_PATH_EDIT, g_hInst, nullptr);
    CreateWindowW(L"BUTTON", L"浏览", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  x + 278, y, 50, 24, hWnd, (HMENU)IDC_BROWSE_BTN, g_hInst, nullptr);

    y += 34;
    // ---- 被动端口范围 ----
    g_pasvLabel = CreateWindowW(L"STATIC", L"被动端口:", WS_CHILD | WS_VISIBLE,
                  x, y + 3, 70, 20, hWnd, nullptr, g_hInst, nullptr);
    g_hPasvStart = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"50000",
                                   WS_CHILD | WS_VISIBLE | ES_NUMBER,
                                   x + 72, y, 70, 24, hWnd, (HMENU)IDC_PASV_START, g_hInst, nullptr);
    g_pasvDash = CreateWindowW(L"STATIC", L"-", WS_CHILD | WS_VISIBLE,
                  x + 146, y + 3, 10, 20, hWnd, nullptr, g_hInst, nullptr);
    g_hPasvEnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"50100",
                                 WS_CHILD | WS_VISIBLE | ES_NUMBER,
                                 x + 158, y, 70, 24, hWnd, (HMENU)IDC_PASV_END, g_hInst, nullptr);

    // 高级模式 / 精简模式切换：默认按配置（首次为高级模式）。
    // 按下 = 高级模式，弹起 = 精简模式；按钮文字同步显示当前模式。
    g_viewToggle = CreateWindowW(L"BUTTON",
                  g_config.compactMode ? L"精简模式" : L"高级模式",
                  WS_CHILD | WS_VISIBLE | BS_PUSHLIKE | BS_AUTOCHECKBOX,
                  x + 238, y, 78, 24, hWnd, (HMENU)IDC_VIEW_TOGGLE,
                  g_hInst, nullptr);
    SendMessage(g_viewToggle, BM_SETCHECK,
                g_config.compactMode ? BST_UNCHECKED : BST_CHECKED, 0);

    // 精简模式的三类权限（默认隐藏，切到精简模式时显示在被动端口那一行）
    g_chkCompactRead = CreateWindowW(L"BUTTON", L"读取",
                  WS_CHILD | BS_AUTOCHECKBOX,
                  x, y, 70, 24, hWnd, (HMENU)IDC_CHK_COMPACT_READ,
                  g_hInst, nullptr);
    g_chkCompactWrite = CreateWindowW(L"BUTTON", L"写入",
                  WS_CHILD | BS_AUTOCHECKBOX,
                  x + 76, y, 70, 24, hWnd, (HMENU)IDC_CHK_COMPACT_WRITE,
                  g_hInst, nullptr);
    g_chkCompactDelete = CreateWindowW(L"BUTTON", L"删除",
                  WS_CHILD | BS_AUTOCHECKBOX,
                  x + 152, y, 70, 24, hWnd, (HMENU)IDC_CHK_COMPACT_DELETE,
                  g_hInst, nullptr);
    SendMessage(g_chkCompactRead, BM_SETCHECK,
                g_config.compactPermRead ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_chkCompactWrite, BM_SETCHECK,
                g_config.compactPermWrite ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_chkCompactDelete, BM_SETCHECK,
                g_config.compactPermDelete ? BST_CHECKED : BST_UNCHECKED, 0);

    y += 38;
    // ---- 权限设置分组（一行一个 + 描述）----
    g_permGroup = CreateWindowW(L"BUTTON", L"权限设置",
                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                  x, y, kLeftWidth - 24, 200, hWnd, nullptr, g_hInst, nullptr);

    int cx = x + 12;
    int cy = y + 22;
    g_chkAll = CreateWindowW(L"BUTTON", L"全部权限  - 勾选/取消所有权限",
                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                             cx, cy, 290, 22, hWnd, (HMENU)IDC_CHK_ALL, g_hInst, nullptr);
    cy += 24;
    struct PermItem { const wchar_t* label; int id; HWND* hwnd; };
    PermItem items[] = {
        {L"读取      - 下载文件 (RETR)",        IDC_CHK_READ,   &g_chkRead},
        {L"写入      - 上传文件 (STOR/APPE)",   IDC_CHK_WRITE,  &g_chkWrite},
        {L"删除文件  - 删除文件 (DELE)",        IDC_CHK_DEL,    &g_chkDel},
        {L"创建目录  - 新建文件夹 (MKD)",       IDC_CHK_MKDIR,  &g_chkMkdir},
        {L"删除目录  - 删除文件夹 (RMD)",       IDC_CHK_RMDIR,  &g_chkRmdir},
        {L"重命名    - 改名/移动 (RNFR/RNTO)",  IDC_CHK_RENAME, &g_chkRename},
        {L"列目录    - 浏览目录 (LIST/NLST)",   IDC_CHK_LIST,   &g_chkList},
    };
    for (auto& it : items) {
        *it.hwnd = CreateWindowW(L"BUTTON", it.label,
                                 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                 cx, cy, 290, 22, hWnd, (HMENU)(INT_PTR)it.id, g_hInst, nullptr);
        cy += 22;
    }

    // 默认勾选：读取 + 列目录
    SendMessage(g_chkRead, BM_SETCHECK, BST_CHECKED, 0);
    SendMessage(g_chkList, BM_SETCHECK, BST_CHECKED, 0);

    y += 210;
    // ---- IP 限制分组 ----
    g_ipGroup = CreateWindowW(L"BUTTON", L"IP 限制（留空=允许所有）",
                  WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                  x, y, kLeftWidth - 24, 150, hWnd, nullptr, g_hInst, nullptr);

    g_ipEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                               x + 12, y + 22, 180, 24, hWnd, (HMENU)IDC_IP_EDIT, g_hInst, nullptr);
    CreateWindowW(L"BUTTON", L"添加", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  x + 198, y + 22, 50, 24, hWnd, (HMENU)IDC_IP_ADD, g_hInst, nullptr);
    CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  x + 252, y + 22, 50, 24, hWnd, (HMENU)IDC_IP_DEL, g_hInst, nullptr);

    g_ipList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
                               x + 12, y + 52, 290, 88, hWnd, (HMENU)IDC_IP_LIST, g_hInst, nullptr);

    y += 160;
    // ---- 启动按钮（大按钮，占满左侧剩余高度）----
    int toggleH = clientH - y - 12;
    if (toggleH < 60) toggleH = 60;
    g_hToggle = CreateWindowW(L"BUTTON", L"启动服务",
                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              x, y, kLeftWidth - 24, toggleH, hWnd, (HMENU)IDC_TOGGLE_BTN, g_hInst, nullptr);

    // ---- 右侧：分页栏（占满剩余空间）----
    int logX = kLeftWidth + 12;
    int logW = clientW - logX - 12;
    int logH = clientH - 12 - 12;
    g_hTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                             logX, 12, logW, logH, hWnd, (HMENU)IDC_TAB, g_hInst, nullptr);
    TCITEMW tie{};
    tie.mask = TCIF_TEXT;
    tie.pszText = (LPWSTR)L"运行日志";
    SendMessageW(g_hTab, TCM_INSERTITEMW, 0, (LPARAM)&tie);
    tie.pszText = (LPWSTR)L"快捷方式";
    SendMessageW(g_hTab, TCM_INSERTITEMW, 1, (LPARAM)&tie);
    tie.pszText = (LPWSTR)L"高级设置";
    SendMessageW(g_hTab, TCM_INSERTITEMW, 2, (LPARAM)&tie);
    tie.pszText = (LPWSTR)L"性能设置";
    SendMessageW(g_hTab, TCM_INSERTITEMW, 3, (LPARAM)&tie);
    tie.pszText = (LPWSTR)L"安全提醒";
    SendMessageW(g_hTab, TCM_INSERTITEMW, 4, (LPARAM)&tie);
    tie.pszText = (LPWSTR)L"伪客户端";
    SendMessageW(g_hTab, TCM_INSERTITEMW, 5, (LPARAM)&tie);
    tie.pszText = (LPWSTR)L"其他设置";
    SendMessageW(g_hTab, TCM_INSERTITEMW, 6, (LPARAM)&tie);
    tie.pszText = (LPWSTR)L"网络发现";
    SendMessageW(g_hTab, TCM_INSERTITEMW, 7, (LPARAM)&tie);
    // 8 个标签在默认宽度下会挤不下，固定为 66px 宽，刚好铺满分页栏。
    SendMessageW(g_hTab, TCM_SETITEMSIZE, 0, MAKELPARAM(66, 26));

    int pageX = logX + 6;
    int pageY = 12 + 34;   // 与标题栏留出间距
    int pageW = logW - 12;
    int pageH = logH - 38;
    g_pageX = pageX;
    g_pageY = pageY;
    g_pageW = pageW;
    g_pageH = pageH;

    // 性能设置 / 安全提醒 / 其他设置三页要求“整页白底”：分页控件自身的页体
    // 颜色由主题决定，这里在该区域叠一块白色底板。底板在分页控件之上、页面
    // 控件之下（先创建底板再创建页面控件），只在这三页显示。
    g_pagePanel = CreateWindowW(L"STATIC", L"",
        WS_CHILD | WS_CLIPSIBLINGS | SS_WHITERECT,
        pageX, pageY, pageW, pageH, hWnd, nullptr, g_hInst, nullptr);

    // 页0：运行日志
    // 顶部栏：一排双态按钮，分别控制各类日志的显示（默认全部按下=启用）
    {
        struct LogFilterBtn { const wchar_t* text; int id; };
        LogFilterBtn btns[] = {
            {L"上传日志", IDC_LOG_FILTER_UPLOAD},
            {L"下载日志", IDC_LOG_FILTER_DOWNLOAD},
            {L"客户端日志", IDC_LOG_FILTER_CLIENT},
            {L"其他日志", IDC_LOG_FILTER_OTHER},
            {L"警告日志", IDC_LOG_FILTER_WARN},
        };
        int bx = pageX;
        int bw = 84;   // 每个按钮宽度
        int gap = 6;   // 按钮间距
        for (int i = 0; i < 5; ++i) {
            // BS_PUSHLIKE + BS_AUTOCHECKBOX：外观像按钮，行为像复选框（双态）
            g_logFilterBtns[i] = CreateWindowW(L"BUTTON", btns[i].text,
                WS_CHILD | WS_VISIBLE | BS_PUSHLIKE | BS_AUTOCHECKBOX,
                bx, pageY, bw, 26, hWnd, (HMENU)(INT_PTR)btns[i].id, g_hInst, nullptr);
            SendMessage(g_logFilterBtns[i], BM_SETCHECK, BST_CHECKED, 0);  // 默认按下
            bx += bw + gap;
        }
    }
    // 日志文本框下移，给顶部按钮栏留出空间
    g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                             ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                             pageX, pageY + 32, pageW, pageH - 32, hWnd, (HMENU)IDC_LOG_EDIT, g_hInst, nullptr);

    // 页1：快捷方式连接本（ListView：名称 + 地址两列）
    g_bmList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                               WS_CHILD | WS_VSCROLL | LVS_REPORT | LVS_SINGLESEL |
                               LVS_SHOWSELALWAYS,
                               pageX, pageY, pageW, pageH - 40, hWnd, (HMENU)IDC_BM_LIST, g_hInst, nullptr);
    ListView_SetExtendedListViewStyle(g_bmList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    {
        // 三列宽度之和精确等于列表宽度，避免右侧出现空白“多余列”
        int total = pageW - 4;          // 预留边框
        int wName = total * 30 / 100;
        int wAddr = total * 50 / 100;
        int wStat = total - wName - wAddr;   // 余数给状态列，保证填满
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = (LPWSTR)L"名称";
        col.cx = wName;
        col.iSubItem = 0;
        SendMessageW(g_bmList, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
        col.pszText = (LPWSTR)L"地址";
        col.cx = wAddr;
        col.iSubItem = 1;
        SendMessageW(g_bmList, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);
        col.pszText = (LPWSTR)L"状态";
        col.cx = wStat;
        col.iSubItem = 2;
        SendMessageW(g_bmList, LVM_INSERTCOLUMNW, 2, (LPARAM)&col);
    }
    CreateWindowW(L"BUTTON", L"添加", WS_CHILD | BS_PUSHBUTTON,
                  pageX, pageY + pageH - 34, 80, 30, hWnd, (HMENU)IDC_BM_ADD, g_hInst, nullptr);
    CreateWindowW(L"BUTTON", L"删除", WS_CHILD | BS_PUSHBUTTON,
                  pageX + 90, pageY + pageH - 34, 80, 30, hWnd, (HMENU)IDC_BM_DEL, g_hInst, nullptr);
    CreateWindowW(L"BUTTON", L"编辑", WS_CHILD | BS_PUSHBUTTON,
                  pageX + 180, pageY + pageH - 34, 80, 30, hWnd, (HMENU)IDC_BM_EDIT, g_hInst, nullptr);
    CreateWindowW(L"BUTTON", L"打开连接", WS_CHILD | BS_PUSHBUTTON,
                  pageX + 270, pageY + pageH - 34, 100, 30, hWnd, (HMENU)IDC_BM_OPEN, g_hInst, nullptr);
    CreateWindowW(L"BUTTON", L"检查连通", WS_CHILD | BS_PUSHBUTTON,
                  pageX + 380, pageY + pageH - 34, 100, 30, hWnd, (HMENU)IDC_BM_CHECK, g_hInst, nullptr);

    // 页2：高级设置
    g_advEnable = CreateWindowW(L"BUTTON", L"启用高级权限",
                                WS_CHILD | BS_AUTOCHECKBOX | BS_FLAT,
                                pageX, pageY, pageW, 24, hWnd, (HMENU)IDC_ADV_ENABLE, g_hInst, nullptr);
    g_advHint = CreateWindowW(L"STATIC", L"右键可修改权限（R读 W写 D删文件 M建目录 X删目录 N重命名 L列目录）",
                              WS_CHILD | SS_LEFT,
                              pageX, pageY + 28, pageW, 20, hWnd, (HMENU)IDC_ADV_HINT, g_hInst, nullptr);
    g_advList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                WS_CHILD | WS_VSCROLL | LVS_REPORT |
                                LVS_SHOWSELALWAYS,
                                pageX, pageY + 52, pageW, pageH - 92, hWnd, (HMENU)IDC_ADV_LIST, g_hInst, nullptr);
    // 扩展样式：整行选中 + 网格线 + 支持多选（Ctrl/Shift）
    ListView_SetExtendedListViewStyle(g_advList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    // 子类化：支持键盘快捷键（Ctrl+A 全选、Ctrl+D 取消选择、空格 切换）
    g_advListOldProc = (WNDPROC)SetWindowLongPtrW(g_advList, GWLP_WNDPROC,
                                                  (LONG_PTR)AdvListSubclassProc);
    // 图标列表
    g_advImages = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 16, 16);
    ListView_SetImageList(g_advList, g_advImages, LVSIL_SMALL);
    // 列：名称 + 权限
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = (LPWSTR)L"名称";
    col.cx = pageW - 130;
    col.iSubItem = 0;
    SendMessageW(g_advList, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
    col.pszText = (LPWSTR)L"权限";
    col.cx = 120;
    col.iSubItem = 1;
    SendMessageW(g_advList, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);

    CreateWindowW(L"BUTTON", L"刷新列表", WS_CHILD | BS_PUSHBUTTON,
                  pageX, pageY + pageH - 34, 100, 30, hWnd, (HMENU)IDC_ADV_REFRESH, g_hInst, nullptr);
    CreateWindowW(L"BUTTON", L"全部允许", WS_CHILD | BS_PUSHBUTTON,
                  pageX + 110, pageY + pageH - 34, 100, 30, hWnd, (HMENU)IDC_ADV_ALLOW, g_hInst, nullptr);
    CreateWindowW(L"BUTTON", L"全部禁止", WS_CHILD | BS_PUSHBUTTON,
                  pageX + 220, pageY + pageH - 34, 100, 30, hWnd, (HMENU)IDC_ADV_DENY, g_hInst, nullptr);
    CreateWindowW(L"BUTTON", L"隐藏", WS_CHILD | BS_PUSHBUTTON,
                  pageX + 330, pageY + pageH - 34, 80, 30, hWnd, (HMENU)IDC_ADV_HIDE, g_hInst, nullptr);
    CreateWindowW(L"BUTTON", L"取消隐藏", WS_CHILD | BS_PUSHBUTTON,
                  pageX + 420, pageY + pageH - 34, 90, 30, hWnd, (HMENU)IDC_ADV_UNHIDE, g_hInst, nullptr);

    // 页3：性能设置（滑动条 + 输入框联动，自动保存）
    {
        int py = pageY;
        int lx = pageX;
        int ew = 70;                 // 输入框宽度
        int sldX = pageX + 90;       // 滑动条起点
        int sldW = pageW - 90 - ew - 10;  // 滑动条宽度
        int li = 0;
        auto mkLabel = [&](const wchar_t* t, int yy) {
            g_perfLabels[li++] = CreateWindowW(L"STATIC", t, WS_CHILD | SS_LEFT,
                          lx, yy + 3, 88, 20, hWnd, nullptr, g_hInst, nullptr);
        };
        auto mkEdit = [&](int id, int yy) -> HWND {
            return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                          WS_CHILD | ES_NUMBER | ES_RIGHT, sldX + sldW + 6, yy, ew, 24,
                          hWnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
        };
        auto mkSld = [&](int id, int yy) -> HWND {
            return CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                          WS_CHILD | TBS_HORZ | TBS_NOTICKS,
                          sldX, yy + 2, sldW, 22, hWnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
        };

        mkLabel(L"超时(秒):", py);
        g_sldTimeout = mkSld(IDC_SLD_TIMEOUT, py);
        g_perfTimeout = mkEdit(IDC_PERF_TIMEOUT, py);
        py += 34;

        g_perfNoDelay = CreateWindowW(L"BUTTON", L"启用 TCP_NODELAY（关闭 Nagle，降低延迟）",
                          WS_CHILD | BS_AUTOCHECKBOX, lx, py, pageW - 20, 22, hWnd,
                          (HMENU)IDC_PERF_NODELAY, g_hInst, nullptr);
        py += 34;

        mkLabel(L"内核缓冲(KB):", py);
        g_sldSockBuf = mkSld(IDC_SLD_SOCKBUF, py);
        g_perfSockBuf = mkEdit(IDC_PERF_SOCKBUF, py);
        py += 34;

        mkLabel(L"传输缓冲(KB):", py);
        g_sldAppBuf = mkSld(IDC_SLD_APPBUF, py);
        g_perfAppBuf = mkEdit(IDC_PERF_APPBUF, py);
        py += 34;

        mkLabel(L"单文件(MB):", py);
        g_sldMaxFile = mkSld(IDC_SLD_MAXFILE, py);
        g_perfMaxFile = mkEdit(IDC_PERF_MAXFILE, py);
        py += 34;

        mkLabel(L"单IP并发:", py);
        g_sldMaxConn = mkSld(IDC_SLD_MAXCONN, py);
        g_perfMaxConn = mkEdit(IDC_PERF_MAXCONN, py);
        py += 40;

        // 传输模式下拉框
        g_perfLabels[li++] = CreateWindowW(L"STATIC", L"传输模式:", WS_CHILD | SS_LEFT,
                      lx, py + 3, 88, 20, hWnd, nullptr, g_hInst, nullptr);
        g_perfTransferMode = CreateWindowW(L"COMBOBOX", L"",
            WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            sldX, py, 280, 200, hWnd, (HMENU)IDC_PERF_XFERMODE, g_hInst, nullptr);
        {
            const wchar_t* modes[] = {
                L"快速（续传开，哈希关）",
                L"均衡（续传开，哈希开）",
                L"安全（续传关，哈希开）",
                L"纯标准（续传关，哈希关，仍探测）",
                L"禁用（续传关，哈希关，不探测）"
            };
            for (auto m : modes)
                SendMessageW(g_perfTransferMode, CB_ADDSTRING, 0, (LPARAM)m);
            SendMessageW(g_perfTransferMode, CB_SETCURSEL,
                         (WPARAM)g_config.transferMode, 0);
        }
        py += 40;

        g_perfHint = CreateWindowW(L"STATIC",
                          L"提示：拖动滑动条或输入数值后自动保存（输入结束自动校验，非法值恢复默认）。设置为零=无上限。重启服务后生效。",
                          WS_CHILD | SS_LEFT, lx, py, pageW - 20, 40, hWnd,
                          (HMENU)IDC_PERF_HINT, g_hInst, nullptr);
        // 底部按钮靠右：重置 / 保存并排（与安全提醒、其他设置页一致）
        g_perfResetBtn = CreateWindowW(L"BUTTON", L"重置为默认", WS_CHILD | BS_PUSHBUTTON,
                      pageX + pageW - 250, pageY + pageH - 34, 120, 30, hWnd,
                      (HMENU)IDC_PERF_RESET, g_hInst, nullptr);
        // 右下角“保存”按钮（无修改时禁用）
        g_perfSaveBtn = CreateWindowW(L"BUTTON", L"保存", WS_CHILD | BS_PUSHBUTTON,
                      pageX + pageW - 120, pageY + pageH - 34, 120, 30, hWnd,
                      (HMENU)IDC_PERF_SAVE, g_hInst, nullptr);
        EnableWindow(g_perfSaveBtn, FALSE);
    }

    // 页4：安全提醒（敏感操作弹窗确认）
    {
        int py = pageY;
        int lx = pageX;
        g_secDelFile = CreateWindowW(L"BUTTON", L"删除文件时弹窗确认 (DELE)",
                          WS_CHILD | BS_AUTOCHECKBOX, lx, py, pageW - 20, 24, hWnd,
                          (HMENU)IDC_SEC_DELFILE, g_hInst, nullptr);
        py += 28;
        g_secDelDir = CreateWindowW(L"BUTTON", L"删除目录时弹窗确认 (RMD)",
                          WS_CHILD | BS_AUTOCHECKBOX, lx, py, pageW - 20, 24, hWnd,
                          (HMENU)IDC_SEC_DELDIR, g_hInst, nullptr);
        py += 28;
        g_secRename = CreateWindowW(L"BUTTON", L"重命名/移动时弹窗确认 (RNFR/RNTO)",
                          WS_CHILD | BS_AUTOCHECKBOX, lx, py, pageW - 20, 24, hWnd,
                          (HMENU)IDC_SEC_RENAME, g_hInst, nullptr);
        py += 28;
        g_secOverwrite = CreateWindowW(L"BUTTON", L"上传覆盖已存在文件时弹窗确认 (STOR)",
                          WS_CHILD | BS_AUTOCHECKBOX, lx, py, pageW - 20, 24, hWnd,
                          (HMENU)IDC_SEC_OVERWRITE, g_hInst, nullptr);
        py += 28;
        g_secDownload = CreateWindowW(L"BUTTON", L"下载文件时弹窗确认 (RETR)（默认关闭）",
                          WS_CHILD | BS_AUTOCHECKBOX, lx, py, pageW - 20, 24, hWnd,
                          (HMENU)IDC_SEC_DOWNLOAD, g_hInst, nullptr);
        py += 28;
        // 删除方式：下拉框（彻底删除 / 移动到回收站）
        CreateWindowW(L"STATIC", L"删除方式:", WS_CHILD | SS_LEFT,
                      lx, py + 3, 70, 20, hWnd, (HMENU)IDC_SEC_RECYCLE_LABEL, g_hInst, nullptr);
        g_secRecycle = CreateWindowW(L"COMBOBOX", L"",
                          WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                          lx + 72, py, 200, 200, hWnd,
                          (HMENU)IDC_SEC_RECYCLEMODE, g_hInst, nullptr);
        SendMessageW(g_secRecycle, CB_ADDSTRING, 0, (LPARAM)L"彻底删除（不可恢复）");
        SendMessageW(g_secRecycle, CB_ADDSTRING, 0, (LPARAM)L"移动到系统回收站（可还原）");
        SendMessage(g_secRecycle, CB_SETCURSEL, 0, 0);
        py += 34;

        // IP 名单模式：下拉框（白名单 / 黑名单）
        CreateWindowW(L"STATIC", L"IP 名单模式:", WS_CHILD | SS_LEFT,
                      lx, py + 3, 90, 20, hWnd, (HMENU)IDC_SEC_IPMODE_LABEL, g_hInst, nullptr);
        g_secIpMode = CreateWindowW(L"COMBOBOX", L"",
                          WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                          lx + 92, py, 200, 200, hWnd,
                          (HMENU)IDC_SEC_IPMODE, g_hInst, nullptr);
        SendMessageW(g_secIpMode, CB_ADDSTRING, 0, (LPARAM)L"白名单（仅允许列表内 IP）");
        SendMessageW(g_secIpMode, CB_ADDSTRING, 0, (LPARAM)L"黑名单（拒绝列表内 IP）");
        SendMessage(g_secIpMode, CB_SETCURSEL, 0, 0);
        py += 34;

        // 弹窗等待超时：滑动条 + 输入框（与性能设置页一致）
        int ew = 70;
        int sldX = lx + 130;
        int sldW = pageW - 130 - ew - 10;
        g_secTimeoutLabel = CreateWindowW(L"STATIC", L"弹窗等待超时(秒):",
                          WS_CHILD | SS_LEFT, lx, py + 3, 128, 20, hWnd,
                          (HMENU)IDC_SEC_TIMEOUT_LABEL, g_hInst, nullptr);
        g_sldSecTimeout = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                          WS_CHILD | TBS_HORZ | TBS_NOTICKS,
                          sldX, py + 2, sldW, 22, hWnd,
                          (HMENU)IDC_SLD_SECTIMEOUT, g_hInst, nullptr);
        g_secTimeout = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"30",
                          WS_CHILD | ES_NUMBER | ES_RIGHT, sldX + sldW + 6, py, ew, 24, hWnd,
                          (HMENU)IDC_SEC_TIMEOUT, g_hInst, nullptr);
        py += 40;

        // 底部按钮靠右：重置 / 保存并排
        g_secResetBtn = CreateWindowW(L"BUTTON", L"重置默认", WS_CHILD | BS_PUSHBUTTON,
                      pageX + pageW - 250, pageY + pageH - 34, 120, 30, hWnd,
                      (HMENU)IDC_SEC_RESET, g_hInst, nullptr);
        g_secSaveBtn = CreateWindowW(L"BUTTON", L"保存", WS_CHILD | BS_PUSHBUTTON,
                      pageX + pageW - 120, pageY + pageH - 34, 120, 30, hWnd,
                      (HMENU)IDC_SEC_SAVE, g_hInst, nullptr);
        EnableWindow(g_secSaveBtn, FALSE);
    }

    // 页5：客户端（内置 FTP 客户端）
    {
        int py = pageY;
        int lx = pageX;
        // 顶部两行（地址/端口、用户名/密码）单独铺一块白底
        g_cliTopY = py;
        g_cliTopH = 56;
        g_cliTopPanel = CreateWindowW(L"STATIC", L"",
                      WS_CHILD | SS_WHITERECT,
                      lx, py - 2, pageW, 60, hWnd, nullptr, g_hInst, nullptr);
        // 顶部栏：地址输入框 + 端口输入框 + 三个正方形按钮（进入/刷新/返回）
        g_cliHostLabel = CreateWindowW(L"STATIC", L"地址:", WS_CHILD | SS_LEFT,
                      lx, py + 3, 36, 20, hWnd, nullptr, g_hInst, nullptr);
        g_cliHost = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"127.0.0.1",
                      WS_CHILD | ES_AUTOHSCROLL,
                      lx + 38, py, 150, 26, hWnd, (HMENU)IDC_CLI_HOST, g_hInst, nullptr);
        // 回车等同于点击"进入"
        g_cliHostOldProc = (WNDPROC)SetWindowLongPtrW(g_cliHost, GWLP_WNDPROC,
                                                       (LONG_PTR)CliConnectSubclassProc);
        g_cliPortLabel = CreateWindowW(L"STATIC", L"端口:", WS_CHILD | SS_LEFT,
                      lx + 194, py + 3, 36, 20, hWnd, nullptr, g_hInst, nullptr);
        g_cliPort = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"21",
                      WS_CHILD | ES_NUMBER,
                      lx + 232, py, 60, 26, hWnd, (HMENU)IDC_CLI_PORT, g_hInst, nullptr);
        g_cliPortOldProc = (WNDPROC)SetWindowLongPtrW(g_cliPort, GWLP_WNDPROC,
                                                       (LONG_PTR)CliConnectSubclassProc);
        // 三个按钮：等分填充右侧剩余空间（进入 / 刷新 / 返回）
        int bx = lx + 300;
        int btnArea = pageW - 300;          // 剩余可用宽度
        int gap = 6;                        // 按钮间距
        int btnW = (btnArea - gap * 2) / 3; // 三等分
        if (btnW < 40) btnW = 40;
        const int backRight = bx + (btnW + gap) * 2 + btnW; // “返回”按钮右边缘
        CreateWindowW(L"BUTTON", L"进入 >", WS_CHILD | BS_PUSHBUTTON,
                      bx, py, btnW, 26, hWnd, (HMENU)IDC_CLI_ENTER, g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"刷新", WS_CHILD | BS_PUSHBUTTON,
                      bx + btnW + gap, py, btnW, 26, hWnd, (HMENU)IDC_CLI_REFRESH, g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"< 返回", WS_CHILD | BS_PUSHBUTTON,
                      bx + (btnW + gap) * 2, py, btnW, 26, hWnd, (HMENU)IDC_CLI_BACK, g_hInst, nullptr);
        py += 32;
        // 登录凭据：需要密码的 FTP 直接在“进入”前填这里
        g_cliUserLabel = CreateWindowW(L"STATIC", L"用户名:", WS_CHILD | SS_LEFT,
                      lx, py + 3, 60, 20, hWnd, nullptr, g_hInst, nullptr);
        g_cliUserEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"anonymous",
                      WS_CHILD | ES_AUTOHSCROLL,
                      lx + 62, py, 150, 24, hWnd, (HMENU)IDC_CLI_USER, g_hInst, nullptr);
        g_cliPassLabel = CreateWindowW(L"STATIC", L"密码:", WS_CHILD | SS_LEFT,
                      lx + 224, py + 3, 44, 20, hWnd, nullptr, g_hInst, nullptr);
        g_cliPassEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                      WS_CHILD | ES_AUTOHSCROLL | ES_PASSWORD,
                      lx + 270, py, 150, 24, hWnd, (HMENU)IDC_CLI_PASS, g_hInst, nullptr);
        // 登录方式：匿名 / 账号密码（选匿名时禁用上面两个输入框）
        // 右边缘与上一行“< 返回”按钮对齐；CBS_DROPDOWNLIST 的关闭高度
        // 由字体决定（比 24 高的编辑框多 4px），所以上移 4px 让底边齐平。
        const int authX = lx + 430;
        int authW = backRight - authX;
        if (authW < 90) authW = 90;
        g_cliAuthCombo = CreateWindowW(L"COMBOBOX", L"",
                      WS_CHILD | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST,
                      authX, py - 4, authW, 200, hWnd, (HMENU)IDC_CLI_AUTHMODE, g_hInst, nullptr);
        SendMessageW(g_cliAuthCombo, WM_SETFONT,
                     (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        // 下拉框的关闭高度由字体决定（约 28），比密码框(24)高一点；
        // 底边对齐在 ShowTabPage(5) 里做（那时窗口位置已经确定）。
        SendMessageW(g_cliAuthCombo, CB_ADDSTRING, 0, (LPARAM)L"匿名登录");
        SendMessageW(g_cliAuthCombo, CB_ADDSTRING, 0, (LPARAM)L"账号密码");
        SendMessageW(g_cliAuthCombo, CB_SETCURSEL, g_config.cliAnonymous ? 0 : 1, 0);
        ApplyClientAuthMode();
        py += 28;
        // 当前路径：可编辑输入框（面包屑路径），回车可跳转到指定目录
        g_cliPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"/",
                      WS_CHILD | ES_AUTOHSCROLL,
                      lx, py, pageW, 24, hWnd, (HMENU)IDC_CLI_PATH, g_hInst, nullptr);
        // 子类化：捕获回车（跳转）与 Esc（恢复当前目录）
        g_cliPathOldProc = (WNDPROC)SetWindowLongPtrW(g_cliPath, GWLP_WNDPROC,
                                                      (LONG_PTR)CliPathSubclassProc);
        py += 28;
        // 文件列表（ListView：名称 + 大小）
        g_cliList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                      WS_CHILD | WS_VSCROLL | LVS_REPORT | LVS_SHOWSELALWAYS,
                      lx, py, pageW, pageY + pageH - py, hWnd,
                      (HMENU)IDC_CLI_LIST, g_hInst, nullptr);
        ListView_SetExtendedListViewStyle(g_cliList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        // 子类化：支持键盘快捷键（Delete 删除、Enter 进入、Backspace 返回、F5 刷新）
        g_cliListOldProc = (WNDPROC)SetWindowLongPtrW(g_cliList, GWLP_WNDPROC,
                                                      (LONG_PTR)CliListSubclassProc);
        // 图标列表（复用系统图标列表）
        g_cliImages = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 16, 16);
        ListView_SetImageList(g_cliList, g_cliImages, LVSIL_SMALL);
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = (LPWSTR)L"名称";
        col.cx = pageW - 130;
        col.iSubItem = 0;
        SendMessageW(g_cliList, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
        col.pszText = (LPWSTR)L"大小";
        col.cx = 120;
        col.iSubItem = 1;
        SendMessageW(g_cliList, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);

        // 提前创建 FTP 客户端对象（拖入目标在窗口创建完成后注册）
        if (!g_client) g_client = std::make_unique<FtpClient>();
    }

    // 页6：其他设置（绑定网卡 + 默认文件名编码）
    {
        int py = pageY + 16;
        g_otherBindLabel = CreateWindowW(L"STATIC", L"服务监听网卡:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pageX, py + 4, 120, 22, hWnd, nullptr, g_hInst, nullptr);
        g_otherBind = CreateWindowW(L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP,
            pageX + 128, py, pageW - 132, 240, hWnd,
            (HMENU)IDC_OTHER_BIND, g_hInst, nullptr);

        py += 48;
        g_otherEncodingLabel = CreateWindowW(L"STATIC", L"默认编码:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pageX, py + 4, 120, 22, hWnd, nullptr, g_hInst, nullptr);
        g_otherEncoding = CreateWindowW(L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP,
            pageX + 128, py, pageW - 132, 180, hWnd,
            (HMENU)IDC_OTHER_ENCODING, g_hInst, nullptr);
        SendMessageW(g_otherEncoding, CB_ADDSTRING, 0,
                     (LPARAM)L"自动（推荐）");
        SendMessageW(g_otherEncoding, CB_ADDSTRING, 0,
                     (LPARAM)L"仅 UTF-8");
        SendMessageW(g_otherEncoding, CB_ADDSTRING, 0,
                     (LPARAM)L"仅 GBK（本地编码）");
        // 3/4：客户端把名字转坏（资源管理器上传中文名出现 '?'）时的兜底。
        // 会改写落盘文件名，因此不设为默认，需用户显式选择。
        SendMessageW(g_otherEncoding, CB_ADDSTRING, 0,
                     (LPARAM)L"兼容模式（尝试修复）：非法字符换全角字符");
        SendMessageW(g_otherEncoding, CB_ADDSTRING, 0,
                     (LPARAM)L"兼容模式（跳过解码）：非法字符换空格");

        // 兼容模式会改写落盘文件名，用三行小字说明适用场景。
        g_otherEncodingHint = CreateWindowW(L"STATIC",
            L"提示：兼容模式会改写落盘的文件名，仅在客户端把名字转坏时使用。\n"
            L"示例：资源管理器上传中文名出现 ? → 550 Cannot create file。\n"
            L"默认模式（自动 / UTF-8 / GBK）不会改写任何文件名。",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pageX, py + 30, pageW, 58, hWnd, nullptr, g_hInst, nullptr);

        // ---- 页脚：左下角“作者:HLBQ” + GitHub 图标 + “前往”链接 ----
        // 与底部按钮同一行（按钮 30 高、页脚 20 高，垂直居中对齐），
        // 右侧空间让给“重置默认 / 保存”。
        int btnY = pageY + pageH - 34;
        int footY = btnY + 5;
        g_otherAuthor = CreateWindowW(L"STATIC", L"作者:HLBQ",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pageX, footY, 90, 20, hWnd,
            (HMENU)IDC_OTHER_AUTHOR, g_hInst, nullptr);

        int iconX = pageX + 94;
        if (!g_ghIconHandle)
            g_ghIconHandle = CreateIconFromPngData(kGhPng, kGhPngSize, 18);
        g_ghIcon = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ICON | SS_NOTIFY,
            iconX, footY + 1, 18, 18, hWnd,
            (HMENU)IDC_OTHER_GH_ICON, g_hInst, nullptr);
        if (g_ghIconHandle)
            SendMessageW(g_ghIcon, STM_SETICON, (WPARAM)g_ghIconHandle, 0);

        g_ghGo = CreateWindowW(L"STATIC", L"前往",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
            iconX + 18 + 6, footY, 34, 20, hWnd,
            (HMENU)IDC_OTHER_GH_GO, g_hInst, nullptr);
        if (!g_ghLinkFont) g_ghLinkFont = CreateLinkFont();
        if (g_ghLinkFont)
            SendMessageW(g_ghGo, WM_SETFONT, (WPARAM)g_ghLinkFont, TRUE);

        // 第二个链接：个人网站图标 + “前往”（紧跟在 GitHub 那一组后面）
        int siteX = iconX + 18 + 6 + 34 + 20;
        if (!g_siteIconHandle)
            g_siteIconHandle = CreateIconFromPngData(kSitePng, kSitePngSize, 18);
        g_siteIcon = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ICON | SS_NOTIFY,
            siteX, footY + 1, 18, 18, hWnd,
            (HMENU)IDC_OTHER_SITE_ICON, g_hInst, nullptr);
        if (g_siteIconHandle)
            SendMessageW(g_siteIcon, STM_SETICON, (WPARAM)g_siteIconHandle, 0);

        g_siteGo = CreateWindowW(L"STATIC", L"前往",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
            siteX + 18 + 6, footY, 34, 20, hWnd,
            (HMENU)IDC_OTHER_SITE_GO, g_hInst, nullptr);
        if (g_ghLinkFont)
            SendMessageW(g_siteGo, WM_SETFONT, (WPARAM)g_ghLinkFont, TRUE);

        // 底部按钮靠右：重置 / 保存并排
        g_otherResetBtn = CreateWindowW(L"BUTTON", L"重置默认",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            pageX + pageW - 250, pageY + pageH - 34, 120, 30, hWnd,
            (HMENU)IDC_OTHER_RESET, g_hInst, nullptr);
        g_otherSaveBtn = CreateWindowW(L"BUTTON", L"保存",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            pageX + pageW - 120, pageY + pageH - 34, 120, 30, hWnd,
            (HMENU)IDC_OTHER_SAVE, g_hInst, nullptr);

        PopulateBindCombo();
        int enc = g_config.fileNameEncoding;
        if (enc < 0 || enc > 4) enc = 0;
        SendMessageW(g_otherEncoding, CB_SETCURSEL, enc, 0);
        g_otherDirty = false;
        UpdateOtherSaveState();
    }

    // 页7：网络发现
    {
        int py = pageY;
        g_discNetLabel = CreateWindowW(L"STATIC", L"扫描网段:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pageX, py + 4, 68, 22, hWnd, nullptr, g_hInst, nullptr);
        g_discNet = CreateWindowW(L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP,
            pageX + 68, py, 150, 200, hWnd,
            (HMENU)IDC_DISC_NET, g_hInst, nullptr);
        SendMessageW(g_discNet, CB_ADDSTRING, 0,
                     (LPARAM)L"C 类网段（/24）");
        SendMessageW(g_discNet, CB_ADDSTRING, 0,
                     (LPARAM)L"B 类网段（/16）");
        SendMessageW(g_discNet, CB_SETCURSEL, 0, 0);

        g_discPortLabel = CreateWindowW(L"STATIC", L"端口:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pageX + 228, py + 4, 40, 22, hWnd, nullptr, g_hInst, nullptr);
        g_discPorts = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"21;2121",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            pageX + 270, py, 130, 24, hWnd,
            (HMENU)IDC_DISC_PORTS, g_hInst, nullptr);
        g_discScanBtn = CreateWindowW(L"BUTTON", L"扫描",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            pageX + 410, py - 2, 84, 28, hWnd,
            (HMENU)IDC_DISC_SCAN, g_hInst, nullptr);

        g_discList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
            pageX, py + 38, pageW, pageH - 68, hWnd,
            (HMENU)IDC_DISC_LIST, g_hInst, nullptr);
        ListView_SetExtendedListViewStyle(g_discList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = (LPWSTR)L"IP 地址";
        col.cx = 170;
        col.iSubItem = 0;
        SendMessageW(g_discList, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
        col.pszText = (LPWSTR)L"端口";
        col.cx = 65;
        col.iSubItem = 1;
        SendMessageW(g_discList, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);
        col.pszText = (LPWSTR)L"服务";
        col.cx = 90;
        col.iSubItem = 2;
        SendMessageW(g_discList, LVM_INSERTCOLUMNW, 2, (LPARAM)&col);
        col.pszText = (LPWSTR)L"主机名";
        col.cx = 215;
        col.iSubItem = 3;
        SendMessageW(g_discList, LVM_INSERTCOLUMNW, 3, (LPARAM)&col);

        g_discStatus = CreateWindowW(L"STATIC",
            L"尚未扫描，选择网段和端口后点击“扫描”。",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            pageX, py + pageH - 22, pageW, 22, hWnd, nullptr, g_hInst, nullptr);
    }

    // 应用字体
    EnumChildWindows(hWnd, [](HWND child, LPARAM lp) -> BOOL {
        SendMessage(child, WM_SETFONT, (WPARAM)lp, TRUE);
        return TRUE;
    }, (LPARAM)hFont);

    // 启动/停止服务按钮：使用更大的粗体字，更醒目大气
    HFONT hFontToggle = CreateUiFont(26, true);
    SendMessage(g_hToggle, WM_SETFONT, (WPARAM)hFontToggle, TRUE);

    ShowTabPage(0);
}

// 判断某个控件是否属于“白底页”（性能设置 3 / 安全提醒 4 / 其他设置 6）。
// 这三页整页铺了白色底板，但静态文字、复选框默认按“按钮面色”绘制背景，
// 会在白底上留下灰色方块，因此需要让它们也返回白色画刷。
// 判定方式：当前选中的是这三页之一，且控件矩形落在分页内容区内。
static bool IsWhitePageCtl(HWND hCtl) {
    if (!hCtl || !g_hWnd || !g_hTab) return false;
    if (!IsWindowVisible(g_hTab)) return false;          // 精简模式没有分页
    if (GetParent(hCtl) != g_hWnd) return false;
    const int sel = (int)SendMessageW(g_hTab, TCM_GETCURSEL, 0, 0);
    RECT rc{};
    if (!GetWindowRect(hCtl, &rc)) return false;
    POINT pts[2] = {{rc.left, rc.top}, {rc.right, rc.bottom}};
    MapWindowPoints(HWND_DESKTOP, g_hWnd, pts, 2);
    // 客户端页：只有顶部两行（地址/端口、用户名/密码）铺了白底
    if (sel == 5) {
        return pts[0].x >= g_pageX && pts[0].y >= g_cliTopY - 2 &&
               pts[1].x <= g_pageX + g_pageW &&
               pts[1].y <= g_cliTopY + g_cliTopH + 2;
    }
    if (sel != 3 && sel != 4 && sel != 6) return false;
    return pts[0].x >= g_pageX && pts[0].y >= g_pageY &&
           pts[1].x <= g_pageX + g_pageW && pts[1].y <= g_pageY + g_pageH;
}

// 提示类文字（灰色小字）：高级设置页提示、性能页提示、安全页提示、其他设置页提示
static bool IsHintTextCtl(HWND hCtl) {
    return (g_advHint && hCtl == g_advHint) ||
           (g_perfHint && hCtl == g_perfHint) ||
           (g_secHint && hCtl == g_secHint) ||
           (g_otherEncodingHint && hCtl == g_otherEncodingHint);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        CreateControls(hWnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BROWSE_BTN:  BrowseFolder(); return 0;
        case IDC_TOGGLE_BTN:  ToggleServer(); return 0;
        case IDC_CHK_ALL:     OnCheckAll(); return 0;
        // 单个权限勾选框：联动“全部权限”
        case IDC_CHK_READ:
        case IDC_CHK_WRITE:
        case IDC_CHK_DEL:
        case IDC_CHK_MKDIR:
        case IDC_CHK_RMDIR:
        case IDC_CHK_RENAME:
        case IDC_CHK_LIST:
            if (HIWORD(wParam) == BN_CLICKED) OnPermItemChanged();
            return 0;
        // 精简模式的三类权限：变化后立即同步到运行中的服务
        case IDC_CHK_COMPACT_READ:
        case IDC_CHK_COMPACT_WRITE:
        case IDC_CHK_COMPACT_DELETE:
            if (HIWORD(wParam) == BN_CLICKED) PushPermissionsToServer();
            return 0;
        case IDC_IP_ADD:      AddIp(); return 0;
        case IDC_IP_DEL:      DelIp(); return 0;
        case IDC_BM_ADD:      AddBookmarkDialog(); return 0;
        case IDC_BM_DEL:      DelBookmark(); return 0;
        case IDC_BM_EDIT:     EditBookmark(); return 0;
        case IDC_BM_OPEN:     OpenBookmark(); return 0;
        case IDC_BM_CHECK:    CheckBookmarkConnect(); return 0;
        case IDC_VIEW_TOGGLE:
            if (HIWORD(wParam) == BN_CLICKED) {
                // 按下 = 高级模式，弹起 = 精简模式
                bool compact =
                    SendMessage(g_viewToggle, BM_GETCHECK, 0, 0) != BST_CHECKED;
                ApplyCompactMode(compact);
            }
            return 0;
        case IDC_ADV_ENABLE:  OnAdvEnableToggle(); return 0;
        case IDC_ADV_REFRESH: RefreshAdvList(); return 0;
        case IDC_ADV_ALLOW:   SetSelectedPermAll(true); return 0;
        case IDC_ADV_DENY:    SetSelectedPermAll(false); return 0;
        case IDC_ADV_HIDE:    HideSelectedAdv(); return 0;
        case IDC_ADV_UNHIDE:  UnhideSelectedAdv(); return 0;
        case IDC_PERF_NODELAY: CommitPerfFromUi(); return 0;
        case IDC_PERF_SAVE:   SavePerfToConfig(); return 0;
        case IDC_SEC_SAVE:    SaveSecurityToConfig(); return 0;
        case IDC_SEC_RESET: {
            // 恢复默认值（标记为未保存，需点击“保存”才写入配置）
            ApplySecurityToUi(SecurityConfig());
            g_secDirty = true;
            if (g_secSaveBtn) EnableWindow(g_secSaveBtn, TRUE);
            return 0;
        }
        // 其他设置页：绑定网卡 / 文件名编码
        case IDC_OTHER_BIND:
            if (HIWORD(wParam) == CBN_SELCHANGE) OnOtherBindChanged();
            return 0;
        case IDC_OTHER_ENCODING:
            if (HIWORD(wParam) == CBN_SELCHANGE) OnOtherEncodingChanged();
            return 0;
        case IDC_OTHER_SAVE:  OnOtherSave(); return 0;
        case IDC_OTHER_RESET: OnOtherReset(); return 0;
        // 页脚：点 GitHub 图标或“前往”都用默认浏览器打开项目主页
        case IDC_OTHER_GH_ICON:
        case IDC_OTHER_GH_GO:
            if (HIWORD(wParam) == STN_CLICKED)
                ShellExecuteW(nullptr, L"open", kGitHubUrl,
                              nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        // 页脚：第二个链接，打开个人网站
        case IDC_OTHER_SITE_ICON:
        case IDC_OTHER_SITE_GO:
            if (HIWORD(wParam) == STN_CLICKED)
                ShellExecuteW(nullptr, L"open", kSiteUrl,
                              nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        // 网络发现页
        case IDC_DISC_SCAN:   StartNetworkScan(); return 0;
        // 客户端页：进入 / 刷新 / 返回
        case IDC_CLI_ENTER: {
            std::string host = GetTextUtf8(g_cliHost);
            int port = atoi(GetTextUtf8(g_cliPort).c_str());
            if (host.empty() || port < 1 || port > 65535) {
                MessageBoxW(g_hWnd, L"请输入有效的地址和端口", L"提示", MB_ICONWARNING);
                return 0;
            }
            // 登录方式为“匿名”时，一律用 anonymous + 空密码
            const bool anonymous = g_cliAuthCombo &&
                SendMessageW(g_cliAuthCombo, CB_GETCURSEL, 0, 0) == 0;
            if (anonymous) {
                g_cliUser = "anonymous";
                g_cliPass.clear();
            } else {
                g_cliUser = g_cliUserEdit ? GetTextUtf8(g_cliUserEdit) : g_cliUser;
                g_cliPass = g_cliPassEdit ? GetTextUtf8(g_cliPassEdit) : g_cliPass;
                if (g_cliUser.empty()) g_cliUser = "anonymous";
            }
            // 记住本次输入，下次启动自动回填（明文保存在 config.ini）
            g_config.cliUser = g_cliUser;
            g_config.cliPass = g_cliPass;
            g_config.cliAnonymous = anonymous;
            g_config.Save(g_configPath);
            ClientConnectAndList(host, port, g_cliUser, g_cliPass);
            return 0;
        }
        case IDC_CLI_AUTHMODE:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                const bool anonymous =
                    SendMessageW(g_cliAuthCombo, CB_GETCURSEL, 0, 0) == 0;
                g_config.cliAnonymous = anonymous;
                if (!anonymous) {
                    // 从匿名切到账号密码时，把输入框里留下的值同步进配置
                    if (g_cliUserEdit) g_config.cliUser = GetTextUtf8(g_cliUserEdit);
                    if (g_cliPassEdit) g_config.cliPass = GetTextUtf8(g_cliPassEdit);
                }
                ApplyClientAuthMode();
                g_config.Save(g_configPath);
            }
            return 0;
        case IDC_CLI_REFRESH: ClientRefresh(); return 0;
        case IDC_CLI_BACK:    ClientGoUp(); return 0;
        // 路径输入框：回车跳转到指定目录
        case IDC_CLI_PATH:
            if (HIWORD(wParam) == EN_KILLFOCUS) {
                // 失焦时把路径恢复为当前实际目录，避免显示与真实目录不一致
                if (g_client) {
                    SetWindowTextW(g_cliPath,
                        Utf8ToWide(g_client->CurrentDir()).c_str());
                }
            }
            return 0;
        // 运行日志页：分类过滤双态按钮（按下=启用，弹起=关闭）
        case IDC_LOG_FILTER_UPLOAD:
        case IDC_LOG_FILTER_DOWNLOAD:
        case IDC_LOG_FILTER_CLIENT:
        case IDC_LOG_FILTER_OTHER:
        case IDC_LOG_FILTER_WARN: {
            if (HIWORD(wParam) == BN_CLICKED) {
                int id = LOWORD(wParam);
                HWND btn = (HWND)lParam;
                bool on = SendMessage(btn, BM_GETCHECK, 0, 0) == BST_CHECKED;
                switch (id) {
                case IDC_LOG_FILTER_UPLOAD:   g_logUpload.store(on);   break;
                case IDC_LOG_FILTER_DOWNLOAD: g_logDownload.store(on); break;
                case IDC_LOG_FILTER_CLIENT:   g_logClient.store(on);   break;
                case IDC_LOG_FILTER_OTHER:    g_logOther.store(on);    break;
                case IDC_LOG_FILTER_WARN:     g_logWarn.store(on);     break;
                }
            }
            return 0;
        }
        case IDC_SEC_DELFILE:
        case IDC_SEC_DELDIR:
        case IDC_SEC_RENAME:
        case IDC_SEC_OVERWRITE:
        case IDC_SEC_DOWNLOAD:
            if (HIWORD(wParam) == BN_CLICKED) {
                g_secDirty = true;
                if (g_secSaveBtn) EnableWindow(g_secSaveBtn, TRUE);
            }
            return 0;
        // 删除方式 / IP 名单模式下拉框：选择变化时标记未保存
        case IDC_SEC_RECYCLEMODE:
        case IDC_SEC_IPMODE:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                g_secDirty = true;
                if (g_secSaveBtn) EnableWindow(g_secSaveBtn, TRUE);
            }
            return 0;
        case IDC_SEC_TIMEOUT:
            if (HIWORD(wParam) == EN_CHANGE) {
                // 输入框 -> 滑动条实时同步
                int v = atoi(GetTextUtf8(g_secTimeout).c_str());
                if (v < kRangeSecTimeout.min) v = kRangeSecTimeout.min;
                if (v > kRangeSecTimeout.max) v = kRangeSecTimeout.max;
                SendMessage(g_sldSecTimeout, TBM_SETPOS, TRUE, v);
                g_secDirty = true;
                if (g_secSaveBtn) EnableWindow(g_secSaveBtn, TRUE);
            } else if (HIWORD(wParam) == EN_KILLFOCUS) {
                // 失焦时校验并回写
                SecurityConfig s = ReadSecurityFromUi();
                ApplySecurityToUi(s);
            }
            return 0;
        case IDC_PERF_RESET: {
            // 重置为默认值（标记为未保存，需点击“保存”才写入配置）
            ApplyPerfToUi(PerfConfig());
            MarkPerfDirty();
            return 0;
        }
        // 输入框：内容变化时实时同步滑动条；失焦时校验并保存
        case IDC_PERF_TIMEOUT:
        case IDC_PERF_SOCKBUF:
        case IDC_PERF_APPBUF:
        case IDC_PERF_MAXFILE:
        case IDC_PERF_MAXCONN:
            if (HIWORD(wParam) == EN_CHANGE) SyncSliderFromEdit(LOWORD(wParam));
            else if (HIWORD(wParam) == EN_KILLFOCUS) CommitPerfFromUi();
            return 0;
        case IDC_PERF_XFERMODE:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                MarkPerfDirty();
            }
            return 0;
        }
        return 0;

    case WM_NOTIFY: {
        LPNMHDR nh = (LPNMHDR)lParam;
        if (nh->idFrom == IDC_TAB && nh->code == TCN_SELCHANGE) {
            int sel = (int)SendMessage(g_hTab, TCM_GETCURSEL, 0, 0);
            // 离开性能页时，若有未保存修改则还原为已保存的配置
            if (sel != 3 && g_perfDirty) {
                ApplyPerfToUi(g_config.perf);
                g_perfDirty = false;
                if (g_perfSaveBtn) EnableWindow(g_perfSaveBtn, FALSE);
            }
            // 离开安全页时，若有未保存修改则还原为已保存的配置
            if (sel != 4 && g_secDirty) {
                ApplySecurityToUi(g_config.security);
                g_secDirty = false;
                if (g_secSaveBtn) EnableWindow(g_secSaveBtn, FALSE);
            }
            ShowTabPage(sel);
        }
        // 快捷方式列表：双击打开
        if (nh->idFrom == IDC_BM_LIST && nh->code == NM_DBLCLK) {
            OpenBookmark();
        }
        // 网络发现结果：双击默认在伪客户端中打开
        if (nh->idFrom == IDC_DISC_LIST && nh->code == NM_DBLCLK) {
            OpenDiscoveryInClient(GetSelectedDiscovery());
        }
        // 客户端列表：双击进入目录
        if (nh->idFrom == IDC_CLI_LIST && nh->code == NM_DBLCLK) {
            ClientEnterSelected();
        }
        // 客户端列表：开始拖拽（把远程文件拖到资源管理器）
        if (nh->idFrom == IDC_CLI_LIST && nh->code == LVN_BEGINDRAG) {
            // 在锁内完成数据收集（g_client/g_cliEntries 均为共享状态），
            // 收集完释放锁后再进入 DoDragDrop 消息循环，避免死锁。
            std::vector<FtpDragItem> items;
            bool hasDir = false;
            {
                std::lock_guard<std::mutex> lk(g_clientMutex);
                if (g_client && g_client->IsConnected()) {
                    std::string cwd = g_client->CurrentDir();
                    if (cwd.empty() || cwd.back() != '/') cwd += "/";
                    int sel = -1;
                    std::lock_guard<std::mutex> lk2(g_cliEntriesMutex);
                    while ((sel = (int)SendMessage(g_cliList, LVM_GETNEXTITEM,
                            (WPARAM)sel, MAKELPARAM(LVNI_SELECTED, 0))) >= 0) {
                        if (sel >= (int)g_cliEntries.size()) continue;
                        const auto& e = g_cliEntries[sel];
                        if (e.isDir) hasDir = true;
                        FtpDragItem it;
                        it.remotePath = cwd + e.name;
                        it.name = Utf8ToWide(e.name);
                        it.isDir = e.isDir;
                        it.size = e.size;
                        items.push_back(it);
                    }
                }
            }
            if (hasDir) {
                // 文件夹拖出会得到空目录（资源管理器不会为目录请求
                // FILECONTENTS 流），内容会静默丢失，故阻止并提示替代方案
                MessageBoxW(g_hWnd,
                    L"暂不支持把文件夹拖出到资源管理器（会得到空目录）。\n"
                    L"请使用右键菜单中的“下载”来下载文件夹。",
                    L"FtpTool", MB_OK | MB_ICONINFORMATION);
                items.clear();
            }
            if (!items.empty()) {
                StartFtpDrag(g_hWnd, g_client.get(), items, &g_clientMutex);
            }
            return 0;
        }
        // 高级设置列表：隐藏项整行显示为灰色
        if (nh->idFrom == IDC_ADV_LIST && nh->code == NM_CUSTOMDRAW) {
            LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lParam;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                // 读取该行权限列文字，若为 <HIDE> 则整行灰色
                wchar_t buf[64] = {0};
                LVITEMW item{};
                item.iSubItem = 1;
                item.pszText = buf;
                item.cchTextMax = 64;
                SendMessageW(g_advList, LVM_GETITEMTEXTW,
                             (WPARAM)cd->nmcd.dwItemSpec, (LPARAM)&item);
                if (wcscmp(buf, L"<HIDE>") == 0) {
                    cd->clrText = RGB(160, 160, 160);
                }
                return CDRF_DODEFAULT;
            }
        }
        // 快捷方式列表：状态列彩色绘制（开放=绿，关闭=红）
        if (nh->idFrom == IDC_BM_LIST && nh->code == NM_CUSTOMDRAW) {
            LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lParam;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) return CDRF_NOTIFYSUBITEMDRAW;
            if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                if (cd->iSubItem == 2) {
                    wchar_t buf[64] = {0};
                    LVITEMW item{};
                    item.iSubItem = 2;
                    item.pszText = buf;
                    item.cchTextMax = 64;
                    SendMessageW(g_bmList, LVM_GETITEMTEXTW,
                                 (WPARAM)cd->nmcd.dwItemSpec, (LPARAM)&item);
                    if (wcscmp(buf, L"开放") == 0)
                        cd->clrText = RGB(0, 150, 0);
                    else if (wcscmp(buf, L"关闭") == 0)
                        cd->clrText = RGB(200, 0, 0);
                    else if (wcscmp(buf, L"检测中") == 0)
                        cd->clrText = RGB(150, 150, 150);
                }
                return CDRF_DODEFAULT;
            }
        }
        return 0;
    }

    case WM_SETCURSOR: {
        // 鼠标移到页脚的 GitHub 图标 / “前往”上时显示手型，提示可点击
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt{};
            GetCursorPos(&pt);
            HWND hOver = WindowFromPoint(pt);
            if (hOver && (hOver == g_ghIcon || hOver == g_ghGo ||
                          hOver == g_siteIcon || hOver == g_siteGo)) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        // 高级设置页的复选框/提示文字，以及性能设置、安全提醒、其他设置三页
        // 的全部文字控件：背景刷白（这三页整页是白底），提示文字用灰色。
        HWND hCtl = (HWND)lParam;
        if (hCtl == g_advEnable || hCtl == g_advHint || IsWhitePageCtl(hCtl)) {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetBkColor(hdc, RGB(255, 255, 255));
            if (IsHintTextCtl(hCtl)) SetTextColor(hdc, RGB(128, 128, 128));
            // 其他设置页页脚：作者信息用更淡的灰，GitHub 入口用经典链接蓝
            else if (g_otherAuthor && hCtl == g_otherAuthor)
                SetTextColor(hdc, RGB(158, 158, 158));
            else if (g_ghGo && hCtl == g_ghGo)
                SetTextColor(hdc, RGB(0, 102, 204));
            else if (g_siteGo && hCtl == g_siteGo)
                SetTextColor(hdc, RGB(0, 102, 204));
            static HBRUSH hWhite = CreateSolidBrush(RGB(255, 255, 255));
            return (LRESULT)hWhite;
        }
        break;
    }

    case WM_HSCROLL: {
        // 拖动滑动条：实时同步到对应输入框并保存
        HWND hSld = (HWND)lParam;
        HWND edit = nullptr;
        if (hSld == g_sldTimeout) edit = g_perfTimeout;
        else if (hSld == g_sldSockBuf) edit = g_perfSockBuf;
        else if (hSld == g_sldAppBuf) edit = g_perfAppBuf;
        else if (hSld == g_sldMaxFile) edit = g_perfMaxFile;
        else if (hSld == g_sldMaxConn) edit = g_perfMaxConn;
        if (edit) {
            int pos = (int)SendMessage(hSld, TBM_GETPOS, 0, 0);
            g_perfSyncing = true;
            SetWindowTextW(edit, Utf8ToWide(std::to_string(pos)).c_str());
            g_perfSyncing = false;
            CommitPerfFromUi();
            return 0;
        }
        // 安全页超时滑动条
        if (hSld == g_sldSecTimeout) {
            int pos = (int)SendMessage(hSld, TBM_GETPOS, 0, 0);
            SetWindowTextW(g_secTimeout, Utf8ToWide(std::to_string(pos)).c_str());
            g_secDirty = true;
            if (g_secSaveBtn) EnableWindow(g_secSaveBtn, TRUE);
            return 0;
        }
        break;
    }

    case WM_APP_LOG:
        FlushPendingLog();
        return 0;

    // 传输进度弹窗消息（创建/刷新/销毁进度窗口）
    case WM_APP + 20:   // BEGIN
    case WM_APP + 21:   // UPDATE（工作线程主动请求刷新）
    case WM_APP + 22:   // END
    case WM_APP + 23:   // FILE（切换当前文件名）
    case WM_APP + 24:   // ACTION（切换操作类型）
        if (ProgressDialog::HandleMessage(hWnd, msg, wParam, lParam)) return 0;
        break;

    case WM_APP_SCAN_RESULT: {
        auto* r = (ScanResultMsg*)lParam;
        if (r) {
            AddDiscoveryResult(r->ip, r->port, r->host);
            delete r;
            if (g_discStatus) {
                std::wstring s = L"正在扫描... 已发现 " +
                                 std::to_wstring(g_discEntries.size()) + L" 个开放端口";
                SetWindowTextW(g_discStatus, s.c_str());
            }
        }
        return 0;
    }

    case WM_APP_SCAN_PROGRESS: {
        auto* p = (ScanProgressMsg*)lParam;
        if (p) {
            UpdateScanProgress(*p);
            delete p;
        }
        return 0;
    }

    case WM_APP_SCAN_DONE: {
        g_scanRunning.store(false);
        if (g_discScanBtn) EnableWindow(g_discScanBtn, TRUE);
        if (g_scanDlg) DestroyWindow(g_scanDlg);
        int reason = (int)wParam;
        std::wstring s;
        if (reason == 2) {
            s = L"扫描失败：无法初始化网络。";
        } else if (reason == 1) {
            s = L"扫描已终止，已发现 " +
                std::to_wstring(g_discEntries.size()) + L" 个开放端口。";
        } else {
            s = L"扫描完成，已发现 " +
                std::to_wstring(g_discEntries.size()) + L" 个开放端口。";
        }
        if (g_discStatus) SetWindowTextW(g_discStatus, s.c_str());
        g_scanStop.store(false);
        return 0;
    }

    case WM_APP_BM_STATUS: {
        // 后台线程完成一条连通检测：更新状态列
        int* payload = (int*)lParam;
        if (payload) {
            int row = payload[0];
            int st = payload[1];
            delete[] payload;
            if (st == 1) SetBookmarkStatus(row, L"开放", 1);
            else         SetBookmarkStatus(row, L"关闭", 2);
        }
        return 0;
    }

    case WM_APP_CONFIRM: {
        // lParam 是“信封”：内含 shared_ptr<ConfirmRequest> 的堆副本。
        // 拷贝到本地后立即删除信封；即使客户端线程已等待超时放弃，
        // 本地副本仍能安全存活（修复原裸指针的 use-after-free）。
        auto* holder = (std::shared_ptr<ConfirmRequest>*)lParam;
        if (holder) {
            std::shared_ptr<ConfirmRequest> req = *holder;
            delete holder;
            HandleConfirmRequest(req);
            if (req->doneEvent) SetEvent(req->doneEvent);
        }
        return 0;
    }

    case WM_APP_CLI_REFRESH:
        // 客户端后台操作完成：用缓存重绘文件列表
        RefreshClientList();
        return 0;

    case WM_APP_CLI_RELIST:
        // 删除/上传/下载/新建/重命名后：重新从服务器拉取目录再重绘，
        // 确保列表显示的是服务器最新状态（而不是旧缓存）。
        ClientRefresh();
        return 0;

    case WM_APP_CLI_STATUS: {
        // 客户端后台操作结果：成功写日志；失败弹出错误框。
        // 之前只写日志，而且“警告日志”过滤关闭时连日志都会被吃掉，
        // 所以权限不足/连接失败/550 等错误用户完全看不到。
        std::string* msg = (std::string*)lParam;
        if (!msg) return 0;
        bool ok = wParam != 0;
        // 错误用 level 2：即使“警告日志”开关关闭也始终记录
        LogCallbackFn(ok ? 0 : 2, *msg);
        if (!ok) {
            std::wstring text = ErrorTextToWide(*msg);
            if (text.empty()) text = L"未知错误（详细信息见运行日志）";
            if (text.size() > 800) {
                text = text.substr(0, 800);
                text += L"…\n（完整信息见运行日志）";
            }
            g_errorQueue.push_back(text);
            if (!g_errorPopupShowing && g_hWnd) {
                g_errorPopupShowing = true;
                while (!g_errorQueue.empty() && IsWindow(g_hWnd)) {
                    std::wstring t = g_errorQueue.front();
                    g_errorQueue.pop_front();
                    ShowErrorDialog(t);
                }
                g_errorPopupShowing = false;
                if (!IsWindow(g_hWnd)) g_errorQueue.clear();
            }
        }
        delete msg;
        return 0;
    }

    case WM_CONTEXTMENU: {
        HWND hTarget = (HWND)wParam;
        // 高级设置列表右键菜单
        if (hTarget == g_advList) {
            ShowPermMenu();
            return 0;
        }
        // 客户端文件列表右键菜单
        if (hTarget == g_cliList) {
            ShowClientMenu();
            return 0;
        }
        // 快捷方式列表右键菜单
        if (hTarget == g_bmList) {
            ShowBookmarkMenu();
            return 0;
        }
        // 网络发现结果右键菜单
        if (hTarget == g_discList) {
            ShowDiscoveryMenu();
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        g_scanStop.store(true);
        if (g_scanDlg) DestroyWindow(g_scanDlg);
        SaveConfigFromUi();
        StopServerSync();   // 退出前必须确保停止完成，防止后台线程 UAF
        DestroyWindow(hWnd);
        return 0;

    case WM_APP_STOP_DONE:
        // 后台停止服务完成：恢复按钮状态
        NotifyStopDone();
        return 0;

    case WM_DESTROY:
        // 注销 OLE 拖放目标：RegisterDragDrop 会持有 IDropTarget 引用，
        // 不显式 Revoke 会在窗口销毁后残留悬挂的拖放目标
        RevokeFtpDropTarget(hWnd);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace

int RunGui(HINSTANCE hInstance, int nCmdShow) {
    g_hInst = hInstance;
    g_localIp = FtpServer::GetLocalIp();

    // 初始化 OLE（WIC 解码 PNG 图标 + 拖放都需要）
    OleInitialize(nullptr);

    g_configPath = AppConfig::DefaultPath();
    g_config = AppConfig::Load(g_configPath);
    // 配置文件不存在（丢失/首次运行）时立即主动创建默认配置：
    // 原先直到关闭时才写入，中途异常退出配置就丢了
    {
        std::error_code ecCfg;
        if (!fs::exists(fs::path(Utf8ToWide(g_configPath)), ecCfg))
            g_config.Save(g_configPath);
    }

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    // 从内嵌 PNG 动态创建窗口图标（大/小两种尺寸）
    HICON hIconBig = CreateIconFromPng(GetSystemMetrics(SM_CXICON));
    HICON hIconSm = CreateIconFromPng(GetSystemMetrics(SM_CXSMICON));
    if (!hIconBig) hIconBig = LoadIcon(nullptr, IDI_APPLICATION);
    if (!hIconSm) hIconSm = hIconBig;

    const wchar_t* clsName = L"FtpToolWndClass";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = clsName;
    wc.hIcon = hIconBig;
    wc.hIconSm = hIconSm;
    RegisterClassExW(&wc);

    // 固定窗口大小（不可调整）
    RECT rc = {0, 0, 920, 570};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    g_hWnd = CreateWindowExW(0, clsName, L"Ftp Tool",
                             WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             nullptr, nullptr, hInstance, nullptr);
    if (!g_hWnd) return 1;

    // 再次显式设置窗口图标（确保任务栏/标题栏都显示）
    SendMessage(g_hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    SendMessage(g_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);

    // 注册“拖入上传”目标：注册在主窗口上，命中判定为客户端文件列表。
    // 从资源管理器把本地文件/文件夹拖到客户端列表上即可上传到当前目录。
    // 注意：必须在窗口创建完成（g_hWnd 已赋值）后注册，否则 WM_CREATE
    //       期间 g_hWnd 仍为空，会导致注册失败。
    if (g_client) {
        RegisterFtpDropTarget(g_hWnd, g_cliList, g_client.get(), &g_clientMutex);
    }

    // 用配置填充界面
    SetWindowTextW(g_hPort, Utf8ToWide(std::to_string(g_config.port)).c_str());
    if (!g_config.sharePath.empty())
        SetWindowTextW(g_hPath, Utf8ToWide(g_config.sharePath).c_str());
    SetWindowTextW(g_hPasvStart, Utf8ToWide(std::to_string(g_config.pasvStart)).c_str());
    SetWindowTextW(g_hPasvEnd, Utf8ToWide(std::to_string(g_config.pasvEnd)).c_str());
    SendMessage(g_chkRead,   BM_SETCHECK, g_config.perms.read   ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_chkWrite,  BM_SETCHECK, g_config.perms.write  ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_chkDel,    BM_SETCHECK, g_config.perms.del    ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_chkMkdir,  BM_SETCHECK, g_config.perms.mkdir  ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_chkRmdir,  BM_SETCHECK, g_config.perms.rmdir  ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_chkRename, BM_SETCHECK, g_config.perms.rename ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_chkList,   BM_SETCHECK, g_config.perms.list   ? BST_CHECKED : BST_UNCHECKED, 0);
    for (const auto& ip : g_config.allowedIps) {
        SendMessageW(g_ipList, LB_ADDSTRING, 0, (LPARAM)Utf8ToWide(ip).c_str());
    }
    RefreshBookmarkList();

    // 高级模式状态
    SendMessage(g_advEnable, BM_SETCHECK,
                g_config.advancedMode ? BST_CHECKED : BST_UNCHECKED, 0);
    OnAdvEnableToggle();

    // 性能设置页（同时设置滑动条范围与位置）
    ApplyPerfToUi(g_config.perf);

    // 安全提醒页
    ApplySecurityToUi(g_config.security);

    // 客户端页：回填上次连接的地址与端口
    if (g_cliHost) SetWindowTextW(g_cliHost, Utf8ToWide(g_config.cliHost).c_str());
    if (g_cliPort) SetWindowTextW(g_cliPort,
                                  Utf8ToWide(std::to_string(g_config.cliPort)).c_str());
    // 客户端页：回填上次使用的登录凭据
    g_cliUser = g_config.cliUser.empty() ? "anonymous" : g_config.cliUser;
    g_cliPass = g_config.cliPass;
    if (g_cliUserEdit) SetWindowTextW(g_cliUserEdit, Utf8ToWide(g_cliUser).c_str());
    if (g_cliPassEdit) SetWindowTextW(g_cliPassEdit, Utf8ToWide(g_cliPass).c_str());
    // 恢复登录方式（匿名时两个输入框置灰）
    if (g_cliAuthCombo) {
        SendMessageW(g_cliAuthCombo, CB_SETCURSEL,
                     g_config.cliAnonymous ? 0 : 1, 0);
        ApplyClientAuthMode();
    }

    // 运行日志页：恢复分类过滤开关状态（同步到原子变量与按钮外观）
    g_logUpload.store(g_config.logFilterUpload);
    g_logDownload.store(g_config.logFilterDownload);
    g_logClient.store(g_config.logFilterClient);
    g_logOther.store(g_config.logFilterOther);
    g_logWarn.store(g_config.logFilterWarn);
    {
        bool states[5] = {g_config.logFilterUpload, g_config.logFilterDownload,
                          g_config.logFilterClient, g_config.logFilterOther,
                          g_config.logFilterWarn};
        for (int i = 0; i < 5; ++i) {
            if (g_logFilterBtns[i])
                SendMessage(g_logFilterBtns[i], BM_SETCHECK,
                            states[i] ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    }

    // 应用上次保存的界面模式（默认高级模式）。
    // 放在 ShowWindow 之前，避免启动时先显示完整窗口再突然缩小。
    ApplyCompactMode(g_config.compactMode);

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        // IsDialogMessage 会拦截单行编辑框中的回车（去查询默认按钮，
        // 本窗口没有默认按钮，按键被直接丢弃），导致子类化的回车/ESC
        // 处理收不到消息。焦点在地址/端口/路径输入框或文件列表时，
        // 这些快捷键直接派发。
        if (msg.message == WM_KEYDOWN) {
            bool editKey =
                (msg.hwnd == g_cliHost || msg.hwnd == g_cliPort ||
                 msg.hwnd == g_cliPath) &&
                (msg.wParam == VK_RETURN ||
                 (msg.wParam == VK_ESCAPE && msg.hwnd == g_cliPath));
            bool listKey = msg.hwnd == g_cliList &&
                (msg.wParam == VK_DELETE || msg.wParam == VK_RETURN ||
                 msg.wParam == VK_BACK || msg.wParam == VK_F5);
            if (editKey || listKey) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                continue;
            }
        }
        if (!IsDialogMessage(g_hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return (int)msg.wParam;
}
