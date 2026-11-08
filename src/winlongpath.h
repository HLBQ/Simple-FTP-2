#pragma once

#include <string>

// ---------------------------------------------------------------------------
// Windows 长路径支持（手动实现，不依赖系统 LongPathsEnabled 开关）
//
// 把任意路径转换为 \\?\ 前缀的绝对长路径形式：
//   - 相对路径 / 含 . 与 .. 的路径：先经 GetFullPathNameW 规范化
//   - 盘符绝对路径：D:\dir\file -> \\?\D:\dir\file
//   - UNC 路径：\\server\share  -> \\?\UNC\server\share
//   - 已带 \\?\ 前缀：原样返回
//
// 该机制自 Windows Vista 起原生可用，路径上限约 32767 字符，
// 且行为完全由本程序控制，在任何用户设备上效果一致。
// ---------------------------------------------------------------------------

std::wstring ToLongPathW(const std::wstring& path);
