#include "winlongpath.h"

#include <windows.h>

std::wstring ToLongPathW(const std::wstring& path) {
    static const wchar_t kPrefix[] = L"\\\\?\\";
    const size_t kPreLen = 4;

    if (path.empty()) return path;

    // 已带 \\?\ 前缀：原样返回
    if (path.size() >= kPreLen &&
        _wcsnicmp(path.c_str(), kPrefix, kPreLen) == 0) {
        return path;
    }

    // UNC：\\server\share -> \\?\UNC\server\share
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        return std::wstring(kPrefix) + L"UNC\\" + path.substr(2);
    }

    // 其他路径：先取绝对完整路径（折叠 . 与 ..、解析相对路径、统一反斜杠）。
    // 注意 \\?\ 路径系统不再做这些规范化，所以必须在加前缀之前完成。
    DWORD need = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (need > 0) {
        std::wstring full(need, L'\0');
        DWORD got = GetFullPathNameW(path.c_str(), need, &full[0], nullptr);
        if (got > 0) {
            full.resize(got);
            return std::wstring(kPrefix) + full;
        }
    }

    // 兜底：直接加前缀（调用方需保证此时已是绝对路径）
    return std::wstring(kPrefix) + path;
}
