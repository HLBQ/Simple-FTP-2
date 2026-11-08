#include "config.h"

#include <windows.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// 简单的 INI 风格配置读写
// 格式：
//   [general]
//   port=21
//   sharePath=D:\share
//   ...
//   [bookmark]
//   name=xxx|host=1.2.3.4|port=21|user=admin|pass=123
//   [record]
//   time=...|target=...|detail=...
// ---------------------------------------------------------------------------

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// 转义：换行替换为空格（破坏 INI 行结构），'%' 与 '|' 用 URL 风格编码。
// 修复：原实现把 '|' 直接改成 '/'，会把含 '|' 的名字/路径篡改成错误内容。
static std::string Escape(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        if (c == '\r' || c == '\n') { r.push_back(' '); continue; }
        if (c == '%') r += "%25";
        else if (c == '|') r += "%7C";
        else r.push_back(c);
    }
    return r;
}

// 解码 Escape 的输出（%7C -> '|'，%25 -> '%'）
static std::string Unescape(const std::string& s) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int a = hex(s[i + 1]), b = hex(s[i + 2]);
            if (a >= 0 && b >= 0) {
                r.push_back((char)((a << 4) | b));
                i += 2;
                continue;
            }
        }
        r.push_back(s[i]);
    }
    return r;
}

// UTF-8 -> 宽字符（错误提示用）
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                  nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

// 获取 exe 所在目录（UTF-8）
static std::string ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) path = path.substr(0, pos);

    int len = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), (int)path.size(),
                                  nullptr, 0, nullptr, nullptr);
    std::string dir(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), (int)path.size(), &dir[0], len,
                        nullptr, nullptr);
    return dir;
}

std::string AppConfig::DefaultPath() {
    return ExeDir() + "\\config.ini";
}

AppConfig AppConfig::Load(const std::string& file) {
    AppConfig cfg;
    // 首次启动/配置不存在时，共享路径默认为程序所在文件夹
    cfg.sharePath = ExeDir();
    // 用宽字符路径打开：MSVC 的窄字符 fstream 按 ANSI 代码页解释路径，
    // UTF-8 编码的中文/特殊字符路径会被解析错，导致配置永远读不到
    std::ifstream in(fs::path(Utf8ToWide(file)), std::ios::binary);
    if (!in) return cfg; // 文件不存在，返回默认

    std::string section;
    bool hasCompactPerms = false;
    bool hasCompactDelete = false;
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));

        if (section == "general") {
            if (key == "port") cfg.port = atoi(val.c_str());
            else if (key == "sharePath") cfg.sharePath = Unescape(val);
            else if (key == "pasvStart") cfg.pasvStart = atoi(val.c_str());
            else if (key == "pasvEnd") cfg.pasvEnd = atoi(val.c_str());
            else if (key == "permRead") cfg.perms.read = (val == "1");
            else if (key == "permWrite") cfg.perms.write = (val == "1");
            else if (key == "permDel") cfg.perms.del = (val == "1");
            else if (key == "permMkdir") cfg.perms.mkdir = (val == "1");
            else if (key == "permRmdir") cfg.perms.rmdir = (val == "1");
            else if (key == "permRename") cfg.perms.rename = (val == "1");
            else if (key == "permList") cfg.perms.list = (val == "1");
            else if (key == "allowedIp") {
                if (!val.empty()) cfg.allowedIps.push_back(Unescape(val));
            }
            else if (key == "bindIp") cfg.bindIp = Unescape(val);
            else if (key == "fileNameEncoding") cfg.fileNameEncoding = atoi(val.c_str());
            else if (key == "advancedMode") cfg.advancedMode = (val == "1");
            else if (key == "compactMode") cfg.compactMode = (val == "1");
            else if (key == "compactPermRead") { cfg.compactPermRead = (val == "1"); hasCompactPerms = true; }
            else if (key == "compactPermWrite") { cfg.compactPermWrite = (val == "1"); hasCompactPerms = true; }
            else if (key == "compactPermDelete") { cfg.compactPermDelete = (val == "1"); hasCompactDelete = true; }
            else if (key == "perfDataTimeout") cfg.perf.dataTimeoutSec = atoi(val.c_str());
            else if (key == "perfTcpNoDelay") cfg.perf.tcpNoDelay = (val == "1");
            else if (key == "perfSockBufKB") cfg.perf.sockBufKB = atoi(val.c_str());
            else if (key == "perfAppBufKB") cfg.perf.appBufKB = atoi(val.c_str());
            else if (key == "perfMaxFileMB") cfg.perf.maxFileSizeMB = atoi(val.c_str());
            else if (key == "perfMaxConnPerIp") cfg.perf.maxConnPerIp = atoi(val.c_str());
            else if (key == "secConfirmDeleteFile") cfg.security.confirmDeleteFile = (val == "1");
            else if (key == "secConfirmDeleteDir") cfg.security.confirmDeleteDir = (val == "1");
            else if (key == "secConfirmRename") cfg.security.confirmRename = (val == "1");
            else if (key == "secConfirmOverwrite") cfg.security.confirmOverwrite = (val == "1");
            else if (key == "secConfirmDownload") cfg.security.confirmDownload = (val == "1");
            else if (key == "secDeleteToRecycleBin") cfg.security.deleteToRecycleBin = (val == "1");
            else if (key == "secConfirmTimeout") cfg.security.confirmTimeoutSec = atoi(val.c_str());
            else if (key == "secIpBlacklist") cfg.security.ipBlacklist = (val == "1");
            else if (key == "cliHost") cfg.cliHost = Unescape(val);
            else if (key == "cliPort") cfg.cliPort = atoi(val.c_str());
            else if (key == "cliUser") cfg.cliUser = Unescape(val);
            else if (key == "cliPass") cfg.cliPass = Unescape(val);
            else if (key == "cliAnonymous") cfg.cliAnonymous = (atoi(val.c_str()) != 0);
            else if (key == "transferMode") cfg.transferMode = atoi(val.c_str());
            else if (key == "logFilterUpload") cfg.logFilterUpload = (val == "1");
            else if (key == "logFilterDownload") cfg.logFilterDownload = (val == "1");
            else if (key == "logFilterClient") cfg.logFilterClient = (val == "1");
            else if (key == "logFilterOther") cfg.logFilterOther = (val == "1");
            else if (key == "logFilterWarn") cfg.logFilterWarn = (val == "1");
        } else if (section == "fileperm") {
            // path=xxx|read=1|write=0|del=0|mkdir=0|rmdir=0|rename=0|list=1
            // 注意：这里必须用整行 line（含第一个 key=），否则第一段会丢失
            FilePermission fp;
            std::stringstream ss(line);
            std::string part;
            while (std::getline(ss, part, '|')) {
                size_t p = part.find('=');
                if (p == std::string::npos) continue;
                std::string k = Trim(part.substr(0, p));
                std::string v = Trim(part.substr(p + 1));
                if (k == "path") fp.path = Unescape(v);
                else if (k == "read") fp.perms.read = (v == "1");
                else if (k == "write") fp.perms.write = (v == "1");
                else if (k == "del") fp.perms.del = (v == "1");
                else if (k == "mkdir") fp.perms.mkdir = (v == "1");
                else if (k == "rmdir") fp.perms.rmdir = (v == "1");
                else if (k == "rename") fp.perms.rename = (v == "1");
                else if (k == "list") fp.perms.list = (v == "1");
                else if (k == "hidden") fp.hidden = (v == "1");
            }
            if (!fp.path.empty()) cfg.filePerms.push_back(fp);
        } else if (section == "bookmark") {
            // name=xxx|host=...|port=...|user=...|pass=...
            // 注意：这里必须用整行 line（含第一个 key=），否则第一段会丢失
            FtpBookmark bm;
            std::stringstream ss(line);
            std::string part;
            while (std::getline(ss, part, '|')) {
                size_t p = part.find('=');
                if (p == std::string::npos) continue;
                std::string k = Trim(part.substr(0, p));
                std::string v = Trim(part.substr(p + 1));
                if (k == "name") bm.name = Unescape(v);
                else if (k == "host") bm.host = Unescape(v);
                else if (k == "port") bm.port = atoi(v.c_str());
                else if (k == "user") bm.user = Unescape(v);
                else if (k == "pass") bm.pass = Unescape(v);
            }
            if (!bm.host.empty()) cfg.bookmarks.push_back(bm);
        } else if (section == "record") {
            // 注意：这里必须用整行 line（含第一个 key=），否则第一段会丢失
            ConnectRecord rec;
            std::stringstream ss(line);
            std::string part;
            while (std::getline(ss, part, '|')) {
                size_t p = part.find('=');
                if (p == std::string::npos) continue;
                std::string k = Trim(part.substr(0, p));
                std::string v = Trim(part.substr(p + 1));
                if (k == "time") rec.time = Unescape(v);
                else if (k == "target") rec.target = Unescape(v);
                else if (k == "detail") rec.detail = Unescape(v);
            }
            if (!rec.target.empty()) cfg.records.push_back(rec);
        }
    }
    // 旧配置没有精简权限时，用高级模式权限做一次初始快照，
    // 避免第一次切到精简模式时凭空丢掉权限。
    if (!hasCompactPerms) {
        cfg.compactPermRead = cfg.perms.read;
        cfg.compactPermWrite = cfg.perms.write || cfg.perms.mkdir ||
                               cfg.perms.rename;
        cfg.compactPermDelete = cfg.perms.del || cfg.perms.rmdir;
    } else if (!hasCompactDelete) {
        // 上一版只有“读取/写入”，且写入包含删除；迁移时拆成
        // 写入 + 删除，保持原有权限不丢失。
        cfg.compactPermDelete = cfg.compactPermWrite;
    }
    // 配置存在但共享路径为空时，同样回退到程序所在文件夹
    if (cfg.sharePath.empty()) cfg.sharePath = ExeDir();
    if (cfg.bindIp.empty()) cfg.bindIp = "0.0.0.0";
    if (cfg.fileNameEncoding < 0 || cfg.fileNameEncoding > 4)
        cfg.fileNameEncoding = 0;
    return cfg;
}

void AppConfig::Save(const std::string& file) const {
    // 宽字符路径：原因同 Load（窄路径按 ANSI 解释，中文路径写入会失败）
    std::ofstream out(fs::path(Utf8ToWide(file)), std::ios::binary | std::ios::trunc);
    if (!out) {
        MessageBoxW(nullptr, (L"配置保存失败：无法打开文件\n" + Utf8ToWide(file)).c_str(),
                    L"FtpTool", MB_OK | MB_ICONWARNING);
        return;
    }

    out << "# FtpTool 配置文件（自动生成）\n";
    out << "[general]\n";
    out << "port=" << port << "\n";
    out << "sharePath=" << Escape(sharePath) << "\n";
    out << "pasvStart=" << pasvStart << "\n";
    out << "pasvEnd=" << pasvEnd << "\n";
    out << "permRead=" << (perms.read ? 1 : 0) << "\n";
    out << "permWrite=" << (perms.write ? 1 : 0) << "\n";
    out << "permDel=" << (perms.del ? 1 : 0) << "\n";
    out << "permMkdir=" << (perms.mkdir ? 1 : 0) << "\n";
    out << "permRmdir=" << (perms.rmdir ? 1 : 0) << "\n";
    out << "permRename=" << (perms.rename ? 1 : 0) << "\n";
    out << "permList=" << (perms.list ? 1 : 0) << "\n";
    out << "bindIp=" << Escape(bindIp) << "\n";
    out << "fileNameEncoding=" << fileNameEncoding << "\n";
    out << "advancedMode=" << (advancedMode ? 1 : 0) << "\n";
    out << "compactMode=" << (compactMode ? 1 : 0) << "\n";
    out << "compactPermRead=" << (compactPermRead ? 1 : 0) << "\n";
    out << "compactPermWrite=" << (compactPermWrite ? 1 : 0) << "\n";
    out << "compactPermDelete=" << (compactPermDelete ? 1 : 0) << "\n";
    out << "perfDataTimeout=" << perf.dataTimeoutSec << "\n";
    out << "perfTcpNoDelay=" << (perf.tcpNoDelay ? 1 : 0) << "\n";
    out << "perfSockBufKB=" << perf.sockBufKB << "\n";
    out << "perfAppBufKB=" << perf.appBufKB << "\n";
    out << "perfMaxFileMB=" << perf.maxFileSizeMB << "\n";
    out << "perfMaxConnPerIp=" << perf.maxConnPerIp << "\n";
    out << "secConfirmDeleteFile=" << (security.confirmDeleteFile ? 1 : 0) << "\n";
    out << "secConfirmDeleteDir=" << (security.confirmDeleteDir ? 1 : 0) << "\n";
    out << "secConfirmRename=" << (security.confirmRename ? 1 : 0) << "\n";
    out << "secConfirmOverwrite=" << (security.confirmOverwrite ? 1 : 0) << "\n";
    out << "secConfirmDownload=" << (security.confirmDownload ? 1 : 0) << "\n";
    out << "secDeleteToRecycleBin=" << (security.deleteToRecycleBin ? 1 : 0) << "\n";
    out << "secConfirmTimeout=" << security.confirmTimeoutSec << "\n";
    out << "secIpBlacklist=" << (security.ipBlacklist ? 1 : 0) << "\n";
    out << "cliHost=" << Escape(cliHost) << "\n";
    out << "cliPort=" << cliPort << "\n";
    out << "cliUser=" << Escape(cliUser) << "\n";
    out << "cliPass=" << Escape(cliPass) << "\n";
    out << "cliAnonymous=" << (cliAnonymous ? 1 : 0) << "\n";
    out << "transferMode=" << transferMode << "\n";
    out << "logFilterUpload=" << (logFilterUpload ? 1 : 0) << "\n";
    out << "logFilterDownload=" << (logFilterDownload ? 1 : 0) << "\n";
    out << "logFilterClient=" << (logFilterClient ? 1 : 0) << "\n";
    out << "logFilterOther=" << (logFilterOther ? 1 : 0) << "\n";
    out << "logFilterWarn=" << (logFilterWarn ? 1 : 0) << "\n";
    for (const auto& ip : allowedIps) {
        out << "allowedIp=" << Escape(ip) << "\n";
    }

    out << "\n[fileperm]\n";
    for (const auto& fp : filePerms) {
        out << "path=" << Escape(fp.path)
            << "|read=" << (fp.perms.read ? 1 : 0)
            << "|write=" << (fp.perms.write ? 1 : 0)
            << "|del=" << (fp.perms.del ? 1 : 0)
            << "|mkdir=" << (fp.perms.mkdir ? 1 : 0)
            << "|rmdir=" << (fp.perms.rmdir ? 1 : 0)
            << "|rename=" << (fp.perms.rename ? 1 : 0)
            << "|list=" << (fp.perms.list ? 1 : 0)
            << "|hidden=" << (fp.hidden ? 1 : 0) << "\n";
    }

    out << "\n[bookmark]\n";
    for (const auto& bm : bookmarks) {
        out << "name=" << Escape(bm.name)
            << "|host=" << Escape(bm.host)
            << "|port=" << bm.port
            << "|user=" << Escape(bm.user)
            << "|pass=" << Escape(bm.pass) << "\n";
    }

    out << "\n[record]\n";
    for (const auto& rec : records) {
        out << "time=" << Escape(rec.time)
            << "|target=" << Escape(rec.target)
            << "|detail=" << Escape(rec.detail) << "\n";
    }

    out.flush();
    if (!out.good()) {
        MessageBoxW(nullptr, (L"配置保存失败：写入文件出错\n" + Utf8ToWide(file)).c_str(),
                    L"FtpTool", MB_OK | MB_ICONWARNING);
    }
}
