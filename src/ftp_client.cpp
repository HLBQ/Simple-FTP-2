#include "ftp_client.h"

#include "winlongpath.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <system_error>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace {

namespace fs = std::filesystem;

// 确保 WSA 已初始化（进程内只初始化一次）
void EnsureWsa() {
    static bool inited = false;
    if (!inited) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        inited = true;
    }
}

// UTF-8 -> 本地编码（GBK）
std::string Utf8ToLocal(const std::string& s) {
    if (s.empty()) return "";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    int len2 = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(),
                                   nullptr, 0, nullptr, nullptr);
    std::string r(len2, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &r[0], len2,
                        nullptr, nullptr);
    return r;
}

// UTF-8 -> 宽字符（用于 _wfopen，正确处理中文/特殊字符路径）
std::wstring Utf8ToWidePath(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

// 本地编码（GBK）-> UTF-8
std::string LocalToUtf8(const std::string& s) {
    if (s.empty()) return "";
    int len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &w[0], len);
    int len2 = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                   nullptr, 0, nullptr, nullptr);
    std::string r(len2, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &r[0], len2,
                        nullptr, nullptr);
    return r;
}

// 判断一个字段是否为“时间”字段：形如 HH:MM 或 YYYY（年份）
bool IsTimeField(const std::string& f) {
    if (f.size() == 5 && f[2] == ':' &&
        isdigit((unsigned char)f[0]) && isdigit((unsigned char)f[1]) &&
        isdigit((unsigned char)f[3]) && isdigit((unsigned char)f[4]))
        return true;   // HH:MM
    if (f.size() == 4 && isdigit((unsigned char)f[0]) &&
        isdigit((unsigned char)f[1]) && isdigit((unsigned char)f[2]) &&
        isdigit((unsigned char)f[3]))
        return true;   // YYYY
    return false;
}

// 从 LIST 行解析出文件名（兼容 Unix 与 DOS 两种格式）
std::string ParseListName(const std::string& line) {
    // Unix: -rw-r--r-- 1 user group 1234 Jan 1 12:00 filename
    //        -rw-r--r-- 1 user group 1234 Jan 12 2020 filename
    // DOS:  01-01-20  12:00PM       <DIR>          dirname
    if (line.size() > 40 && (line[0] == '-' || line[0] == 'd' || line[0] == 'l')) {
        // Unix 格式：字段依次为 权限 链接数 用户 组 大小 月 日 时间/年 文件名
        // 由于“日”可能是 1~2 位，不能简单按空格数定位，
        // 改为：先跳过前 5 个字段（权限/链接/用户/组/大小），
        // 再跳过 月、日，然后跳过“时间/年”字段，其后即为文件名。
        size_t pos = 0;
        int skip = 5;   // 跳过前 5 个字段
        while (skip > 0 && pos < line.size()) {
            while (pos < line.size() && line[pos] == ' ') ++pos;   // 跳过空格
            while (pos < line.size() && line[pos] != ' ') ++pos;   // 跳过字段
            --skip;
        }
        // 此时 pos 指向“月”字段前，跳过 月、日 两个字段
        int skip2 = 2;
        while (skip2 > 0 && pos < line.size()) {
            while (pos < line.size() && line[pos] == ' ') ++pos;
            while (pos < line.size() && line[pos] != ' ') ++pos;
            --skip2;
        }
        // 跳过“时间/年”字段
        while (pos < line.size() && line[pos] == ' ') ++pos;
        size_t timeStart = pos;
        while (pos < line.size() && line[pos] != ' ') ++pos;
        std::string timeField = line.substr(timeStart, pos - timeStart);
        if (IsTimeField(timeField)) {
            // 时间字段之后即为文件名
            while (pos < line.size() && line[pos] == ' ') ++pos;
            if (pos < line.size()) return line.substr(pos);
        }
        // 若时间字段识别失败，回退到“第 8 个空格之后”
        int spaces = 0;
        size_t i = 0;
        for (; i < line.size(); ++i) {
            if (line[i] == ' ') {
                ++spaces;
                if (spaces == 8) break;
            }
        }
        if (i < line.size()) {
            size_t start = i + 1;
            while (start < line.size() && line[start] == ' ') ++start;
            return line.substr(start);
        }
    }
    // DOS 格式：<DIR> 或大小之后
    size_t dirPos = line.find("<DIR>");
    if (dirPos != std::string::npos) {
        size_t start = dirPos + 5;
        while (start < line.size() && line[start] == ' ') ++start;
        return line.substr(start);
    }
    // 兜底：取最后一个空格后的内容
    size_t sp = line.find_last_of(' ');
    if (sp != std::string::npos && sp + 1 < line.size())
        return line.substr(sp + 1);
    return line;
}

// 计算文件 MD5（小写十六进制），失败返回空字符串。流式读取。
// progress 可选：(已读字节, 文件大小)
std::string CalcMd5(const std::wstring& widePath,
                    const FtpClient::ProgressFn& progress) {
    FILE* f = _wfopen(ToLongPathW(widePath).c_str(), L"rb");
    if (!f) return "";

    // 获取文件总大小用于进度
    uint64_t total = 0;
    {
        std::error_code ec;
        fs::path p = fs::path(ToLongPathW(widePath));
        if (fs::is_regular_file(p, ec)) total = (uint64_t)fs::file_size(p, ec);
    }

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_MD5_ALGORITHM,
                                                    nullptr, 0))) {
        fclose(f); return "";
    }
    if (!BCRYPT_SUCCESS(BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        fclose(f); return "";
    }

    std::vector<char> buf(64 * 1024);
    bool ok = true;
    size_t n;
    uint64_t read = 0;
    while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) {
        if (!BCRYPT_SUCCESS(BCryptHashData(hHash, (PUCHAR)buf.data(), (ULONG)n, 0))) {
            ok = false; break;
        }
        read += (uint64_t)n;
        if (progress) progress(read, total);
    }
    fclose(f);

    std::string result;
    if (ok) {
        BYTE digest[16];
        if (BCRYPT_SUCCESS(BCryptFinishHash(hHash, digest, sizeof(digest), 0))) {
            char hex[33];
            for (int i = 0; i < 16; ++i)
                std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
            hex[32] = '\0';
            result = hex;
        }
    }
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return result;
}

} // namespace

FtpClient::FtpClient() {
    EnsureWsa();
}

FtpClient::~FtpClient() {
    Disconnect();
}

std::string FtpClient::ToServer(const std::string& utf8) const {
    // 若服务器支持 UTF8，直接发送 UTF-8；否则转本地编码
    return utf8Mode_ ? utf8 : Utf8ToLocal(utf8);
}

std::string FtpClient::FromServer(const std::string& raw) const {
    return utf8Mode_ ? raw : LocalToUtf8(raw);
}

// 把 UTF-8 名字编码成发往服务器的字节串。
// 非 UTF-8 会话下发 GBK：必须检查“有没有字符在 GBK 里表示不了”，
// 否则 WideCharToMultiByte 会悄悄把它换成 '?'，服务端再因为 '?' 是
// Windows 非法文件名字符而返回 550（errno=22），用户只看到一句莫名其妙的
// “上传失败”。这里提前拦下并给出可操作的提示。
bool FtpClient::EncodeName(const std::string& utf8, std::string& out) const {
    out.clear();
    if (utf8Mode_) {
        out = utf8;          // 会话已切 UTF-8：原样发送
        return true;
    }
    if (utf8.empty()) return true;

    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                utf8.c_str(), (int)utf8.size(), nullptr, 0);
    if (n <= 0) {
        lastError_ = "文件名不是合法的 UTF-8 文本，无法发送";
        return false;
    }
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.c_str(),
                        (int)utf8.size(), &w[0], n);

    // WC_NO_BEST_FIT_CHARS：不做“看起来像”的近似替换（如把全角字符换成
    // 半角），只用真正的默认字符，并通过 usedDefault 报告发生了替换。
    BOOL usedDefault = FALSE;
    int m = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w.c_str(),
                                (int)w.size(), nullptr, 0, nullptr, &usedDefault);
    if (m <= 0 || usedDefault) {
        lastError_ = "文件名包含本地编码(GBK)无法表示的字符，"
                     "服务器当前是 GBK 模式：请在服务器的“其他设置→默认编码”"
                     "选择 UTF-8（或兼容模式），或把文件改成中文/英文名再传";
        return false;
    }
    out.assign((size_t)m, '\0');
    WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w.c_str(), (int)w.size(),
                        &out[0], m, nullptr, nullptr);
    return true;
}

bool FtpClient::ReadLine(std::string& line) {
    line.clear();
    char c;
    while (true) {
        int n = recv((SOCKET)ctrlSock_, &c, 1, 0);
        if (n <= 0) return false;
        if (c == '\n') break;
        if (c != '\r') line.push_back(c);
    }
    return true;
}

int FtpClient::ReadResponse(std::string* firstLine) {
    std::string line;
    if (!ReadLine(line)) return -1;
    if (firstLine) *firstLine = line;
    if (line.size() < 3) return -1;
    int code = atoi(line.substr(0, 3).c_str());
    // 多行响应：第 4 个字符为 '-' 表示后续还有行，直到出现 "code " 结束
    if (line.size() >= 4 && line[3] == '-') {
        std::string endMark = line.substr(0, 3) + " ";
        while (true) {
            std::string l;
            if (!ReadLine(l)) return -1;
            if (l.compare(0, endMark.size(), endMark) == 0) break;
        }
    }
    return code;
}

int FtpClient::SendCmd(const std::string& cmd, std::string* respLine) {
    // 每条新命令先清掉上一条错误：传输前的“探测”命令（例如上传前用
    // SIZE 看远程是否已存在，文件不存在会返回 550）不应把错误留在
    // lastError_ 里，让一次成功的上传在界面上显示成失败。
    lastError_.clear();
    std::string data = cmd + "\r\n";
    int sent = send((SOCKET)ctrlSock_, data.c_str(), (int)data.size(), 0);
    if (sent <= 0) {
        lastError_ = "发送命令失败";
        return -1;
    }
    return ReadResponse(respLine);
}

bool FtpClient::Connect(const std::string& host, uint16_t port,
                        const std::string& user, const std::string& pass) {
    Disconnect();
    lastError_.clear();

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        lastError_ = "创建套接字失败";
        return false;
    }

    // 连接超时 8 秒
    DWORD to = 8000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof(to));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
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
    if (!ok) {
        closesocket(s);
        lastError_ = "无法连接到服务器";
        return false;
    }
    ctrlSock_ = (uintptr_t)s;

    // 读取欢迎信息
    int code = ReadResponse();
    if (code != 220) {
        lastError_ = "服务器未就绪";
        Disconnect();
        return false;
    }

    // 尝试启用 UTF-8（失败不影响；有些服务器要求先登录，登录后再试一次）
    auto tryUtf8 = [this]() {
        std::string r;
        int c = SendCmd("OPTS UTF8 ON", &r);
        // 只认真正的 200：若服务端其实没切（正文里写 ignored），还按 UTF-8
        // 发名字，就会被服务端按本地编码解码成乱码。
        if (c == 200 && r.find("ignored") == std::string::npos)
            utf8Mode_ = true;
        return utf8Mode_;
    };
    tryUtf8();   // 本工具服务端允许登录前设置

    // 登录
    std::string resp;
    code = SendCmd("USER " + ToServer(user), &resp);
    if (code == 331) {
        code = SendCmd("PASS " + ToServer(pass), &resp);
    }
    if (code != 230 && code != 202) {
        lastError_ = "登录失败：" + resp;
        Disconnect();
        return false;
    }

    // 登录前那次被拒（530 等）时补试一次，避免“服务器明明支持 UTF-8，
    // 客户端却一直用本地编码”的错位。
    if (!utf8Mode_) tryUtf8();

    // 设置二进制传输模式
    SendCmd("TYPE I");

    // 探测服务器支持的扩展功能（禁用模式下不发 FEAT，极端兼容）
    supportRest_ = false;
    supportXmd5_ = false;
    if (mode_ != TransferMode::Disabled) {
        DetectFeatures();
    }

    cwd_ = "/";
    return true;
}

void FtpClient::DetectFeatures() {
    std::string data = "FEAT\r\n";
    if (send((SOCKET)ctrlSock_, data.c_str(), (int)data.size(), 0) <= 0) return;

    // 读取所有 FEAT 响应行，直到 "211 End"
    std::string line;
    if (!ReadLine(line)) return;
    // 211-Features:  开头
    if (line.size() < 3 || line[0] != '2') return;
    std::string endMark = line.substr(0, 3) + " ";   // "211 "
    bool isMultiline = (line.size() >= 4 && line[3] == '-');

    auto checkFeature = [&](const std::string& l) {
        std::string u;
        u.reserve(l.size());
        for (char c : l) u.push_back((char)std::toupper((unsigned char)c));
        if (u.find("REST STREAM") != std::string::npos) supportRest_ = true;
        if (u.find("XMD5") != std::string::npos) supportXmd5_ = true;
    };

    if (!isMultiline) {
        checkFeature(line);
        return;
    }
    while (ReadLine(line)) {
        if (line.compare(0, endMark.size(), endMark) == 0) break;
        checkFeature(line);
    }
}

void FtpClient::Disconnect() {
    if (ctrlSock_ != (uintptr_t)-1) {
        // 尽力发送 QUIT
        std::string data = "QUIT\r\n";
        send((SOCKET)ctrlSock_, data.c_str(), (int)data.size(), 0);
        closesocket((SOCKET)ctrlSock_);
        ctrlSock_ = (uintptr_t)-1;
    }
    utf8Mode_ = false;
}

uintptr_t FtpClient::OpenPassive() {
    std::string resp;
    int code = SendCmd("PASV", &resp);
    if (code != 227) {
        lastError_ = "PASV 失败：" + resp;
        return (uintptr_t)-1;
    }
    // 解析 227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)
    size_t l = resp.find('(');
    size_t r = resp.find(')');
    if (l == std::string::npos || r == std::string::npos) {
        lastError_ = "PASV 响应格式错误";
        return (uintptr_t)-1;
    }
    std::string nums = resp.substr(l + 1, r - l - 1);
    int v[6] = {0};
    int idx = 0;
    std::stringstream ss(nums);
    std::string tok;
    while (std::getline(ss, tok, ',') && idx < 6) {
        v[idx++] = atoi(tok.c_str());
    }
    if (idx < 6) {
        lastError_ = "PASV 地址解析失败";
        return (uintptr_t)-1;
    }
    char ip[32];
    snprintf(ip, sizeof(ip), "%d.%d.%d.%d", v[0], v[1], v[2], v[3]);
    // 不信任 PASV 响应里通告的 IP，改用控制连接的对端地址。
    // 开启 TUN/代理、多网卡或服务器在 NAT 后面时，227 里的地址经常
    // 是不可达的 10.x 地址；而控制连接实际连到的地址一定是可达的。
    // 这同时避免了 PASV IP 欺骗（FTP bounce）。
    sockaddr_in peer{};
    int peerLen = sizeof(peer);
    if (getpeername((SOCKET)ctrlSock_, (sockaddr*)&peer, &peerLen) == 0 &&
        peer.sin_family == AF_INET) {
        char peerIp[INET_ADDRSTRLEN] = {0};
        if (inet_ntop(AF_INET, &peer.sin_addr, peerIp, sizeof(peerIp))) {
            strncpy_s(ip, sizeof(ip), peerIp, _TRUNCATE);
        }
    }
    int port = v[4] * 256 + v[5];

    SOCKET ds = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ds == INVALID_SOCKET) {
        lastError_ = "创建数据套接字失败";
        return (uintptr_t)-1;
    }
    // 数据连接超时 30 秒（8 秒对大文件传输间隙太短，容易误报失败）
    DWORD to = 30000;
    setsockopt(ds, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
    setsockopt(ds, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof(to));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    if (connect(ds, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(ds);
        lastError_ = "数据连接失败";
        return (uintptr_t)-1;
    }
    return (uintptr_t)ds;
}

bool FtpClient::List(std::vector<Entry>& out) {
    out.clear();
    uintptr_t ds = OpenPassive();
    if (ds == (uintptr_t)-1) return false;

    std::string resp;
    int code = SendCmd("LIST", &resp);
    if (code != 150 && code != 125) {
        closesocket((SOCKET)ds);
        lastError_ = "LIST 失败：" + resp;
        return false;
    }

    // 读取数据
    std::string data;
    char buf[4096];
    int n;
    while ((n = recv((SOCKET)ds, buf, sizeof(buf), 0)) > 0) {
        data.append(buf, n);
    }
    closesocket((SOCKET)ds);

    // 读取传输完成响应
    ReadResponse();

    // 逐行解析
    std::stringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        Entry e;
        e.name = FromServer(ParseListName(line));
        if (e.name == "." || e.name == "..") continue;
        e.isDir = (line[0] == 'd') || (line.find("<DIR>") != std::string::npos);
        // 解析大小（Unix 格式第 5 个字段）。
        // 注意：字段之间可能有多个连续空格（如大小右对齐填充），
        // 因此必须按“字段”跳过连续空格，而不能按空格字符计数。
        if (line[0] == '-' || line[0] == 'd') {
            size_t pos = 0;
            int field = 0;   // 已跳过的字段数
            // 跳过前 4 个字段（权限/链接数/用户/组），定位到第 5 个字段（大小）
            while (field < 4 && pos < line.size()) {
                while (pos < line.size() && line[pos] == ' ') ++pos;  // 跳过前导空格
                while (pos < line.size() && line[pos] != ' ') ++pos;  // 跳过字段内容
                ++field;
            }
            while (pos < line.size() && line[pos] == ' ') ++pos;      // 跳过字段间空格
            size_t start = pos;
            while (pos < line.size() && line[pos] != ' ') ++pos;      // 大小字段内容
            if (pos > start) {
                e.size = _strtoui64(line.substr(start, pos - start).c_str(), nullptr, 10);
            }
        }
        out.push_back(e);
    }
    return true;
}

bool FtpClient::ChangeDir(const std::string& name) {
    std::string resp;
    std::string argName;
    if (!EncodeName(name, argName)) return false;
    int code = SendCmd("CWD " + argName, &resp);
    if (code != 250) {
        lastError_ = "切换目录失败：" + resp;
        return false;
    }
    // 更新 cwd_
    if (!name.empty() && name[0] == '/') {
        cwd_ = name;
    } else if (name == "..") {
        if (cwd_.size() > 1) {
            size_t p = cwd_.find_last_of('/');
            cwd_ = (p == 0 || p == std::string::npos) ? "/" : cwd_.substr(0, p);
        }
    } else {
        if (cwd_.back() != '/') cwd_ += "/";
        cwd_ += name;
    }
    return true;
}

bool FtpClient::ChangeDirUp() {
    std::string resp;
    int code = SendCmd("CDUP", &resp);
    if (code != 250) {
        lastError_ = "返回上级目录失败：" + resp;
        return false;
    }
    if (cwd_.size() > 1) {
        size_t p = cwd_.find_last_of('/');
        cwd_ = (p == 0 || p == std::string::npos) ? "/" : cwd_.substr(0, p);
    }
    return true;
}

bool FtpClient::Download(const std::string& remoteName, const std::string& localPath) {
    return Download(remoteName, localPath, 0, nullptr);
}

bool FtpClient::Download(const std::string& remoteName, const std::string& localPath,
                         uint64_t totalSize, const ProgressFn& progress,
                         const ProgressFn& hashProgress) {
    namespace fs = std::filesystem;

    // 1) 确定远程文件大小
    uint64_t remoteSize = totalSize;
    if (remoteSize == 0) {
        remoteSize = GetRemoteSize(remoteName);
        if (remoteSize == (uint64_t)-1) remoteSize = 0;
    }

    // 2) 本地文件大小
    uint64_t localSize = 0;
    bool localExists = false;
    {
        std::error_code ec;
        fs::path lp = fs::path(ToLongPathW(Utf8ToWidePath(localPath)));
        if (fs::is_regular_file(lp, ec)) {
            localExists = true;
            localSize = (uint64_t)fs::file_size(lp, ec);
            if (ec) localSize = 0;
        }
    }

    // 3) 根据模式与服务器能力决定行为
    bool useRest = (mode_ == TransferMode::Fast || mode_ == TransferMode::Balanced)
                   && supportRest_;
    bool useHash = (mode_ == TransferMode::Balanced || mode_ == TransferMode::Safe)
                   && supportXmd5_;

    // 跳过条件：本地存在且大小等于远程
    if (localExists && localSize == remoteSize && remoteSize > 0) {
        if (useHash) {
            // 大小相同但可能是假文件：比对 MD5
            std::string localMd5 = ComputeLocalMd5(localPath, hashProgress);
            std::string remoteMd5 = GetRemoteMd5(remoteName);
            if (!localMd5.empty() && !remoteMd5.empty() && localMd5 == remoteMd5) {
                // 完整且一致：跳过
                if (progress) progress(remoteSize, remoteSize);
                return true;
            }
            // MD5 不一致：假文件，全量重下
            localExists = false;   // 当作不存在，走全新下载
            localSize = 0;
        } else {
            // 不校验哈希：大小相同即视为完整，跳过
            if (progress) progress(remoteSize, remoteSize);
            return true;
        }
    }

    // 4) 续传或全新下载
    uint64_t offset = 0;
    if (useRest && localExists && localSize < remoteSize) {
        offset = localSize;   // 从断点续传
    } else if (localExists && localSize > remoteSize) {
        // 本地比远程还大（异常）：全量重下
        offset = 0;
    }

    bool ok = DownloadRaw(remoteName, localPath, offset, remoteSize, progress);
    if (!ok) return false;

    // 5) 续传/下载完成后，若开启哈希则校验；不一致则全量重下一次
    if (useHash && remoteSize > 0) {
        std::string localMd5 = ComputeLocalMd5(localPath, hashProgress);
        std::string remoteMd5 = GetRemoteMd5(remoteName);
        if (localMd5.empty() || remoteMd5.empty() || localMd5 != remoteMd5) {
            // 校验失败：全量重下一次
            ok = DownloadRaw(remoteName, localPath, 0, remoteSize, progress);
            if (ok) {
                // 重下后再校验一次
                std::string md5b = ComputeLocalMd5(localPath, hashProgress);
                if (md5b.empty() || md5b != remoteMd5) {
                    lastError_ = "下载完成但哈希校验失败";
                    return false;
                }
            }
        }
    }
    return ok;
}

bool FtpClient::DownloadRaw(const std::string& remoteName, const std::string& localPath,
                            uint64_t offset, uint64_t totalSize, const ProgressFn& progress) {
    uintptr_t ds = OpenPassive();
    if (ds == (uintptr_t)-1) return false;

    // 续传：发送 REST 设置偏移量
    if (offset > 0) {
        std::string r;
        int rc = SendCmd("REST " + std::to_string(offset), &r);
        if (rc != 350) {
            closesocket((SOCKET)ds);
            lastError_ = "REST 失败：" + r;
            return false;
        }
    }

    std::string resp;
    std::string argName;
    if (!EncodeName(remoteName, argName)) {
        closesocket((SOCKET)ds);
        return false;
    }
    int code = SendCmd("RETR " + argName, &resp);
    if (code != 150 && code != 125) {
        closesocket((SOCKET)ds);
        lastError_ = "下载失败：" + resp;
        return false;
    }

    // offset=0 截断重写；offset>0 追加（续传）
    const wchar_t* fmode = (offset == 0) ? L"wb" : L"ab";
    FILE* f = _wfopen(ToLongPathW(Utf8ToWidePath(localPath)).c_str(), fmode);
    if (!f) {
        closesocket((SOCKET)ds);
        lastError_ = "无法创建本地文件";
        return false;
    }
    char buf[65536];
    int n;
    uint64_t done = offset;
    bool recvOk = true;
    while ((n = recv((SOCKET)ds, buf, sizeof(buf), 0)) > 0) {
        fwrite(buf, 1, n, f);
        done += (uint64_t)n;
        if (progress) progress(done, totalSize);
    }
    if (n < 0) recvOk = false;   // 超时/错误≠EOF，不能当下载成功
    fclose(f);
    closesocket((SOCKET)ds);

    int finCode = ReadResponse();
    if (!recvOk) {
        lastError_ = "下载中断（接收数据失败）";
        return false;
    }
    if (finCode != 226 && finCode != 250) {
        lastError_ = "下载未完成（服务器响应异常）";
        return false;
    }
    return true;
}

uintptr_t FtpClient::OpenDownloadStream(const std::string& remoteName) {
    uintptr_t ds = OpenPassive();
    if (ds == (uintptr_t)-1) return (uintptr_t)-1;

    std::string resp;
    std::string argName;
    if (!EncodeName(remoteName, argName)) {
        closesocket((SOCKET)ds);
        return (uintptr_t)-1;
    }
    int code = SendCmd("RETR " + argName, &resp);
    if (code != 150 && code != 125) {
        closesocket((SOCKET)ds);
        lastError_ = "下载失败：" + resp;
        return (uintptr_t)-1;
    }
    return ds;
}

int FtpClient::ReadStreamData(uintptr_t stream, char* buf, int size) {
    if (stream == (uintptr_t)-1) return -1;
    int n = recv((SOCKET)stream, buf, size, 0);
    return n;   // >0 数据，0 结束，<0 错误
}

void FtpClient::CloseDownloadStream(uintptr_t stream) {
    if (stream == (uintptr_t)-1) return;
    closesocket((SOCKET)stream);
    // 读取传输完成响应（226 等）
    ReadResponse();
}

bool FtpClient::Upload(const std::string& localPath, const std::string& remoteName) {
    return Upload(localPath, remoteName, 0, nullptr);
}

bool FtpClient::Upload(const std::string& localPath, const std::string& remoteName,
                       uint64_t totalSize, const ProgressFn& progress,
                       const ProgressFn& hashProgress) {
    namespace fs = std::filesystem;

    // 1) 本地文件大小
    uint64_t localSize = totalSize;
    if (localSize == 0) {
        std::error_code ec;
        fs::path lp = fs::path(ToLongPathW(Utf8ToWidePath(localPath)));
        if (fs::is_regular_file(lp, ec))
            localSize = (uint64_t)fs::file_size(lp, ec);
        if (ec) localSize = 0;
    }

    // 2) 远程文件大小（若已存在）
    uint64_t remoteSize = GetRemoteSize(remoteName);
    bool remoteExists = (remoteSize != (uint64_t)-1);
    if (!remoteExists) remoteSize = 0;

    // 3) 根据模式决定行为
    bool useRest = (mode_ == TransferMode::Fast || mode_ == TransferMode::Balanced)
                   && supportRest_;
    bool useHash = (mode_ == TransferMode::Balanced || mode_ == TransferMode::Safe)
                   && supportXmd5_;

    // 远程已有且大小相同：哈希校验决定是否跳过
    if (remoteExists && remoteSize == localSize && localSize > 0) {
        if (useHash) {
            std::string localMd5 = ComputeLocalMd5(localPath, hashProgress);
            std::string remoteMd5 = GetRemoteMd5(remoteName);
            if (!localMd5.empty() && !remoteMd5.empty() && localMd5 == remoteMd5) {
                if (progress) progress(localSize, localSize);
                return true;   // 已完整上传且一致
            }
            // 不一致：全量重传
            remoteSize = 0;
        } else {
            if (progress) progress(localSize, localSize);
            return true;   // 大小相同即视为已上传
        }
    }

    // 4) 续传或全量上传
    uint64_t offset = 0;
    if (useRest && remoteExists && remoteSize < localSize) {
        offset = remoteSize;
    }

    bool ok = UploadRaw(localPath, remoteName, offset, localSize, progress);
    if (!ok) return false;

    // 5) 上传完成后哈希校验
    if (useHash && localSize > 0) {
        std::string localMd5 = ComputeLocalMd5(localPath, hashProgress);
        std::string remoteMd5 = GetRemoteMd5(remoteName);
        if (localMd5.empty() || remoteMd5.empty() || localMd5 != remoteMd5) {
            // 校验失败：全量重传一次
            ok = UploadRaw(localPath, remoteName, 0, localSize, progress);
            if (ok) {
                std::string md5b = ComputeLocalMd5(localPath, hashProgress);
                std::string rmd5b = GetRemoteMd5(remoteName);
                if (md5b.empty() || rmd5b.empty() || md5b != rmd5b) {
                    lastError_ = "上传完成但哈希校验失败";
                    return false;
                }
            }
        }
    }
    return ok;
}

bool FtpClient::UploadRaw(const std::string& localPath, const std::string& remoteName,
                          uint64_t offset, uint64_t totalSize, const ProgressFn& progress) {
    FILE* f = _wfopen(ToLongPathW(Utf8ToWidePath(localPath)).c_str(), L"rb");
    if (!f) {
        lastError_ = "无法打开本地文件";
        return false;
    }
    // 续传：本地文件指针移到 offset
    if (offset > 0) {
        if (_fseeki64(f, (long long)offset, SEEK_SET) != 0) {
            fclose(f);
            lastError_ = "本地文件定位失败";
            return false;
        }
    }

    uintptr_t ds = OpenPassive();
    if (ds == (uintptr_t)-1) {
        fclose(f);
        return false;
    }

    // 续传：发送 REST 设置服务器端偏移
    if (offset > 0) {
        std::string r;
        int rc = SendCmd("REST " + std::to_string(offset), &r);
        if (rc != 350) {
            closesocket((SOCKET)ds);
            fclose(f);
            lastError_ = "REST 失败：" + r;
            return false;
        }
    }

    std::string resp;
    std::string argName;
    if (!EncodeName(remoteName, argName)) {
        closesocket((SOCKET)ds);
        fclose(f);
        return false;
    }
    int code = SendCmd("STOR " + argName, &resp);
    if (code != 150 && code != 125) {
        closesocket((SOCKET)ds);
        fclose(f);
        lastError_ = "上传失败：" + resp;
        return false;
    }

    char buf[65536];
    size_t n;
    uint64_t done = offset;
    bool sendOk = true;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        int off = 0;
        while (off < (int)n) {
            int s2 = send((SOCKET)ds, buf + off, (int)n - off, 0);
            if (s2 <= 0) { sendOk = false; break; }
            off += s2;
        }
        if (!sendOk) break;
        done += (uint64_t)n;
        if (progress) progress(done, totalSize);
    }
    fclose(f);
    closesocket((SOCKET)ds);

    int finCode = ReadResponse();
    if (!sendOk) {
        lastError_ = "上传中断（发送数据失败）";
        return false;
    }
    if (finCode != 226 && finCode != 250) {
        lastError_ = "上传未完成（服务器响应异常）";
        return false;
    }
    return true;
}

bool FtpClient::RemoveFile(const std::string& name) {
    std::string resp;
    std::string argName;
    if (!EncodeName(name, argName)) return false;
    int code = SendCmd("DELE " + argName, &resp);
    if (code != 250) {
        lastError_ = "删除文件失败：" + resp;
        return false;
    }
    return true;
}

bool FtpClient::MakeDir(const std::string& name) {
    std::string resp;
    std::string argName;
    if (!EncodeName(name, argName)) return false;
    int code = SendCmd("MKD " + argName, &resp);
    if (code != 257) {
        lastError_ = "创建目录失败：" + resp;
        return false;
    }
    return true;
}

bool FtpClient::RemoveDir(const std::string& name) {
    std::string resp;
    std::string argName;
    if (!EncodeName(name, argName)) return false;
    int code = SendCmd("RMD " + argName, &resp);
    if (code != 250) {
        lastError_ = "删除目录失败：" + resp;
        return false;
    }
    return true;
}

bool FtpClient::Rename(const std::string& from, const std::string& to) {
    std::string resp;
    std::string argFrom, argTo;
    if (!EncodeName(from, argFrom) || !EncodeName(to, argTo)) return false;
    int code = SendCmd("RNFR " + argFrom, &resp);
    if (code != 350) {
        lastError_ = "重命名失败（RNFR）：" + resp;
        return false;
    }
    code = SendCmd("RNTO " + argTo, &resp);
    if (code != 250) {
        lastError_ = "重命名失败（RNTO）：" + resp;
        return false;
    }
    return true;
}

uint64_t FtpClient::GetRemoteSize(const std::string& remoteName) {
    std::string resp;
    std::string argName;
    if (!EncodeName(remoteName, argName)) return (uint64_t)-1;
    int code = SendCmd("SIZE " + argName, &resp);
    if (code != 213) {
        lastError_ = "获取大小失败：" + resp;
        return (uint64_t)-1;
    }
    // 213 <size>
    size_t p = resp.find(' ');
    if (p == std::string::npos) return (uint64_t)-1;
    lastError_.clear();      // 成功即清掉上一条陈旧错误
    return (uint64_t)std::strtoull(resp.c_str() + p + 1, nullptr, 10);
}

std::string FtpClient::GetRemoteMd5(const std::string& remoteName) {
    std::string resp;
    std::string argName;
    if (!EncodeName(remoteName, argName)) return "";
    int code = SendCmd("XMD5 " + argName, &resp);
    if (code != 213) {
        lastError_ = "获取MD5失败：" + resp;
        return "";
    }
    // 213 <md5hex>
    size_t p = resp.find(' ');
    if (p == std::string::npos) return "";
    return resp.substr(p + 1);
}

std::string FtpClient::ComputeLocalMd5(const std::string& localPath,
                                       const ProgressFn& progress) {
    return CalcMd5(Utf8ToWidePath(localPath), progress);
}
