#include "ftp_server.h"

#include "winlongpath.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include <vector>
#include <string>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cerrno>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <ctime>
#include <chrono>

namespace fs = std::filesystem;

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

// ---------------------------------------------------------------------------
// 工具函数
// ---------------------------------------------------------------------------

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string ToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::toupper(c); });
    return s;
}

// UTF-8 <-> UTF-16 转换（避免使用系统 ANSI 代码页，防止含特殊字符的
// 文件名在转换时抛出 "No mapping for the Unicode character..." 异常）
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

// 按指定代码页解码字节串（用于文件名编码兼容）
static std::wstring DecodeBytesToWide(const std::string& s, UINT cp) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring w(len, L'\0');
    MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

// 是否是合法的 UTF-8 字节串
static bool IsValidUtf8Bytes(const std::string& s) {
    if (s.empty()) return true;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                               s.c_str(), (int)s.size(), nullptr, 0) > 0;
}

static size_t CountWChar(const std::wstring& s, wchar_t ch) {
    size_t n = 0;
    for (wchar_t c : s) if (c == ch) ++n;
    return n;
}

// 临时诊断：记录客户端原始参数与解码结果，便于定位 Explorer 的混合编码。
static std::string HexBytes(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) {
        out.push_back(hex[c >> 4]);
        out.push_back(hex[c & 0x0F]);
    }
    return out;
}
static void DumpArgEncoding(const std::string& raw, int mode, bool valid,
                            size_t qa, size_t qm, const std::string& result) {
    // 纯 ASCII 参数（绝大多数命令，如 "/"、"LIST"）不记录：它们不可能
    // 有编码问题，写进日志只会让 arghex.log 无限增长。
    bool nonAscii = false;
    for (unsigned char c : raw) {
        if (c >= 0x80) { nonAscii = true; break; }
    }
    if (!nonAscii) return;
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path logPath =
        std::filesystem::path(exePath).parent_path() / L"arghex.log";
    std::ofstream f(logPath, std::ios::app);
    if (!f) return;
    f << "mode=" << mode << " valid=" << (valid ? 1 : 0)
      << " qa=" << qa << " qm=" << qm
      << " raw=" << HexBytes(raw)
      << " result=" << HexBytes(result) << "\n";
}

// 返回从 i 开始的合法 UTF-8 序列长度（1..4）；非法返回 0。
// 仅用于“混合编码”兜底：客户端可能前半段发 UTF-8、后半段发 GBK，
// 或夹带个别非法字节；严格 IsValidUtf8Bytes 会整串判死，导致回退到
// GBK 后“（）”变成 '?'，Windows 创建文件失败（errno=22）。
static int Utf8SeqLen(const std::string& s, size_t i) {
    if (i >= s.size()) return 0;
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) return 1;
    int n = 0;
    unsigned int cp = 0;
    if ((c & 0xE0) == 0xC0) { n = 2; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 3; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 4; cp = c & 0x07; }
    else return 0;
    if (i + (size_t)n > s.size()) return 0;
    for (int k = 1; k < n; ++k) {
        unsigned char cc = (unsigned char)s[i + k];
        if ((cc & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (cc & 0x3F);
    }
    if (n == 2 && cp < 0x80) return 0;
    if (n == 3 && cp < 0x800) return 0;
    if (n == 4 && cp < 0x10000) return 0;
    if (cp > 0x10FFFF) return 0;
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;   // 代理区不是合法 UTF-8
    return n;
}

// 原始参数的开头是否是“可信的 UTF-8 前缀”。
// 中文 UTF-8 是 3 字节；干净的 GBK 文件名通常首字节就非法 UTF-8，
// 或最多是 2 字节的巧合（如“应”GBK=D3 A6 恰好是 UTF-8 的 Ӧ），
// 不能据此整串改走混合解码，否则会破坏正常 GBK 文件名。
// 只有开头连续合法段中出现 3/4 字节 UTF-8 序列时，才认为客户端
// 确实在发 UTF-8（例如“UTF-8 前缀 + GBK 尾部”的混合情况）。
static bool HasUtf8ThreeBytePrefix(const std::string& s) {
    size_t i = 0;
    size_t threeByteSeqs = 0;
    while (i < s.size()) {
        int n = Utf8SeqLen(s, i);
        if (n == 0) break;
        if (n >= 3) ++threeByteSeqs;
        i += (size_t)n;
    }
    // 至少两个 3/4 字节序列才认定是 UTF-8 前缀：单个 2 字节“巧合”
    // （如“应”GBK=D3 A6）很常见，单个 3 字节巧合则极少。
    return threeByteSeqs >= 2;
}

// 逐段解码：优先按 UTF-8 解释；不合法的那一段按系统 ANSI（GBK）解释。
// 这样能同时兼容“UTF-8 前缀 + GBK 文件名的尾部”以及个别坏字节。
static std::wstring DecodeMixedToWide(const std::string& s) {
    std::wstring out;
    size_t i = 0;
    while (i < s.size()) {
        int n = Utf8SeqLen(s, i);
        // 混合编码时只把 3/4 字节序列当作 UTF-8。
        // 2 字节序列歧义太大：GBK 的“应”=D3 A6 恰好也是合法 UTF-8（Ӧ），
        // 若在这里优先 UTF-8，会把 GBK 前缀“应用”解成“Ӧ用”。
        // 中文 UTF-8 基本都是 3 字节；2 字节交给 GBK 解码更安全。
        if (n >= 3) {
            wchar_t wbuf[4]{};
            int wn = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         s.data() + i, n, wbuf, 4);
            if (wn > 0) {
                out.append(wbuf, (size_t)wn);
                i += (size_t)n;
                continue;
            }
        }
        int n2 = 1;
        if (IsDBCSLeadByteEx(CP_ACP, (BYTE)s[i]) && i + 1 < s.size())
            n2 = 2;
        wchar_t wbuf[4]{};
        int wn = MultiByteToWideChar(CP_ACP, 0, s.data() + i, n2, wbuf, 4);
        if (wn == 1 && wbuf[0] == L'?' && n2 == 2 &&
            (unsigned char)s[i + 1] < 0x80) {
            // 引导字节 + ASCII 不是合法 GBK 双字节，别把后面的 ASCII 吃掉
            n2 = 1;
            wn = MultiByteToWideChar(CP_ACP, 0, s.data() + i, 1, wbuf, 4);
        }
        if (wn > 0) out.append(wbuf, (size_t)wn);
        else out.push_back(L'?');
        i += (size_t)n2;
    }
    return out;
}

// 内部 UTF-8 本地路径 -> \\?\ 长路径（fs::path 形式）。
// 所有实际文件 I/O 统一经过此函数，兼容超过 260 字符的路径。
static fs::path LongLocalPath(const std::string& utf8) {
    return fs::path(ToLongPathW(Utf8ToWide(utf8)));
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len,
                        nullptr, nullptr);
    return s;
}

// 把 fs::path 安全转换为 UTF-8 字符串（始终走宽字符接口）
static std::string PathToUtf8(const fs::path& p) {
    return WideToUtf8(p.wstring());
}

// ---------------------------------------------------------------------------
// Windows 文件名兼容处理（仅在“其他设置 → 默认编码”选择兼容模式时启用）
//
// 客户端发来的文件名里经常夹着 Windows 不允许的字符：
//   1) 编码转换失败被写成 '?'：典型是 Windows 资源管理器把 UTF-8 名字当成
//      GBK 转了一圈，'题'(UTF-8 E9 A2 98) 变成 E9 A2 3F，'）'(EF BC 89)
//      变成 EF BC 3F，中间的 ASCII 字符还会被非法 GBK 尾字节吞掉；
//   2) 老客户端/命令行客户端直接发 ':' '*' '"' '<' '>' '|'。
// CreateFileW 遇到这些字符返回 ERROR_INVALID_NAME(123)，MSVC 的 ofstream
// 再把它映射成 errno=22(EINVAL)，客户端只能看到 "550 Cannot create file"，
// 整个文件都传不上来（连重试也一样）。
//
// 默认（自动识别 / 仅 UTF-8 / 仅 GBK）**不做任何替换**，保持标准 FTP 行为：
// 名字里有非法字符就失败，并给出明确原因。只有显式选下面两种兼容模式才改写：
//
//   3 = 兼容模式（尝试修复）：非法字符 -> 同形全角字符（'?' -> '？'），
//       名字仍然可读，语义尽量接近；
//   4 = 兼容模式（跳过解码）：非法字符 -> 空格，直接“抹掉”这段无法解码的
//       文字，名字里不会留下奇怪的符号。
//
// 替换只作用于“要新建的名字”，不会改已存在的文件；每次替换都会记一条
// 警告日志，便于事后核对/改名。
// ---------------------------------------------------------------------------

// 非法文件名字符 -> 同形全角字符；返回 0 表示该字符合法、无需替换。
// 兼容模式 4 只用它判断“是否非法”，具体替换成什么由调用方决定。
static wchar_t FullWidthForIllegalNameChar(wchar_t c) {
    switch (c) {
    case L'?':  return L'？';   // U+FF1F
    case L'*':  return L'＊';   // U+FF0A
    case L':':  return L'：';   // U+FF1A
    case L'"':  return L'＂';   // U+FF02
    case L'<':  return L'＜';   // U+FF1C
    case L'>':  return L'＞';   // U+FF1E
    case L'|':  return L'｜';   // U+FF5C
    case L'\\': return L'＼';   // U+FF3C
    case L'/':  return L'／';   // U+FF0F
    default:
        // 0x00~0x1F 控制字符在文件名里同样非法
        if (c < 0x20) return L'_';
        return 0;
    }
}

static bool IsReservedDeviceName(const std::wstring& name) {
    // CON PRN AUX NUL COM1~COM9 LPT1~LPT9；带扩展名同样被系统占用
    std::wstring base = name.substr(0, name.find(L'.'));
    while (!base.empty() && base.back() == L' ') base.pop_back();
    for (auto& c : base) {
        if (c >= L'a' && c <= L'z') c = (wchar_t)(c - L'a' + L'A');
    }
    static const wchar_t* kReserved[] = {
        L"CON", L"PRN", L"AUX", L"NUL",
        L"COM1", L"COM2", L"COM3", L"COM4", L"COM5",
        L"COM6", L"COM7", L"COM8", L"COM9",
        L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5",
        L"LPT6", L"LPT7", L"LPT8", L"LPT9"
    };
    for (const wchar_t* r : kReserved) {
        if (base == r) return true;
    }
    return false;
}

// 按兼容模式修复单个文件名（不含目录部分）。changed 回传是否发生过替换。
// fixMode：3=替换为全角同形字符；4=替换为空格；其它值原样返回。
static std::wstring FixWinFileName(const std::wstring& name, int fixMode,
                                   bool* changed) {
    if (changed) *changed = false;
    if (fixMode != 3 && fixMode != 4) return name;

    std::wstring out = name;
    for (auto& c : out) {
        wchar_t full = FullWidthForIllegalNameChar(c);
        if (!full) continue;                       // 合法字符
        c = (fixMode == 3) ? full : L' ';          // 3=全角同形，4=空格
        if (changed) *changed = true;
    }
    // Windows 会静默丢掉结尾的空格和点：'a.' 与 'a' 实际同名，客户端会
    // 看到“上传成功但找不到文件”。这里直接裁掉，保证名字与磁盘一致
    // （兼容模式 4 用空格替换，结尾的空格尤其要裁掉）。
    while (!out.empty() && (out.back() == L' ' || out.back() == L'.')) {
        out.pop_back();
        if (changed) *changed = true;
    }
    if (out.empty()) {
        out = L"_";
        if (changed) *changed = true;
    }
    if (IsReservedDeviceName(out)) {
        out = L"_" + out;
        if (changed) *changed = true;
    }
    return out;
}

// 把绝对本地路径（UTF-8）的最后一段按兼容模式修复成合法文件名。
// 返回 true 表示文件名被改写，out 为新的 UTF-8 路径。
static bool FixCreatePath(const std::string& fullUtf8, int fixMode,
                          std::string& outUtf8) {
    outUtf8 = fullUtf8;
    if (fixMode != 3 && fixMode != 4) return false;
    try {
        fs::path p(Utf8ToWide(fullUtf8));
        std::wstring leaf = p.filename().wstring();
        if (leaf.empty()) return false;
        bool changed = false;
        std::wstring fixedLeaf = FixWinFileName(leaf, fixMode, &changed);
        if (!changed) return false;
        outUtf8 = PathToUtf8(p.parent_path() / fs::path(fixedLeaf));
        return true;
    } catch (...) {
        return false;
    }
}

// 兼容模式下文件名已被改写，客户端随后仍会用原始（含 '?' 的）名字发
// SIZE/RETR/DELE/RNFR。这里在“查不到”时回退到修复后的名字，避免出现
// “刚上传成功，紧接着 550 File not found”的假故障。
// fixMode 为 0（非兼容模式）时不做回退，保持标准行为。
static bool ResolvePath(const std::string& root, const std::string& ftpPath,
                        std::string& out);   // 定义在后面
static bool ResolveExisting(const std::string& root, const std::string& ftpPath,
                            int fixMode, std::string& out) {
    if (!ResolvePath(root, ftpPath, out)) return false;
    if (fixMode != 3 && fixMode != 4) return true;
    std::error_code ec;
    if (fs::exists(LongLocalPath(out), ec)) return true;
    std::string alt;
    if (!FixCreatePath(out, fixMode, alt)) return true;   // 名字本来就合法
    std::error_code ec2;
    if (fs::exists(LongLocalPath(alt), ec2)) out = alt;
    return true;
}

// ---------------------------------------------------------------------------
// 兼容模式专用：修复 Windows 资源管理器在“中文括号”上造成的损坏。
//
// 资源管理器把 UTF-8 名字按 GBK 转一圈再发出来，全角括号
//     （ = EF BC 88      ） = EF BC 89
// 的第三个字节会被替换成 '?'(0x3F)，于是服务端收到 EF BC 3F，按 GBK 解码
// 就成了“锛”，后面那个 '?' 还会被当成 Windows 非法文件名字符。
//
// 第三个字节已经丢失，没法直接判断是左括号还是右括号，只能按配对推断：
//   · 前面还有没配对的“（” → 这一处是“）”
//   · 否则                 → 这一处是“（”
//
// 模式 4（跳过解码）不猜内容：整段 EF BC 3F 直接抹成一个空格，符合“解不
// 出来的文字跳过”的语义，名字里不会留下“锛”这种怪字。
//
// 注意：括号后面紧跟的那个字符，在客户端做 GBK 转换时就已经被非法尾字节
// 吞掉了（如“（1）”丢成“（”），服务端无法恢复；这里只负责把括号本身还原
// 成可读、可创建的文件名。
// ---------------------------------------------------------------------------
static std::string RepairTruncatedParens(const std::string& raw, int fixMode) {
    std::string out;
    out.reserve(raw.size() + 8);
    int depth = 0;   // 尚未配对的左括号数量
    size_t i = 0;
    while (i < raw.size()) {
        const unsigned char c0 = (unsigned char)raw[i];
        // 被资源管理器转坏的全角括号：EF BC 3F
        if (c0 == 0xEF && i + 2 < raw.size() &&
            (unsigned char)raw[i + 1] == 0xBC && (unsigned char)raw[i + 2] == 0x3F) {
            if (fixMode == 4) {
                out.push_back(' ');                    // 跳过解码：整段抹掉
            } else if (depth > 0) {
                out += "\xEF\xBC\x89";                 // ）
                --depth;
            } else {
                out += "\xEF\xBC\x88";                 // （
                ++depth;
            }
            i += 3;
            continue;
        }
        // 完好的全角括号：只用来维护配对计数（UTF-8 与 GBK 两种编码都认）
        if (c0 == 0xEF && i + 2 < raw.size() && (unsigned char)raw[i + 1] == 0xBC) {
            const unsigned char c2 = (unsigned char)raw[i + 2];
            if (c2 == 0x88) { ++depth; out.append(raw, i, 3); i += 3; continue; }
            if (c2 == 0x89) { if (depth > 0) --depth; out.append(raw, i, 3); i += 3; continue; }
        }
        if (c0 == 0xA3 && i + 1 < raw.size()) {
            const unsigned char c1 = (unsigned char)raw[i + 1];
            if (c1 == 0xA8) { ++depth; out.append(raw, i, 2); i += 2; continue; }
            if (c1 == 0xA9) { if (depth > 0) --depth; out.append(raw, i, 2); i += 2; continue; }
        }
        out.push_back((char)c0);
        ++i;
    }
    return out;
}

// 把文件/文件夹移动到系统回收站（而非彻底删除）。
// 成功返回 true。使用 SHFileOperationW，需要双 null 结尾的路径列表。
static bool MoveToRecycleBin(const std::wstring& fullPath) {
    auto tryOp = [](const std::wstring& p) -> bool {
        // 路径必须以双 '\0' 结尾
        std::wstring from = p;
        from.push_back(L'\0');
        from.push_back(L'\0');

        SHFILEOPSTRUCTW op{};
        op.hwnd = nullptr;
        op.wFunc = FO_DELETE;
        op.pFrom = from.c_str();
        op.pTo = nullptr;
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI |
                    FOF_SILENT | FOF_NOCONFIRMMKDIR;
        int rc = SHFileOperationW(&op);
        return rc == 0 && !op.fAnyOperationsAborted;
    };

    // SHFileOperationW 不接受 \\?\ 前缀，先还原为标准路径
    static const wchar_t kPrefix[] = L"\\\\?\\";
    std::wstring stdPath = fullPath;
    if (stdPath.size() >= 4 &&
        _wcsnicmp(stdPath.c_str(), kPrefix, 4) == 0) {
        // \\?\UNC\server\share -> \\server\share
        if (stdPath.size() >= 8 &&
            _wcsnicmp(stdPath.c_str() + 4, L"UNC\\", 4) == 0) {
            stdPath = L"\\\\" + stdPath.substr(8);
        } else {
            stdPath = stdPath.substr(4);
        }
    }

    // 1) 标准路径直接尝试（短路径、常规长度在此成功）
    if (tryOp(stdPath)) return true;

    // 2) 超长路径兜底：转换为 8.3 短文件名后再移入回收站
    //    （依赖卷上的短名生成，Windows 默认开启）
    DWORD need = GetShortPathNameW(stdPath.c_str(), nullptr, 0);
    if (need > 0) {
        std::wstring shortPath(need, L'\0');
        DWORD got = GetShortPathNameW(stdPath.c_str(), &shortPath[0], need);
        if (got > 0) {
            shortPath.resize(got);
            if (tryOp(shortPath)) return true;
        }
    }
    return false;
}

// 将 FTP 路径（以 / 分隔）转换为本地绝对路径，并做目录穿越防护
// 全程使用 error_code 版本，避免任何 filesystem 异常导致崩溃
static bool ResolvePath(const std::string& root, const std::string& ftpPath,
                        std::string& out) {
    try {
        std::string rel = ftpPath;
        // 统一分隔符
        std::replace(rel.begin(), rel.end(), '\\', '/');
        // 去掉开头的 /
        while (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());

        std::error_code ec;
        // 根路径也做 canonical：共享目录是 junction/symlink 时，子路径会被
        // 解析到真实目录，如果根不 canonical，前缀必然对不上（全部 550）。
        fs::path rootOrig = fs::path(Utf8ToWide(root));
        fs::path rootAbs = fs::weakly_canonical(fs::absolute(rootOrig, ec), ec);
        if (ec) {
            ec.clear();
            rootAbs = fs::absolute(rootOrig, ec);
            if (ec) rootAbs = rootOrig;
            rootAbs = rootAbs.lexically_normal();
        }
        fs::path combined = rootAbs / fs::path(Utf8ToWide(rel));

        // 规范化（处理 .. 等）；weakly_canonical 失败时退回词法规范化
        ec.clear();
        fs::path normalized = fs::weakly_canonical(combined, ec);
        if (ec) {
            normalized = combined.lexically_normal();
            ec.clear();
        }

        std::string rootStr = PathToUtf8(rootAbs);
        // 去掉根路径末尾的分隔符：盘符根 "D:\" -> "D:"，普通目录
        // "D:\share\" -> "D:\share"。否则下面的“前缀 + 分隔符边界”检查
        // 会把盘符根/带尾斜杠的共享路径下的所有子路径都判成越界。
        while (rootStr.size() > 1 &&
               (rootStr.back() == '\\' || rootStr.back() == '/')) {
            rootStr.pop_back();
        }
        std::string normStr = PathToUtf8(normalized);

        // 目录穿越检查（大小写不敏感）
        if (normStr.size() < rootStr.size() ||
            _strnicmp(normStr.c_str(), rootStr.c_str(), rootStr.size()) != 0) {
            return false;
        }
        // 边界检查：前缀匹配之后必须紧跟分隔符或恰好结束。
        // 否则共享根 D:\share 会被 D:\shareevil（同名前缀的兄弟目录）绕过。
        if (normStr.size() > rootStr.size()) {
            char c = normStr[rootStr.size()];
            if (c != '\\' && c != '/') return false;
        }
        out = normStr;
        return true;
    } catch (...) {
        return false;
    }
}

// 计算 full 相对于 root 的 FTP 路径（以 / 开头），跨盘符时安全回退
static std::string RelativeFtpPath(const std::string& root,
                                   const std::string& full) {
    try {
        std::error_code ec;
        fs::path rel = fs::relative(LongLocalPath(full),
                                    fs::absolute(fs::path(Utf8ToWide(root)), ec), ec);
        if (ec) return "/";
        std::string r = PathToUtf8(rel);
        std::replace(r.begin(), r.end(), '\\', '/');
        if (r == "." || r.empty()) return "/";
        if (r.rfind("..", 0) == 0) return "/";  // 越界，回退到根
        return "/" + r;
    } catch (...) {
        return "/";
    }
}

// 格式化文件时间为标准 FTP LIST 格式（Unix ls -l 风格）
// 半年内的文件显示 "Mon DD HH:MM"，更早的显示 "Mon DD  YYYY"
static std::string FormatListTime(const fs::path& p) {
    std::error_code ec;
    auto ftime = fs::last_write_time(p, ec);
    std::time_t t = std::time(nullptr);
    if (!ec) {
        // 转换为 time_t（近似）
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        t = std::chrono::system_clock::to_time_t(sctp);
    }
    std::tm tmv{};
    localtime_s(&tmv, &t);
    char buf[64];
    static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                   "Jul","Aug","Sep","Oct","Nov","Dec"};
    // 判断是否在最近 6 个月内
    std::time_t now = std::time(nullptr);
    bool recent = (now - t) < (180LL * 24 * 3600) && t <= now;
    if (recent) {
        std::snprintf(buf, sizeof(buf), "%s %2d %02d:%02d",
                      months[tmv.tm_mon], tmv.tm_mday,
                      tmv.tm_hour, tmv.tm_min);
    } else {
        std::snprintf(buf, sizeof(buf), "%s %2d  %04d",
                      months[tmv.tm_mon], tmv.tm_mday, tmv.tm_year + 1900);
    }
    return buf;
}

// 计算文件的 MD5（小写十六进制字符串）。流式读取，不占大内存。
// 成功返回 true。使用 BCrypt（系统自带，无第三方依赖）。
static bool ComputeFileMd5(const fs::path& filePath, std::string& outHex) {
    outHex.clear();
    std::ifstream in(filePath, std::ios::binary);
    if (!in) return false;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_MD5_ALGORITHM,
                                                    nullptr, 0))) return false;
    if (!BCRYPT_SUCCESS(BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    std::vector<char> buf(64 * 1024);
    bool ok = true;
    while (in.read(buf.data(), (std::streamsize)buf.size()) || in.gcount() > 0) {
        int got = (int)in.gcount();
        if (!BCRYPT_SUCCESS(BCryptHashData(hHash, (PUCHAR)buf.data(), got, 0))) {
            ok = false; break;
        }
    }
    in.close();

    if (ok) {
        BYTE digest[16];
        if (BCRYPT_SUCCESS(BCryptFinishHash(hHash, digest, sizeof(digest), 0))) {
            char hex[33];
            for (int i = 0; i < 16; ++i)
                std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
            hex[32] = '\0';
            outHex = hex;
        } else {
            ok = false;
        }
    }
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

// ---------------------------------------------------------------------------
// FtpPermissions
// ---------------------------------------------------------------------------

std::string FtpPermissions::ToString() const {
    std::string s;
    if (read)   s += "读 ";
    if (write)  s += "写 ";
    if (del)    s += "删 ";
    if (mkdir)  s += "建目录 ";
    if (rmdir)  s += "删目录 ";
    if (rename) s += "重命名 ";
    if (list)   s += "列目录 ";
    if (s.empty()) s = "无";
    return s;
}

std::string FtpPermissions::ToShortString() const {
    std::string s;
    s += read   ? 'R' : '-';
    s += write  ? 'W' : '-';
    s += del    ? 'D' : '-';
    s += mkdir  ? 'M' : '-';
    s += rmdir  ? 'X' : '-';
    s += rename ? 'N' : '-';
    s += list   ? 'L' : '-';
    return s;
}

// 归一化 FTP 路径用于权限匹配：折叠重复的 '/'、'.' 和 '..'。
// 权限规则必须和 ResolvePath 对同一路径的理解一致，否则客户端用
// "//dir/file"、"/dir/../file" 这类写法就能绕过或误伤单文件规则。
static std::string NormalizeMatchPath(const std::string& ftpPath) {
    std::string s = ftpPath;
    std::replace(s.begin(), s.end(), '\\', '/');
    std::vector<std::string> parts;
    size_t i = 0;
    for (;;) {
        size_t j = s.find('/', i);
        if (j == std::string::npos) j = s.size();
        std::string seg = s.substr(i, j - i);
        if (seg == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!seg.empty() && seg != ".") {
            parts.push_back(seg);
        }
        if (j == s.size()) break;
        i = j + 1;
    }
    std::string out;
    for (const auto& p : parts) {
        out += '/';
        out += p;
    }
    return out.empty() ? "/" : out;
}

// Windows 路径大小写不敏感，权限规则也必须按大小写不敏感匹配
static bool EqualsNoCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           _strnicmp(a.c_str(), b.c_str(), a.size()) == 0;
}

// 运行时更新权限：GUI 修改勾选/高级规则后立即调用，不必重启服务。
// 会话线程每条命令都通过 QueryEffectivePerms 读取，因此现有连接也生效。
void FtpServer::UpdatePermissions(bool advancedMode,
                                  const FtpPermissions& perms,
                                  const std::vector<FilePermEntry>& filePerms) {
    std::lock_guard<std::mutex> lk(permMutex_);
    advancedMode_ = advancedMode;
    perms_ = perms;
    filePerms_ = filePerms;
}

FtpPermissions FtpServer::QueryEffectivePerms(const std::string& ftpPath) const {
    std::lock_guard<std::mutex> lk(permMutex_);
    if (!advancedMode_) return perms_;

    std::string p = NormalizeMatchPath(ftpPath);
    const FilePermEntry* best = nullptr;
    size_t bestLen = 0;
    for (const auto& fp : filePerms_) {
        std::string rp = NormalizeMatchPath(fp.path);
        bool match = EqualsNoCase(p, rp) ||
                     (p.size() > rp.size() &&
                      _strnicmp(p.c_str(), rp.c_str(), rp.size()) == 0 &&
                      (rp == "/" || p[rp.size()] == '/'));
        if (match && rp.size() >= bestLen) {
            best = &fp;
            bestLen = rp.size();
        }
    }
    if (best) return best->perms;
    return perms_;
}

bool FtpServer::QueryHidden(const std::string& ftpPath) const {
    std::lock_guard<std::mutex> lk(permMutex_);
    if (!advancedMode_) return false;

    std::string p = NormalizeMatchPath(ftpPath);
    for (const auto& fp : filePerms_) {
        if (!fp.hidden) continue;
        if (EqualsNoCase(p, NormalizeMatchPath(fp.path))) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// FtpServer
// ---------------------------------------------------------------------------

FtpServer::FtpServer() {}

FtpServer::~FtpServer() {
    Stop();
}

// 判断是否为私有局域网地址（192.168.x.x / 10.x.x.x / 172.16-31.x.x）
static bool IsPrivateLanIp(const std::string& ip) {
    if (ip.rfind("192.168.", 0) == 0) return true;
    if (ip.rfind("10.", 0) == 0) return true;
    if (ip.rfind("172.", 0) == 0) {
        // 172.16.0.0 - 172.31.255.255
        int second = atoi(ip.c_str() + 4);
        if (second >= 16 && second <= 31) return true;
    }
    return false;
}

// 判断网卡描述里是否包含关键字（大小写不敏感，仅用于 ASCII 关键字）
static bool DescContainsNoCase(const wchar_t* desc, const wchar_t* key) {
    if (!desc || !key || !*key) return false;
    size_t n = wcslen(key);
    for (const wchar_t* p = desc; *p; ++p) {
        if (_wcsnicmp(p, key, n) == 0) return true;
    }
    return false;
}

// 判断是否是 TUN/TAP/VPN/虚拟网卡。代理软件（Clash TUN、WireGuard 等）
// 创建的网卡往往也有 10.x/192.168.x 地址，不能作为对外的“本机 IP”。
static bool IsVirtualAdapter(const IP_ADAPTER_ADDRESSES* aa) {
    if (!aa) return false;
    // IF_TYPE_PPP = 23, IF_TYPE_SOFTWARE_LOOPBACK = 24, IF_TYPE_TUNNEL = 131
    if (aa->IfType == 23 || aa->IfType == 24 || aa->IfType == 131) return true;
    static const wchar_t* keys[] = {
        L"TAP-WINDOWS", L"TAP-WIN32", L"WINTUN", L"TUNNEL",
        L"WIREGUARD", L"OPENVPN", L"VPN", L"CLASH", L"TAILSCALE",
        L"ZEROTIER", L"VIRTUALBOX", L"VMWARE", L"HYPER-V", L"LOOPBACK",
        L"VETHERNET", L"WSL", L"DOCKER", L"DEFAULT SWITCH",
    };
    for (const wchar_t* k : keys) {
        if (DescContainsNoCase(aa->Description, k)) return true;
    }
    return false;
}

std::string FtpServer::GetLocalIp() {
    // 方法一：枚举所有网卡，优先返回真实网卡上的私有局域网地址。
    // 跳过 TUN/TAP/VPN 虚拟网卡，避免代理软件把地址拐到 10.0.0.2。
    {
        ULONG bufLen = 15000;
        std::vector<BYTE> buf(bufLen);
        ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                      GAA_FLAG_SKIP_DNS_SERVER;
        ULONG ret = GetAdaptersAddresses(AF_INET, flags, nullptr,
                                         (IP_ADAPTER_ADDRESSES*)buf.data(), &bufLen);
        if (ret == ERROR_BUFFER_OVERFLOW) {
            buf.resize(bufLen);
            ret = GetAdaptersAddresses(AF_INET, flags, nullptr,
                                       (IP_ADAPTER_ADDRESSES*)buf.data(), &bufLen);
        }
        if (ret == NO_ERROR) {
            std::string fallback;         // 真实网卡上的非私有地址
            std::string virtualFallback;  // 只有虚拟网卡时的兜底
            for (auto* aa = (IP_ADAPTER_ADDRESSES*)buf.data(); aa; aa = aa->Next) {
                // 跳过未启用的网卡
                if (aa->OperStatus != IfOperStatusUp) continue;
                const bool virt = IsVirtualAdapter(aa);
                for (auto* ua = aa->FirstUnicastAddress; ua; ua = ua->Next) {
                    auto* sa = (sockaddr_in*)ua->Address.lpSockaddr;
                    char ip[INET_ADDRSTRLEN] = {0};
                    inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
                    std::string s(ip);
                    if (s.rfind("127.", 0) == 0 || s.rfind("169.254.", 0) == 0)
                        continue;
                    if (!virt && IsPrivateLanIp(s)) return s;  // 真实局域网优先
                    if (!virt && fallback.empty()) fallback = s;
                    if (virtualFallback.empty()) virtualFallback = s;
                }
            }
            if (!fallback.empty()) return fallback;
            if (!virtualFallback.empty()) return virtualFallback;
        }
    }

    // 方法二：通过 UDP 连接外部地址，获取本机出口 IP
    {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s != INVALID_SOCKET) {
            sockaddr_in remote{};
            remote.sin_family = AF_INET;
            remote.sin_port = htons(53);
            inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);
            if (connect(s, (sockaddr*)&remote, sizeof(remote)) == 0) {
                sockaddr_in local{};
                int len = sizeof(local);
                if (getsockname(s, (sockaddr*)&local, &len) == 0) {
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip));
                    std::string result(ip);
                    closesocket(s);
                    if (result.rfind("127.", 0) != 0 && result != "0.0.0.0")
                        return result;
                }
            }
            closesocket(s);
        }
    }

    // 方法三：gethostname 回退
    {
        char hostname[256] = {0};
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            struct addrinfo hints{}, *res = nullptr;
            hints.ai_family = AF_INET;
            if (getaddrinfo(hostname, nullptr, &hints, &res) == 0) {
                for (auto p = res; p; p = p->ai_next) {
                    auto* sa = (struct sockaddr_in*)p->ai_addr;
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
                    std::string s(ip);
                    if (s.rfind("127.", 0) != 0 && s.rfind("169.254.", 0) != 0) {
                        freeaddrinfo(res);
                        return s;
                    }
                }
                freeaddrinfo(res);
            }
        }
    }
    return "127.0.0.1";
}

bool FtpServer::Start(const std::string& bindIp, uint16_t port,
                      const std::string& rootPath, const FtpPermissions& perms,
                      uint16_t pasvPortStart, uint16_t pasvPortEnd,
                      const std::vector<std::string>& allowedIps,
                      LogCallback logger,
                      bool advancedMode,
                      const std::vector<FilePermEntry>& filePerms,
                      const PerfOptions& perf,
                      const SecurityOptions& security,
                      ConfirmCallback confirm,
                      int fileNameEncoding) {
    if (running_.load()) return false;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        if (logger) logger(2, "WSAStartup 失败");
        return false;
    }

    rootPath_ = rootPath;
    {
        std::lock_guard<std::mutex> lk(permMutex_);
        perms_ = perms;
        advancedMode_ = advancedMode;
        filePerms_ = filePerms;
    }
    pasvStart_ = pasvPortStart;
    pasvEnd_ = pasvPortEnd;
    bindIp_ = bindIp;
    if (fileNameEncoding < 0 || fileNameEncoding > 4) fileNameEncoding = 0;
    fileNameEncoding_.store(fileNameEncoding);
    stopFlag_.store(false);   // 复位停止标志（支持 Stop 后重新 Start）
    allowedIps_ = allowedIps;
    logger_ = logger;
    perf_ = perf;
    security_ = security;
    confirm_ = confirm;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        if (logger) logger(2, "创建套接字失败");
        WSACleanup();
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (bindIp.empty() || bindIp == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, bindIp.c_str(), &addr.sin_addr) != 1) {
        if (logger) logger(2, "无效的绑定地址: " + bindIp);
        closesocket(s);
        WSACleanup();
        return false;
    }

    if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        if (logger) logger(2, "绑定 " + bindIp + ":" + std::to_string(port) +
                                  " 失败（网卡不存在或端口被占用）");
        closesocket(s);
        WSACleanup();
        return false;
    }

    if (listen(s, SOMAXCONN) == SOCKET_ERROR) {
        if (logger) logger(2, "监听失败");
        closesocket(s);
        WSACleanup();
        return false;
    }

    listenSock_ = (uintptr_t)s;
    running_.store(true);
    acceptThread_ = std::thread(&FtpServer::AcceptLoop, this);

    if (logger) {
        logger(0, "FTP 服务启动成功 | 监听: " +
                  (bindIp.empty() || bindIp == "0.0.0.0"
                       ? std::string("所有网卡 (0.0.0.0)")
                       : bindIp) + " | 端口: " +
                  std::to_string(port));
        logger(0, "共享路径: " + rootPath);
        logger(0, "权限: " + perms.ToString());
        logger(0, "文件名编码: " +
                  (fileNameEncoding == 1 ? std::string("仅 UTF-8")
                   : fileNameEncoding == 2 ? std::string("仅 GBK/本地编码")
                   : fileNameEncoding == 3 ? std::string("兼容模式(尝试修复)：非法字符替换为全角字符")
                   : fileNameEncoding == 4 ? std::string("兼容模式(跳过解码)：非法字符替换为空格")
                                           : std::string("自动识别")));
        if (advancedMode_) {
            logger(0, "高级模式: 已启用（按文件/文件夹单独控制权限，共 " +
                      std::to_string(filePerms_.size()) + " 条规则）");
        }
        logger(0, "被动模式端口范围: " + std::to_string(pasvStart_) + "-" +
                  std::to_string(pasvEnd_));
        logger(0, "性能参数: 超时=" +
                  (perf_.dataTimeoutSec > 0 ? std::to_string(perf_.dataTimeoutSec) + "秒" : std::string("不限")) +
                  " | TCP_NODELAY=" + (perf_.tcpNoDelay ? "开" : "关") +
                  " | 内核缓冲=" + std::to_string(perf_.sockBufKB) + "KB" +
                  " | 传输缓冲=" + std::to_string(perf_.appBufKB) + "KB" +
                  " | 单文件上限=" + (perf_.maxFileSizeMB > 0 ? std::to_string(perf_.maxFileSizeMB) + "MB" : std::string("不限")) +
                  " | 单IP并发=" + (perf_.maxConnPerIp > 0 ? std::to_string(perf_.maxConnPerIp) : std::string("不限")));
        if (!allowedIps_.empty()) {
            std::string ips;
            for (size_t i = 0; i < allowedIps_.size(); ++i) {
                if (i) ips += ", ";
                ips += allowedIps_[i];
            }
            if (security_.ipBlacklist) {
                logger(0, "IP 黑名单: " + ips + "（拒绝列表内 IP）");
            } else {
                logger(0, "IP 白名单: " + ips + "（仅允许列表内 IP）");
            }
        } else {
            logger(0, "IP 名单: 未启用（允许所有 IP）");
        }
    }
    return true;
}

void FtpServer::Stop() {
    if (!running_.exchange(false)) return;
    // 先置停止标志：让卡在 OpenData select 等待数据连接的会话线程
    // 在 1 秒内退出（shutdown 唤醒不了监听套接字上的 select）
    stopFlag_.store(true);

    // 1) 关闭监听套接字并等待 AcceptLoop 退出
    if (listenSock_ != ~uintptr_t(0)) {
        closesocket((SOCKET)listenSock_);
        listenSock_ = ~uintptr_t(0);
    }
    if (acceptThread_.joinable()) acceptThread_.join();

    // 2) shutdown 所有在册会话套接字（解除 recv/accept 阻塞），
    //    并等待所有客户端会话线程退出，之后才能安全 WSACleanup。
    //    原实现仅 sleep 100ms，客户端线程仍在使用套接字/WSA 时即清理，
    //    会造成 use-after-free 式崩溃与状态污染。
    SessShutdownAll();
    {
        std::unique_lock<std::mutex> lk(sessMutex_);
        sessCv_.wait(lk, [this] { return activeSessions_ == 0; });
    }

    WSACleanup();
    if (logger_) logger_(0, "FTP 服务已停止");
}

// ---------------------------------------------------------------------------
// 会话跟踪：注册/注销套接字，供 Stop() 解除阻塞并等待会话退出
// ---------------------------------------------------------------------------

void FtpServer::SessRegister(uintptr_t s) {
    std::lock_guard<std::mutex> lk(sessMutex_);
    sessSocks_.push_back(s);
}

void FtpServer::SessUnregister(uintptr_t s) {
    std::lock_guard<std::mutex> lk(sessMutex_);
    for (size_t i = 0; i < sessSocks_.size(); ++i) {
        if (sessSocks_[i] == s) {
            sessSocks_.erase(sessSocks_.begin() + i);
            break;
        }
    }
}

void FtpServer::SessShutdownAll() {
    std::lock_guard<std::mutex> lk(sessMutex_);
    for (uintptr_t s : sessSocks_) {
        // 先 shutdown（优雅关闭），再 closesocket（强制唤醒阻塞中的 recv/select）。
        // Windows 上 shutdown(SD_BOTH) 不会唤醒已经阻塞的 recv，必须 closesocket
        // 才能让 recv 立即返回 SOCKET_ERROR，否则会等满 SO_RCVTIMEO（5 分钟）。
        // 会话线程退出时会再次 closesocket，重复调用无害（返回 WSAENOTSOCK）。
        shutdown((SOCKET)s, SD_BOTH);
        closesocket((SOCKET)s);
    }
    sessSocks_.clear();   // 清空，防止会话线程退出时 SessUnregister 访问已失效条目
}

void FtpServer::SessLeave() {
    {
        std::lock_guard<std::mutex> lk(sessMutex_);
        --activeSessions_;
    }
    sessCv_.notify_all();
}

void FtpServer::AcceptLoop() {
    while (running_.load()) {
        // 用 select 限时等待新连接：Windows 上 closesocket() 无法可靠
        // 唤醒阻塞中的 accept()，Stop() 会因此卡在 acceptThread_.join()。
        // select 每 1 秒醒来检查一次 running_，保证 Stop 能快速 join。
        fd_set r;
        FD_ZERO(&r);
        FD_SET((SOCKET)listenSock_, &r);
        timeval tv{1, 0};
        int sr = select(0, &r, nullptr, nullptr, &tv);
        if (sr <= 0) {
            if (!running_.load()) break;   // 服务停止
            continue;                       // 超时或暂时性错误：继续等待
        }
        sockaddr_in caddr{};
        int clen = sizeof(caddr);
        SOCKET cs = accept((SOCKET)listenSock_, (sockaddr*)&caddr, &clen);
        if (cs == INVALID_SOCKET) {
            if (!running_.load()) break;
            continue;
        }
        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &caddr.sin_addr, ip, sizeof(ip));
        std::string clientIp(ip);

        // IP 名单检查：白名单模式=仅允许列表内 IP；黑名单模式=拒绝列表内 IP
        if (!allowedIps_.empty()) {
            bool inList = false;
            for (const auto& a : allowedIps_) {
                if (a == clientIp) { inList = true; break; }
            }
            if (security_.ipBlacklist) {
                if (inList) {
                    if (logger_) logger_(1, "拒绝连接（在黑名单）: " + clientIp);
                    closesocket(cs);
                    continue;
                }
            } else {
                if (!inList) {
                    if (logger_) logger_(1, "拒绝连接（不在白名单）: " + clientIp);
                    closesocket(cs);
                    continue;
                }
            }
        }

        // 单 IP 并发连接数限制
        if (perf_.maxConnPerIp > 0) {
            std::lock_guard<std::mutex> lk(ipCountMutex_);
            int& cnt = ipConnCount_[clientIp];
            if (cnt >= perf_.maxConnPerIp) {
                if (logger_) logger_(1, "拒绝连接（单 IP 并发超限 " +
                                          std::to_string(perf_.maxConnPerIp) +
                                          "）: " + clientIp);
                closesocket(cs);
                continue;
            }
            cnt++;
        }

        // 为每个客户端启动独立线程处理；线程结束后自行分离，避免
        // 在 Stop() 中持锁 join 造成死锁，也避免线程对象无限累积。
        // 会话计数在创建线程前递增，线程内部由 RAII 守卫递减，
        // 保证 Stop() 能通过条件变量准确等待所有会话退出。
        {
            std::lock_guard<std::mutex> lk(sessMutex_);
            ++activeSessions_;
        }
        bool threadOk = true;
        try {
            std::thread t(&FtpServer::HandleClient, this, (uintptr_t)cs, clientIp);
            t.detach();
        } catch (...) {
            threadOk = false;
            // 线程创建失败：直接关闭该连接，保证服务不崩溃
            if (logger_) logger_(2, "创建客户端线程失败，已关闭连接: " + clientIp);
            closesocket(cs);
            if (perf_.maxConnPerIp > 0) {
                std::lock_guard<std::mutex> lk(ipCountMutex_);
                auto it = ipConnCount_.find(clientIp);
                if (it != ipConnCount_.end()) {
                    if (--it->second <= 0) ipConnCount_.erase(it);
                }
            }
        }
        if (!threadOk) {
            std::lock_guard<std::mutex> lk(sessMutex_);
            --activeSessions_;
        }
    }
}

// ---------------------------------------------------------------------------
// 单个客户端会话处理
// ---------------------------------------------------------------------------

namespace {

struct Session {
    SOCKET ctrl = INVALID_SOCKET;
    SOCKET data = INVALID_SOCKET;
    SOCKET pasvListen = INVALID_SOCKET;
    FtpServer* srv = nullptr;   // 回指服务器：注册/注销套接字供 Stop() 解锁
    std::string clientIp;
    std::string cwd = "/";
    std::string renameFrom;
    bool loggedIn = false;
    bool binary = true;
    std::string root;
    uint16_t pasvStart, pasvEnd;
    LogCallback logger;
    std::string user = "anonymous";
    PerfOptions perf;
    SecurityOptions security;
    ConfirmCallback confirm;
    uint64_t restOffset = 0;   // REST 命令设置的续传偏移量，RETR/STOR 后清零

    // 文件名编码模式：
    //   false = 本地编码（GBK/CP936），兼容 Windows 资源管理器等默认客户端
    //   true  = UTF-8，客户端通过 OPTS UTF8 ON 显式启用
    // 默认使用本地编码，避免资源管理器（不发送 OPTS UTF8 ON）出现乱码。
    bool utf8Mode = false;

    // 当前生效的文件名编码：0=自动，1=UTF-8，2=GBK/本地编码。
    // “其他设置”里的强制编码优先于会话的 OPTS UTF8 协商。
    int NameEncodingMode() const {
        int mode = srv ? srv->FileNameEncoding() : 0;
        if (mode == 1 || mode == 2) return mode;
        return utf8Mode ? 1 : 2;
    }

    // 文件名兼容模式（“其他设置 → 默认编码”里显式开启才生效）：
    //   0 = 不处理，非法字符直接失败（标准 FTP 行为，也是默认值）
    //   3 = 兼容模式(尝试修复)：非法字符 -> 同形全角字符
    //   4 = 兼容模式(跳过解码)：非法字符 -> 空格
    int NameFixMode() const {
        int mode = srv ? srv->FileNameEncoding() : 0;
        return (mode == 3 || mode == 4) ? mode : 0;
    }

    // 按兼容模式修复“即将新建”的本地路径（上传/建目录/改名共用）。
    // 返回 true 表示路径被改写；改写结果会记一条警告日志。
    // what：日志前缀，如 "文件名" / "目录名" / "目标名"。
    bool FixCreateTarget(std::string& full, const char* what) {
        const int fix = NameFixMode();
        if (!fix) return false;
        std::string fixedFull;
        if (!FixCreatePath(full, fix, fixedFull)) return false;
        Log(1, std::string(what) + "含 Windows 非法字符，已替换" +
                   (fix == 3 ? "为全角字符: " : "为空格: ") +
                   PathToUtf8(fs::path(Utf8ToWide(full)).filename()) + " -> " +
                   PathToUtf8(fs::path(Utf8ToWide(fixedFull)).filename()));
        full = fixedFull;
        return true;
    }

    UINT NameCodePage() const {
        return NameEncodingMode() == 1 ? CP_UTF8 : CP_ACP;
    }

    // 把客户端发来的参数（按当前编码模式）转换为宽字符
    std::wstring ArgToWide(const std::string& a) const {
        if (a.empty()) return L"";
        UINT cp = NameCodePage();
        int len = MultiByteToWideChar(cp, 0, a.c_str(), (int)a.size(), nullptr, 0);
        if (len <= 0) return L"";
        std::wstring w(len, L'\0');
        MultiByteToWideChar(cp, 0, a.c_str(), (int)a.size(), &w[0], len);
        return w;
    }

    // 把宽字符按当前编码模式转换为客户端可识别的字节串（用于响应/列表）
    std::string WideToClient(const std::wstring& w) const {
        if (w.empty()) return "";
        UINT cp = NameCodePage();
        int len = WideCharToMultiByte(cp, 0, w.c_str(), (int)w.size(),
                                      nullptr, 0, nullptr, nullptr);
        if (len <= 0) return "";
        std::string s(len, '\0');
        WideCharToMultiByte(cp, 0, w.c_str(), (int)w.size(), &s[0], len,
                            nullptr, nullptr);
        return s;
    }

    // 把客户端参数统一转换为 UTF-8 字节串（供内部 ResolvePath/权限匹配使用）。
    // 内部路径处理统一以 UTF-8 为中间表示，避免本地编码与 UTF-8 混用。
    std::string ArgToUtf8(const std::string& a) const {
        if (a.empty()) return "";

        const int mode = srv ? srv->FileNameEncoding() : 0;
        // 兼容模式：资源管理器会把全角括号“（）”的第三字节换成 '?'，先按
        // 括号配对把 EF BC 3F 还原成“（/）”，否则它会被解码成“锛?”。
        const std::string src =
            (mode == 3 || mode == 4) ? RepairTruncatedParens(a, mode) : a;
        if (mode == 1) {
            // 强制 UTF-8：不再猜测 GBK。非法 UTF-8 保持原字节，由后续
            // 路径解析/文件打开报错，而不是悄悄生成另一个文件名。
            return src;
        }
        if (mode == 2) {
            // 强制 GBK/本地编码：不再优先识别 UTF-8。
            return WideToUtf8(DecodeBytesToWide(src, CP_ACP));
        }

        const bool validUtf8 = IsValidUtf8Bytes(src);
        std::wstring w = DecodeBytesToWide(src, CP_ACP);
        const size_t qa = CountWChar(w, L'?');

        std::string result;
        size_t qm = qa;
        if (validUtf8) {
            // 合法 UTF-8 一律优先：包括资源管理器直接发 UTF-8、但会话
            // 尚未收到 OPTS UTF8 ON 的情况。全角“（）”按 GBK 解码会变成
            // '?'（Windows 文件名非法，errno=22），绝不能走 ACP。
            result = src;
        } else {
            // 两种混合情况都要兜底：
            //   1) UTF-8 前缀 + GBK 尾部（开头有 3/4 字节 UTF-8 序列）；
            //   2) GBK 前缀 + UTF-8 尾部（整串按 ACP 解码会出现 '?'，
            //      例如 Explorer 发送的“大数据…（样”GBK + “题2）”UTF-8）。
            // 干净的 GBK 文件名 qa==0 且没有 UTF-8 前缀，仍走下面的 ACP，
            // 避免把 GBK 中恰好合法的 UTF-8 双字节误判成 Unicode。
            std::wstring mixed = DecodeMixedToWide(src);
            qm = CountWChar(mixed, L'?');
            if (qm <= qa) result = WideToUtf8(mixed);
            else result = WideToUtf8(w);
        }

        DumpArgEncoding(src, mode, validUtf8, qa, qm, result);
        return result;
    }

    // 把内部 UTF-8 路径转换为客户端可识别的字节串（用于响应/列表）
    std::string Utf8ToClient(const std::string& u) const {
        if (u.empty()) return "";
        if (NameEncodingMode() == 1) return u;   // 客户端使用 UTF-8
        return WideToClient(Utf8ToWide(u));      // UTF-8 -> 宽字符 -> 本地编码
    }

    // 把客户端参数解析为“相对于 root 的绝对 FTP 路径”（UTF-8）。
    // FTP 规范：以 '/' 开头的参数是绝对路径；否则相对于当前工作目录 cwd。
    // 递归上传/下载时客户端常用相对路径（如 STOR f.txt），必须拼接 cwd，
    // 否则会被错误地解析到 root 下，导致嵌套目录操作报 550。
    std::string ResolveArg(const std::string& arg) const {
        std::string u = ArgToUtf8(arg);
        std::replace(u.begin(), u.end(), '\\', '/');
        // 一些第三方客户端会把含空格/特殊字符的路径用双引号包起来
        // （STOR "my file.txt"）。Windows 文件名本身不允许 '"'，所以去掉
        // 首尾成对的引号是安全的；否则 " 会被当成文件名的一部分，
        // 创建文件时直接失败（550，errno=22 EINVAL）。
        if (u.size() >= 2 && u.front() == '"' && u.back() == '"') {
            u = u.substr(1, u.size() - 2);
        }
        if (u.empty()) return cwd;           // 空参数表示当前目录
        if (u[0] == '/') return u;           // 绝对路径
        // 相对路径：拼接到当前工作目录
        std::string base = cwd;
        if (base.empty()) base = "/";
        if (base.back() != '/') base += "/";
        return base + u;
    }

    // 计算某个 FTP 路径的有效权限。
    // 权限由 FtpServer 统一持有并加锁查询：GUI 在服务运行中修改权限后，
    // 现有连接的下一条命令也能立即生效（不再需要重启服务）。
    FtpPermissions EffectivePerms(const std::string& ftpPath) const {
        if (!srv) return FtpPermissions();
        return srv->QueryEffectivePerms(ftpPath);
    }

    // 判断某个 FTP 路径是否被标记为隐藏（高级模式，同样实时查询）
    bool IsHidden(const std::string& ftpPath) const {
        return srv && srv->QueryHidden(ftpPath);
    }

    void Send(const std::string& line) {
        std::string msg = line + "\r\n";
        send(ctrl, msg.c_str(), (int)msg.size(), 0);
    }

    void Log(int level, const std::string& m) {
        if (logger) logger(level, "[" + clientIp + "] " + m);
    }

    // 发送错误响应：同时写入服务端日志（便于排查客户端问题）
    void SendErr(const std::string& line) {
        Send(line);
        Log(1, "响应客户端: " + line);
    }

    // 敏感操作确认：若配置了确认回调则调用，返回是否允许执行
    bool Confirm(const std::string& action, const std::string& target) {
        if (!confirm) return true;  // 未配置回调则默认允许
        Log(1, "请求敏感操作: " + action + " -> " + target + "（等待确认）");
        bool ok = confirm(action, target, clientIp);
        Log(ok ? 0 : 1, std::string("敏感操作") + (ok ? "已允许: " : "被拒绝: ") +
                        action + " -> " + target);
        return ok;
    }

    // 创建被动模式监听套接字（PASV/EPSV 共用），成功时写入选中端口。
    // 用 int 循环并先检查范围：uint16_t 自增在 end=65535 时会回绕，
    // 或 start>end 时几乎扫描全部端口，把端口绑定到范围之外。
    bool OpenPassiveListener(uint16_t& chosen) {
        chosen = 0;
        if (pasvListen != INVALID_SOCKET) {
            if (srv) srv->SessUnregister((uintptr_t)pasvListen);
            closesocket(pasvListen);
            pasvListen = INVALID_SOCKET;
        }
        SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (ls == INVALID_SOCKET) return false;
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = INADDR_ANY;
        bool ok = false;
        if (pasvStart <= pasvEnd) {
            for (int p = pasvStart; p <= (int)pasvEnd; ++p) {
                a.sin_port = htons((uint16_t)p);
                if (bind(ls, (sockaddr*)&a, sizeof(a)) != SOCKET_ERROR) {
                    chosen = (uint16_t)p;
                    ok = true;
                    break;
                }
            }
        }
        if (!ok || listen(ls, 1) == SOCKET_ERROR) {
            closesocket(ls);
            return false;
        }
        pasvListen = ls;
        if (srv) srv->SessRegister((uintptr_t)ls);
        return true;
    }

    // 打开数据连接（主动/被动）
    SOCKET OpenData() {
        if (pasvListen != INVALID_SOCKET) {
            sockaddr_in a{};
            int al = sizeof(a);
            SOCKET d = INVALID_SOCKET;
            // 限时等待连接：客户端拿到 PASV 端口后不连数据端口会让 accept
            // 永久阻塞（远程可借此挂死会话线程、耗尽 PASV 端口）。
            // 1 秒步进的 select 也便于服务停止时经 shutdown 解除阻塞。
            int toSec = perf.dataTimeoutSec > 0 ? perf.dataTimeoutSec : 60;
            DWORD deadline = GetTickCount() + (DWORD)toSec * 1000;
            for (;;) {
                fd_set r;
                FD_ZERO(&r);
                FD_SET(pasvListen, &r);
                timeval tv{1, 0};
                int sr = select(0, &r, nullptr, nullptr, &tv);
                if (sr > 0) {
                    d = accept(pasvListen, (sockaddr*)&a, &al);
                    break;
                }
                if (sr == SOCKET_ERROR) break;  // 服务停止（shutdown）
                // Windows 上 shutdown() 唤醒不了 select 中的监听套接字，
                // 必须靠停止标志退出，否则 Stop() 会卡满整个超时死线
                // （实测：传输中途停止服务，UI 线程因此未响应）
                if (srv && srv->IsStopRequested()) break;
                if (GetTickCount() >= deadline) break;  // 等待超时
            }
            if (srv) srv->SessUnregister((uintptr_t)pasvListen);
            closesocket(pasvListen);
            pasvListen = INVALID_SOCKET;
            return d;
        }
        return data; // 主动模式已连接
    }

    void CloseData(SOCKET d) {
        if (d != INVALID_SOCKET && d != data) {
            if (srv) srv->SessUnregister((uintptr_t)d);
            closesocket(d);
        }
        if (data != INVALID_SOCKET) {
            if (srv) srv->SessUnregister((uintptr_t)data);
            closesocket(data);
            data = INVALID_SOCKET;
        }
    }
};

// 对数据/控制套接字应用性能参数（TCP_NODELAY、内核缓冲、超时）
void ApplySockOptions(SOCKET s, const PerfOptions& perf) {
    if (s == INVALID_SOCKET) return;
    if (perf.tcpNoDelay) {
        BOOL nd = TRUE;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof(nd));
    }
    if (perf.sockBufKB > 0) {
        int sz = perf.sockBufKB * 1024;
        setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&sz, sizeof(sz));
        setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&sz, sizeof(sz));
    }
    if (perf.dataTimeoutSec > 0) {
        DWORD to = (DWORD)perf.dataTimeoutSec * 1000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof(to));
    }
}

// 循环发送直到全部发出；超时/出错返回 false（修复：原先 RETR/LIST 忽略
// send 返回值，部分发送时客户端得到损坏数据却收到 226 成功响应）
bool SendAll(SOCKET s, const char* buf, int len) {
    int off = 0;
    while (off < len) {
        int n = send(s, buf + off, len - off, 0);
        if (n == SOCKET_ERROR || n <= 0) return false;
        off += n;
    }
    return true;
}

// 解析命令与参数
void SplitCmd(const std::string& line, std::string& cmd, std::string& arg) {
    size_t sp = line.find(' ');
    if (sp == std::string::npos) {
        cmd = ToUpper(line);
        arg = "";
    } else {
        cmd = ToUpper(line.substr(0, sp));
        arg = Trim(line.substr(sp + 1));
    }
}

} // namespace

void FtpServer::HandleClient(uintptr_t clientSock, std::string clientIp) {
    // RAII 守卫：无论正常结束还是异常退出，都递减会话计数并唤醒 Stop()
    struct SessGuard {
        FtpServer* srv;
        ~SessGuard() { srv->SessLeave(); }
    } guard{this};
    // 整个会话处理包裹异常保护：任何未捕获异常都会导致线程 std::terminate，
    // 进而使整个服务崩溃。这里统一兜底，保证单个客户端出错不影响服务。
    try {
    Session s;
    s.srv = this;
    s.ctrl = (SOCKET)clientSock;
    s.clientIp = clientIp;
    s.root = rootPath_;
    s.pasvStart = pasvStart_;
    s.pasvEnd = pasvEnd_;
    s.logger = logger_;
    s.perf = perf_;
    s.security = security_;
    s.confirm = confirm_;

    // 控制连接应用性能参数
    ApplySockOptions(s.ctrl, perf_);
    // 控制连接的命令间隔（空闲）允许更久：5 分钟。
    // dataTimeoutSec 只应作用于数据通道，不应把合法的空闲会话踢下线。
    {
        DWORD idleTo = 300000;
        setsockopt(s.ctrl, SOL_SOCKET, SO_RCVTIMEO,
                   (const char*)&idleTo, sizeof(idleTo));
    }
    // 注册控制套接字：服务停止时 shutdown 解除阻塞，保证会话线程能退出
    SessRegister(s.ctrl);

    s.Log(0, "客户端已连接");
    s.Send("220 FtpTool C++ Server Ready");

    char buf[4096];
    std::string pending;
    while (running_.load()) {
        int n = recv(s.ctrl, buf, sizeof(buf), 0);
        if (n <= 0) break;
        pending.append(buf, n);

        size_t pos;
        while ((pos = pending.find("\r\n")) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 2);
            if (line.empty()) continue;

            std::string cmd, arg;
            SplitCmd(line, cmd, arg);

            // ---- 无需登录的命令 ----
            if (cmd == "USER") {
                s.user = arg.empty() ? "anonymous" : arg;
                s.Send("331 User name okay, need password");
                continue;
            }
            if (cmd == "PASS") {
                s.loggedIn = true;
                s.Send("230 User logged in, proceed");
                continue;
            }
            if (cmd == "QUIT") {
                s.Send("221 Goodbye");
                goto done;
            }
            if (cmd == "FEAT") {
                s.Send("211-Features:");
                // 强制 GBK 时不再宣传 UTF8，避免客户端误以为可以协商 UTF-8。
                if (!s.srv || s.srv->FileNameEncoding() != 2)
                    s.Send(" UTF8");
                s.Send(" PASV");
                s.Send(" EPSV");
                s.Send(" SIZE");
                s.Send(" MDTM");
                s.Send(" REST STREAM");
                s.Send(" XMD5");
                s.Send("211 End");
                continue;
            }
            if (cmd == "SYST") { s.Send("215 Windows_NT"); continue; }
            if (cmd == "OPTS") {
                // 处理 OPTS UTF8 ON/OFF：切换会话的文件名编码模式。
                // 默认使用本地编码（GBK），兼容 Windows 资源管理器；
                // 客户端显式发送 OPTS UTF8 ON 时切换为 UTF-8。
                // 同时接受带连字符的 "UTF-8" 写法；之前只认 "UTF8"，
                // 客户端发 "OPTS UTF-8 ON" 时服务端回 200 却没切换编码，
                // 后续含中文/特殊字符的上传就会变成乱码或失败。
                std::string a = ToUpper(arg);
                if (a.rfind("UTF8", 0) == 0 || a.rfind("UTF-8", 0) == 0) {
                    // 强制 GBK 时忽略 OPTS UTF8，避免“回了 200 但编码没变”的误导。
                    // 必须回 502 而不是 200：客户端（含本工具自己的客户端）是
                    // 按响应码决定要不要用 UTF-8 发名字的，回 200 会让它误以为
                    // 已切换，随后发来的 UTF-8 名字被按 GBK 解码成乱码（还可能
                    // 含 '?'，直接 550 Cannot create file / errno=22）。
                    if (s.srv && s.srv->FileNameEncoding() == 2) {
                        s.Send("502 UTF8 mode not supported (server uses local encoding)");
                    } else if (a.find("OFF") != std::string::npos) {
                        s.utf8Mode = false;
                        s.Send("200 UTF8 mode disabled");
                    } else {
                        s.utf8Mode = true;
                        s.Send("200 UTF8 mode enabled");
                    }
                } else {
                    s.Send("200 OK");
                }
                continue;
            }
            if (cmd == "NOOP") { s.Send("200 OK"); continue; }
            if (cmd == "TYPE") { s.binary = (ToUpper(arg) != "A"); s.Send(std::string("200 Type set to ") + (s.binary ? "I" : "A")); continue; }

            // 这些标准命令对服务端行为没有影响，直接回 200/2xx。
            // 第三方客户端可能发送它们；回 502 容易让客户端把连接判为异常。
            if (cmd == "MODE") { s.Send("200 Mode set to S"); continue; }
            if (cmd == "STRU") { s.Send("200 Structure set to F"); continue; }
            if (cmd == "CLNT") { s.Send("200 Noted"); continue; }
            if (cmd == "ALLO") { s.Send("200 Allocation not needed"); continue; }
            if (cmd == "ACCT") { s.Send("202 ACCT not needed"); continue; }
            if (cmd == "ABOR") { s.Send("226 Abort successful"); continue; }

            if (!s.loggedIn) {
                s.SendErr("530 Please login with USER and PASS");
                continue;
            }

            // ---- 续传偏移（REST）----
            // REST 只设置偏移量，不主动传输；紧随其后的 RETR/STOR 才实际生效。
            // 偏移 >= 文件大小时，RETR 返回空（0 字节）；STOR 行为同上。
            if (cmd == "REST") {
                unsigned long long off = 0;
                // 解析十进制偏移量（必须为非负整数）
                for (char ch : arg) {
                    if (ch < '0' || ch > '9') { off = 0; break; }
                    off = off * 10 + (unsigned long long)(ch - '0');
                }
                s.restOffset = (uint64_t)off;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "350 Restarting at %llu. Send STORE or RETRIEVE to initiate transfer.",
                              (unsigned long long)s.restOffset);
                s.Send(buf);
                continue;
            }

            // ---- 文件 MD5（XMD5，自定义扩展）----
            if (cmd == "XMD5") {
                std::string full;
                std::string uarg = s.ResolveArg(arg);
                std::error_code ec;
                if (!ResolvePath(s.root, uarg, full) ||
                    !fs::is_regular_file(LongLocalPath(full), ec)) {
                    s.SendErr("550 File not found: " + arg);
                    continue;
                }
                std::string md5;
                if (ComputeFileMd5(LongLocalPath(full), md5)) {
                    s.Send("213 " + md5);
                } else {
                    s.SendErr("550 Cannot compute MD5: " + arg);
                }
                continue;
            }

            // ---- 路径相关 ----
            if (cmd == "PWD" || cmd == "XPWD") {
                // 257 响应中路径若包含 " 需双写转义（RFC 959）
                std::string esc;
                for (char ch : s.cwd) {
                    esc.push_back(ch);
                    if (ch == '"') esc.push_back('"');
                }
                s.Send("257 \"" + s.Utf8ToClient(esc) + "\" is current directory");
                continue;
            }
            if (cmd == "CWD" || cmd == "XCWD") {
                std::string full;
                // 相对路径需拼接当前工作目录（递归进入子目录时客户端常用相对路径）
                std::string target = s.ResolveArg(arg);
                std::error_code ec;
                if (!ResolvePath(s.root, target, full) ||
                    !fs::is_directory(LongLocalPath(full), ec)) {
                    s.SendErr("550 Directory not found or access denied: " + arg);
                } else {
                    // 更新 cwd（相对 root），跨盘符安全
                    s.cwd = RelativeFtpPath(s.root, full);
                    s.Send("250 Directory changed to " + s.Utf8ToClient(s.cwd));
                }
                continue;
            }
            if (cmd == "CDUP") {
                std::string full;
                if (ResolvePath(s.root, s.cwd + "/..", full)) {
                    s.cwd = RelativeFtpPath(s.root, full);
                }
                s.Send("250 Directory changed to " + s.Utf8ToClient(s.cwd));
                continue;
            }

            // ---- 被动/主动模式 ----
            if (cmd == "EPSV") {
                // RFC 2428：EPSV 的响应里只有端口，客户端会使用控制连接
                // 的对端地址来连数据端口。TUN/NAT 环境下不会像 PASV 那样
                // 收到一个错误的本机 IP，第三方客户端兼容性也更好。
                std::string a = ToUpper(arg);
                if (a == "ALL") {
                    s.Send("200 EPSV ALL ok");
                    continue;
                }
                if (!a.empty() && a != "1") {
                    // 2 = IPv6，当前服务只监听 IPv4
                    s.SendErr("522 Network protocol not supported, use (1)");
                    continue;
                }
                uint16_t chosen = 0;
                if (!s.OpenPassiveListener(chosen)) {
                    s.SendErr("425 Cannot open passive connection");
                    continue;
                }
                char resp[96];
                std::snprintf(resp, sizeof(resp),
                              "229 Entering Extended Passive Mode (|||%u|)",
                              (unsigned)chosen);
                s.Send(resp);
                continue;
            }
            if (cmd == "PASV") {
                uint16_t chosen = 0;
                if (!s.OpenPassiveListener(chosen)) {
                    s.SendErr("425 Cannot open passive connection");
                    continue;
                }
                // 返回本机 IP：优先用控制连接的本地地址，也就是客户端
                // 实际连到的那张网卡。使用 TUN 代理时 GetLocalIp() 可能
                // 返回 10.0.0.2 这类客户端不可达的虚拟网卡地址；
                // getsockname 能保证 PASV 通告的是本次连接可达的地址。
                std::string ip;
                sockaddr_in localAddr{};
                int localLen = sizeof(localAddr);
                if (getsockname(s.ctrl, (sockaddr*)&localAddr, &localLen) == 0 &&
                    localAddr.sin_family == AF_INET) {
                    char ipbuf[INET_ADDRSTRLEN] = {0};
                    if (inet_ntop(AF_INET, &localAddr.sin_addr, ipbuf,
                                  sizeof(ipbuf))) {
                        ip = ipbuf;
                    }
                }
                if (ip.empty() || ip == "0.0.0.0") {
                    ip = (bindIp_.empty() || bindIp_ == "0.0.0.0")
                             ? GetLocalIp() : bindIp_;
                }
                std::replace(ip.begin(), ip.end(), '.', ',');
                int p1 = chosen / 256, p2 = chosen % 256;
                char resp[128];
                std::snprintf(resp, sizeof(resp),
                              "227 Entering Passive Mode (%s,%d,%d)", ip.c_str(), p1, p2);
                s.Send(resp);
                continue;
            }
            if (cmd == "PORT") {
                // 解析 h1,h2,h3,h4,p1,p2
                int v[6];
                if (sscanf(arg.c_str(), "%d,%d,%d,%d,%d,%d",
                           &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
                    if (s.data != INVALID_SOCKET) {
                        if (s.srv) s.srv->SessUnregister((uintptr_t)s.data);
                        closesocket(s.data);
                        s.data = INVALID_SOCKET;
                    }
                    SOCKET d = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                    sockaddr_in a{};
                    a.sin_family = AF_INET;
                    char ip[32];
                    std::snprintf(ip, sizeof(ip), "%d.%d.%d.%d", v[0], v[1], v[2], v[3]);
                    inet_pton(AF_INET, ip, &a.sin_addr);
                    a.sin_port = htons((uint16_t)(v[4] * 256 + v[5]));
                    // 非阻塞 connect + select 限时：对不可达地址的阻塞 connect
                    // 会按系统 SYN 重试挂住约 20 秒，期间会话完全无响应
                    u_long nb = 1;
                    ioctlsocket(d, FIONBIO, &nb);
                    int cr = connect(d, (sockaddr*)&a, sizeof(a));
                    bool connOk = (cr == 0);
                    if (!connOk && WSAGetLastError() == WSAEWOULDBLOCK) {
                        int toSec = s.perf.dataTimeoutSec > 0 ? s.perf.dataTimeoutSec : 15;
                        fd_set w, e;
                        FD_ZERO(&w); FD_SET(d, &w);
                        FD_ZERO(&e); FD_SET(d, &e);
                        timeval tv{toSec, 0};
                        int sr = select(0, nullptr, &w, &e, &tv);
                        if (sr > 0 && FD_ISSET(d, &w)) connOk = true;
                    }
                    if (connOk) {
                        u_long blk = 0;
                        ioctlsocket(d, FIONBIO, &blk);
                        s.data = d;
                        if (s.srv) s.srv->SessRegister((uintptr_t)d);
                        s.Send("200 PORT command successful");
                    } else {
                        closesocket(d);
                        s.SendErr("425 Cannot connect to data port");
                    }
                } else {
                    s.SendErr("501 Syntax error in parameters");
                }
                continue;
            }

            // ---- 列目录 ----
            if (cmd == "LIST" || cmd == "NLST") {
                std::string full;
                // 第三方客户端常发 LIST -a / LIST -la 等选项；忽略以 '-'
                // 开头的选项，否则会被当成文件名，返回错误的列表内容。
                std::string listArg = arg;
                while (!listArg.empty() && listArg[0] == '-') {
                    size_t sp = listArg.find(' ');
                    if (sp == std::string::npos) { listArg.clear(); break; }
                    listArg = Trim(listArg.substr(sp + 1));
                }
                // 相对路径需拼接当前工作目录
                std::string target = s.ResolveArg(listArg);
                if (!ResolvePath(s.root, target, full)) {
                    s.SendErr("550 Invalid path: " + arg);
                    continue;
                }
                // 隐藏项强制剥夺 LIST/NLST 权限（不受单独权限控制）：
                // 被标记为隐藏的文件夹，即使有 list 权限也无法列出其内容。
                if (s.IsHidden(target)) {
                    s.SendErr("550 Permission denied (hidden): " + arg);
                    continue;
                }
                // 列目录权限仅对文件夹有意义：文件不需要 list 权限，
                // 只要父目录允许列出即可显示；文件夹无 list 权限则无法列出其内容。
                {
                    std::error_code ecDir;
                    if (fs::is_directory(LongLocalPath(full), ecDir) &&
                        !s.EffectivePerms(target).list) {
                        s.SendErr("550 Permission denied (list): " + arg);
                        continue;
                    }
                }
                SOCKET d = s.OpenData();
                if (d == INVALID_SOCKET) { s.SendErr("425 Cannot open data connection"); continue; }
                ApplySockOptions(d, s.perf);
                s.Send("150 Opening data connection");
                std::string listing;
                std::error_code ec;
                fs::path fullPath = LongLocalPath(full);
                if (fs::is_directory(fullPath, ec)) {
                    for (auto& e : fs::directory_iterator(fullPath, ec)) {
                        // 内部统一用 UTF-8 表示文件名
                        std::string nameUtf8 = PathToUtf8(e.path().filename());
                        // 高级模式下，跳过被标记为隐藏的条目（客户端不可见）
                        {
                            std::string childFtp = s.cwd;
                            if (childFtp.empty() || childFtp.back() != '/') childFtp += "/";
                            childFtp += nameUtf8;
                            if (s.IsHidden(childFtp)) continue;
                        }
                        // 发送给客户端时按会话编码转换（GBK 或 UTF-8）
                        std::string name = s.Utf8ToClient(nameUtf8);
                        if (cmd == "NLST") {
                            listing += name + "\r\n";
                        } else {
                            std::error_code ec2;
                            bool isDir = e.is_directory(ec2);
                            uintmax_t sz = isDir ? 0 : e.file_size(ec2);
                            char line[512];
                            std::snprintf(line, sizeof(line),
                                          "%s 1 owner group %12llu %s ",
                                          isDir ? "drwxr-xr-x" : "-rw-r--r--",
                                          (unsigned long long)sz,
                                          FormatListTime(e.path()).c_str());
                            listing += line;
                            listing += name + "\r\n";
                        }
                    }
                } else {
                    listing = s.Utf8ToClient(PathToUtf8(fullPath.filename())) + "\r\n";
                }
                bool listOk = SendAll(d, listing.c_str(), (int)listing.size());
                s.CloseData(d);
                if (listOk) {
                    s.Send("226 Transfer complete");
                } else {
                    s.SendErr("426 Connection closed, listing aborted");
                }
                continue;
            }

            // ---- 下载 ----
            if (cmd == "RETR") {
                std::string full;
                std::string uarg = s.ResolveArg(arg);   // 相对路径拼接 cwd
                std::error_code ec;
                if (!ResolveExisting(s.root, uarg, s.NameFixMode(), full) ||
                    !fs::is_regular_file(LongLocalPath(full), ec)) {
                    s.SendErr("550 File not found: " + arg);
                    continue;
                }
                if (!s.EffectivePerms(uarg).read) { s.SendErr("550 Permission denied (read): " + arg); continue; }
                // 下载为敏感操作，按配置弹窗确认（默认关闭）
                if (s.security.confirmDownload && !s.Confirm("下载文件", arg)) {
                    s.SendErr("550 Operation cancelled by server operator");
                    continue;
                }
                SOCKET d = s.OpenData();
                if (d == INVALID_SOCKET) { s.SendErr("425 Cannot open data connection"); continue; }
                ApplySockOptions(d, s.perf);
                s.Send("150 Opening data connection");
                std::ifstream in(LongLocalPath(full), std::ios::binary);
                if (!in) { s.CloseData(d); s.SendErr("550 Cannot open file: " + arg); continue; }
                // REST 续传：从指定偏移量开始发送
                if (s.restOffset > 0) {
                    in.seekg((std::streamoff)s.restOffset, std::ios::beg);
                    if (!in) { in.clear(); in.seekg(0, std::ios::end); }
                }
                std::vector<char> b((size_t)s.perf.appBufKB * 1024);
                uint64_t total = 0;
                bool sendOk = true;
                while (in.read(b.data(), (std::streamsize)b.size()) || in.gcount() > 0) {
                    int got = (int)in.gcount();
                    if (!SendAll(d, b.data(), got)) { sendOk = false; break; }
                    total += got;
                }
                in.close();
                s.restOffset = 0;   // REST 偏移一次性有效
                s.CloseData(d);
                if (!sendOk) {
                    // 发送超时/中断：必须回 426，不能把损坏的传输当成功
                    s.SendErr("426 Connection closed, transfer aborted");
                    s.Log(1, "下载中断（发送失败）: " + full);
                } else {
                    s.Send("226 Transfer complete");
                    s.Log(0, "下载文件: " + PathToUtf8(LongLocalPath(full).filename()) +
                              " | 大小: " + std::to_string(total) + " 字节");
                }
                continue;
            }

            // ---- 上传 ----
            if (cmd == "STOR" || cmd == "APPE") {
                std::string full;
                std::string uarg = s.ResolveArg(arg);   // 相对路径拼接 cwd
                if (!ResolvePath(s.root, uarg, full)) { s.SendErr("550 Invalid path: " + arg); continue; }
                // 仅当“其他设置 → 默认编码”选了兼容模式时才改写名字：
                // 非法字符（'?' ':' '*' '"' 等）会让 CreateFileW 返回
                // ERROR_INVALID_NAME(123)，ofstream 又映射成 errno=22，
                // 客户端只看到 550 且整个文件传不上去。
                s.FixCreateTarget(full, "文件名");
                if (!s.EffectivePerms(uarg).write) { s.SendErr("550 Permission denied (write): " + arg); continue; }
                // 覆盖已存在文件时按配置弹窗确认
                if (cmd == "STOR" && s.security.confirmOverwrite) {
                    std::error_code ec0;
                    if (fs::exists(LongLocalPath(full), ec0)) {
                        if (!s.Confirm("覆盖文件", arg)) {
                            s.SendErr("550 Operation cancelled by server operator");
                            continue;
                        }
                    }
                }
                {
                    std::error_code ecDir;
                    if (fs::is_directory(LongLocalPath(full), ecDir)) {
                        s.SendErr("550 Target is a directory: " + arg);
                        continue;
                    }
                }
                // Windows 上目标文件带“只读”属性时无法覆盖（U 盘/光盘拷来的
                // 文件非常常见）。操作员已经授予写权限，这里先清掉只读位。
                {
                    const fs::path lp = LongLocalPath(full);
                    DWORD attr = GetFileAttributesW(lp.c_str());
                    if (attr != INVALID_FILE_ATTRIBUTES &&
                        !(attr & FILE_ATTRIBUTE_DIRECTORY) &&
                        (attr & FILE_ATTRIBUTE_READONLY)) {
                        SetFileAttributesW(lp.c_str(),
                                           attr & ~FILE_ATTRIBUTE_READONLY);
                    }
                }
                SOCKET d = s.OpenData();
                if (d == INVALID_SOCKET) { s.SendErr("425 Cannot open data connection"); continue; }
                ApplySockOptions(d, s.perf);
                s.Send("150 Opening data connection");
                std::ios::openmode mode;
                if (s.restOffset > 0) {
                    // REST 续传：保留已有内容，从偏移量处覆盖写入
                    // 用 in|out 二进制模式（不 trunc），打开后 seekp 到偏移
                    mode = std::ios::binary | std::ios::in | std::ios::out;
                } else {
                    mode = std::ios::binary |
                        (cmd == "APPE" ? std::ios::app : std::ios::trunc);
                }
                // 收到首个数据块才打开文件（trunc 模式）：客户端收到 150 后
                // 立即断开时，不再把已有文件清空成 0 字节
                std::ofstream out;
                bool opened = false, openFail = false, recvErr = false,
                     writeErr = false, tooBig = false;
                int openErr = 0;
                const bool resumed = (s.restOffset > 0);
                const bool appending = (cmd == "APPE");
                uint64_t baseSize = 0;
                if (resumed) {
                    baseSize = s.restOffset;
                } else if (appending) {
                    std::error_code ecBase;
                    baseSize = fs::file_size(LongLocalPath(full), ecBase);
                    if (ecBase) baseSize = 0;
                }
                std::vector<char> b((size_t)s.perf.appBufKB * 1024);
                int n;
                uint64_t total = 0;
                uint64_t maxBytes = (uint64_t)s.perf.maxFileSizeMB * 1024 * 1024;
                while (true) {
                    n = recv(d, b.data(), (int)b.size(), 0);
                    if (n == SOCKET_ERROR) { recvErr = true; break; }  // 超时/错误≠EOF
                    if (n == 0) break;                                 // 对端正常关闭
                    if (!opened) {
                        if (s.restOffset > 0) {
                            // 续传：用 in|out 打开（不截断）；若文件不存在则回退为 trunc
                            out.open(LongLocalPath(full), mode);
                            if (!out) {
                                out.open(LongLocalPath(full),
                                         std::ios::binary | std::ios::trunc);
                                // 文件已不存在时，按续传语义在偏移处开始写，
                                // 前面留空洞；否则数据会整体前移，文件损坏
                                if (out)
                                    out.seekp((std::streamoff)s.restOffset,
                                              std::ios::beg);
                            } else {
                                out.seekp((std::streamoff)s.restOffset, std::ios::beg);
                                if (!out) { out.clear(); out.seekp(0, std::ios::end); }
                            }
                        } else {
                            out.open(LongLocalPath(full), mode);
                        }
                        if (!out) { openFail = true; openErr = errno; break; }
                        opened = true;
                    }
                    out.write(b.data(), n);
                    if (!out) { writeErr = true; break; }
                    total += (uint64_t)n;
                    if (maxBytes > 0 && baseSize + total > maxBytes) {
                        tooBig = true;
                        break;
                    }
                }
                if (opened) out.close();
                if (!opened && !openFail && !recvErr) {
                    // 零字节上传：客户端未发数据即正常关闭，仍应创建空文件
                    if (s.restOffset > 0) {
                        // 续传场景零字节：不改动已有文件
                    } else {
                        std::ofstream empty(LongLocalPath(full), mode);
                        if (!empty) { openFail = true; openErr = errno; }
                    }
                }
                s.restOffset = 0;   // REST 偏移一次性有效
                s.CloseData(d);
                if (openFail) {
                    std::string why;
                    switch (openErr) {
                    case EACCES:
                        why = "（拒绝访问：文件只读、目录 ACL 受限或被安全软件拦截）";
                        break;
                    case ENOENT: why = "（路径不存在）"; break;
                    case ENOSPC: why = "（磁盘空间不足）"; break;
                    case EEXIST: why = "（目标已存在）"; break;
                    case EINVAL:
                        // ERROR_INVALID_NAME(123)：文件名里有 Windows 不接受的
                        // 字符（'?' '*' ':' '"' '<' '>' '|' 或控制字符）。
                        // 若是客户端编码转换把名字转坏了，可在“其他设置 →
                        // 默认编码”里选兼容模式尝试修复/跳过解码。
                        why = "（文件名含 Windows 非法字符，如 ? * : \" < > |；"
                              "可在 其他设置→默认编码 选择兼容模式）";
                        break;
                    default: break;
                    }
                    s.SendErr("550 Cannot create file: " + arg + why);
                    s.Log(1, "创建文件失败: " + full + " | errno=" +
                              std::to_string(openErr));
                    continue;
                }
                if (writeErr) {
                    s.SendErr("451 Local write error (disk full?): " + arg);
                    s.Log(1, "写入文件失败: " + full);
                    continue;
                }
                if (recvErr) {
                    // 接收超时/连接错误：必须回 426，不能把截断文件当成功
                    s.SendErr("426 Connection lost, upload aborted");
                    s.Log(1, "上传中断（接收失败）: " + full);
                    continue;
                }
                if (tooBig) {
                    // 只删除本次全量上传产生的文件；APPE/续传时不能删掉原有内容
                    if (cmd == "STOR" && !resumed) {
                        std::error_code ec;
                        fs::remove(LongLocalPath(full), ec);
                    }
                    s.SendErr("552 File exceeds size limit, aborted");
                    s.Log(1, "上传被拒绝（超过单文件上限）: " + full);
                    continue;
                }
                s.Send("226 Transfer complete");
                s.Log(0, "上传文件: " + PathToUtf8(LongLocalPath(full).filename()) +
                          " | 大小: " + std::to_string(total) + " 字节");
                continue;
            }

            // ---- 删除文件 ----
            if (cmd == "DELE") {
                std::string full;
                std::string uarg = s.ResolveArg(arg);   // 相对路径拼接 cwd
                std::error_code ec;
                if (!ResolveExisting(s.root, uarg, s.NameFixMode(), full) ||
                    !fs::is_regular_file(LongLocalPath(full), ec)) {
                    s.SendErr("550 File not found: " + arg);
                    continue;
                }
                if (!s.EffectivePerms(uarg).del) { s.SendErr("550 Permission denied (delete): " + arg); continue; }
                // 删除文件为敏感操作，按配置弹窗确认
                if (s.security.confirmDeleteFile && !s.Confirm("删除文件", arg)) {
                    s.SendErr("550 Operation cancelled by server operator");
                    continue;
                }
                bool delOk;
                // Windows 不允许直接删除只读文件（U 盘/光盘拷来的常见），
                // 已授予删除权限时先清掉只读位（回收站/彻底删除都要）。
                {
                    DWORD attr = GetFileAttributesW(LongLocalPath(full).c_str());
                    if (attr != INVALID_FILE_ATTRIBUTES &&
                        (attr & FILE_ATTRIBUTE_READONLY)) {
                        SetFileAttributesW(LongLocalPath(full).c_str(),
                                           attr & ~FILE_ATTRIBUTE_READONLY);
                    }
                }
                if (s.security.deleteToRecycleBin) {
                    delOk = MoveToRecycleBin(LongLocalPath(full).wstring());
                } else {
                    delOk = fs::remove(LongLocalPath(full), ec);
                }
                if (delOk) {
                    s.Send("250 File deleted");
                    s.Log(0, std::string("删除文件") +
                              (s.security.deleteToRecycleBin ? "（已移入回收站）: " : ": ") + full);
                } else {
                    s.SendErr("550 Delete failed: " + arg);
                }
                continue;
            }

            // ---- 创建目录 ----
            if (cmd == "MKD" || cmd == "XMKD") {
                std::string full;
                std::string uarg = s.ResolveArg(arg);   // 相对路径拼接 cwd
                if (!ResolvePath(s.root, uarg, full)) { s.SendErr("550 Invalid path: " + arg); continue; }
                if (!s.EffectivePerms(uarg).mkdir) { s.SendErr("550 Permission denied (mkdir): " + arg); continue; }
                // 与上传同样处理：目录名里的非法字符会直接导致建目录失败
                // （550），整目录上传会当场中断。仅兼容模式才改写名字。
                std::string createdName = arg;
                if (s.FixCreateTarget(full, "目录名")) {
                    createdName = s.Utf8ToClient(
                        PathToUtf8(fs::path(Utf8ToWide(full)).filename()));
                }
                std::error_code ec;
                if (fs::create_directory(LongLocalPath(full), ec)) {
                    s.Send("257 \"" + createdName + "\" directory created");
                    s.Log(0, "创建目录: " + full);
                } else if (fs::is_directory(LongLocalPath(full), ec)) {
                    // 目录已存在：按“确保存在”处理。资源管理器拷贝整个文件夹
                    // 时若目标目录已存在会先发 MKD，返回 550 会让部分客户端
                    // 直接中止整次上传（表现也像“权限不够”）。
                    s.Send("257 \"" + createdName + "\" directory already exists");
                    s.Log(0, "目录已存在: " + full);
                } else {
                    s.SendErr("550 Create directory failed: " + arg);
                    s.Log(1, "创建目录失败: " + full + " | " + ec.message());
                }
                continue;
            }

            // ---- 删除目录 ----
            if (cmd == "RMD" || "XRMD" == cmd) {
                std::string full;
                std::string uarg = s.ResolveArg(arg);   // 相对路径拼接 cwd
                std::error_code ec;
                // 必须是目录：防止用 rmdir 权限删文件（绕过 del 权限检查）
                if (!ResolveExisting(s.root, uarg, s.NameFixMode(), full) ||
                    !fs::is_directory(LongLocalPath(full), ec)) {
                    s.SendErr("550 Directory not found: " + arg);
                    continue;
                }
                if (!s.EffectivePerms(uarg).rmdir) { s.SendErr("550 Permission denied (rmdir): " + arg); continue; }
                // 删除目录为敏感操作，按配置弹窗确认
                if (s.security.confirmDeleteDir && !s.Confirm("删除目录", arg)) {
                    s.SendErr("550 Operation cancelled by server operator");
                    continue;
                }
                bool rmdOk;
                if (s.security.deleteToRecycleBin) {
                    rmdOk = MoveToRecycleBin(LongLocalPath(full).wstring());
                } else {
                    rmdOk = fs::remove(LongLocalPath(full), ec);
                }
                if (rmdOk) {
                    s.Send("250 Directory removed");
                    s.Log(0, std::string("删除目录") +
                              (s.security.deleteToRecycleBin ? "（已移入回收站）: " : ": ") + full);
                } else {
                    s.SendErr("550 Remove directory failed (not empty?): " + arg);
                }
                continue;
            }

            // ---- 重命名 ----
            if (cmd == "RNFR") {
                std::string full;
                std::string uarg = s.ResolveArg(arg);   // 相对路径拼接 cwd
                std::error_code ec;
                if (!ResolveExisting(s.root, uarg, s.NameFixMode(), full) ||
                    !fs::exists(LongLocalPath(full), ec)) {
                    s.SendErr("550 Source not found: " + arg);
                    continue;
                }
                if (!s.EffectivePerms(uarg).rename) { s.SendErr("550 Permission denied (rename): " + arg); continue; }
                s.renameFrom = full;
                s.Send("350 Ready for destination name");
                continue;
            }
            if (cmd == "RNTO") {
                if (s.renameFrom.empty()) { s.SendErr("503 Bad sequence of commands (use RNFR first)"); continue; }
                std::string full;
                std::string uarg = s.ResolveArg(arg);   // 相对路径拼接 cwd
                if (!ResolvePath(s.root, uarg, full)) { s.SendErr("550 Invalid destination path: " + arg); continue; }
                // 目标名同样按兼容模式处理，否则重命名必然失败。
                s.FixCreateTarget(full, "目标名");
                if (!s.EffectivePerms(uarg).rename) { s.SendErr("550 Permission denied (rename): " + arg); continue; }
                // 重命名/移动为敏感操作，按配置弹窗确认
                if (s.security.confirmRename &&
                    !s.Confirm("重命名/移动", PathToUtf8(LongLocalPath(s.renameFrom).filename()) + " -> " + arg)) {
                    s.SendErr("550 Operation cancelled by server operator");
                    s.renameFrom.clear();
                    continue;
                }
                std::error_code ec;
                fs::rename(LongLocalPath(s.renameFrom),
                           LongLocalPath(full), ec);
                if (!ec) {
                    s.Send("250 Rename successful");
                    s.Log(0, "重命名: " + s.renameFrom + " -> " + full);
                } else {
                    s.SendErr("550 Rename failed: " + arg);
                }
                s.renameFrom.clear();
                continue;
            }

            // ---- 文件大小 ----
            if (cmd == "SIZE") {
                std::string full;
                std::string uarg = s.ResolveArg(arg);   // 相对路径拼接 cwd
                std::error_code ec;
                if (ResolveExisting(s.root, uarg, s.NameFixMode(), full)) {
                    fs::path fp = LongLocalPath(full);
                    if (fs::is_regular_file(fp, ec)) {
                        s.Send("213 " + std::to_string(fs::file_size(fp, ec)));
                        continue;
                    }
                }
                s.Send("550 Could not get file size: " + arg);
                continue;
            }

            // ---- 修改时间 ----
            if (cmd == "MDTM") {
                std::string full;
                std::string uarg = s.ResolveArg(arg);   // 相对路径拼接 cwd
                std::error_code ec;
                // RFC 3659：返回 213 YYYYMMDDHHMMSS（UTC）
                WIN32_FILE_ATTRIBUTE_DATA fa{};
                if (ResolveExisting(s.root, uarg, s.NameFixMode(), full) &&
                    GetFileAttributesExW(LongLocalPath(full).c_str(),
                                         GetFileExInfoStandard, &fa)) {
                    SYSTEMTIME st{};
                    FileTimeToSystemTime(&fa.ftLastWriteTime, &st);
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "213 %04u%02u%02u%02u%02u%02u",
                                  st.wYear, st.wMonth, st.wDay,
                                  st.wHour, st.wMinute, st.wSecond);
                    s.Send(buf);
                } else {
                    // 文件不存在是客户端上传前探测的正常情况，不作为警告记录
                    s.Send("550 Could not get modification time: " + arg);
                }
                continue;
            }

            s.SendErr("502 Command not implemented: " + cmd);
        }
    }

done:
    // 注销并关闭本会话所有套接字（保证 Stop() 不再等待本会话）
    if (s.pasvListen != INVALID_SOCKET) {
        if (s.srv) s.srv->SessUnregister((uintptr_t)s.pasvListen);
        closesocket(s.pasvListen);
    }
    if (s.data != INVALID_SOCKET) {
        if (s.srv) s.srv->SessUnregister((uintptr_t)s.data);
        closesocket(s.data);
    }
    if (s.srv) s.srv->SessUnregister((uintptr_t)s.ctrl);
    closesocket(s.ctrl);
    s.Log(0, "客户端已断开");

    // 释放单 IP 连接计数
    if (perf_.maxConnPerIp > 0) {
        std::lock_guard<std::mutex> lk(ipCountMutex_);
        auto it = ipConnCount_.find(clientIp);
        if (it != ipConnCount_.end()) {
            if (--it->second <= 0) ipConnCount_.erase(it);
        }
    }
    // 会话计数由 HandleClient 的 SessGuard 守卫统一递减并通知 Stop()
    } catch (const std::exception& e) {
        if (logger_) logger_(2, "客户端会话异常: " + std::string(e.what()));
    } catch (...) {
        if (logger_) logger_(2, "客户端会话发生未知异常");
    }
}
