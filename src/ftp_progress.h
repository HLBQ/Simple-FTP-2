#pragma once

#include <windows.h>
#include <string>
#include <cstdint>

// ============================================================================
//  传输进度弹窗
//
//  用于上传/下载时显示一个专门的进度窗口，包含：
//    - 操作类型（上传/下载）与文件名
//    - 进度条 + 百分比
//    - 已传输 / 总大小
//    - 实时速度（自动换算 B/s、KB/s、MB/s）
//    - 预计剩余时间
//
//  用法（在工作线程中调用）：
//    ProgressDialog::Begin(hOwner, L"下载", L"file.zip", totalSize);
//    ... 传输循环中调用 ProgressDialog::Update(done, total);
//    ProgressDialog::End();
//
//  说明：Begin/Update/End 均线程安全，内部通过 PostMessage 把刷新请求
//        投递到 UI 线程，实际绘制在 UI 线程完成。
// ============================================================================

class ProgressDialog {
public:
    // 开始一次传输：创建并显示进度窗口。
    //   hOwner   : 父窗口
    //   action   : 操作名称（如“上传”“下载”）
    //   fileName : 文件名（UTF-8）
    //   total    : 总字节数（0 表示未知）
    static void Begin(HWND hOwner, const std::wstring& action,
                      const std::wstring& fileName, uint64_t total);

    // 更新进度（在工作线程中调用，线程安全）。
    // verifying=true 表示哈希校验阶段：整体进度条保持不动，另外显示
    // “正在校验”与本文件校验进度；速度/剩余时间切换为校验提示。
    // verifyDone/verifyTotal: 当前文件已校验字节 / 总字节（0 表示未知）。
    static void Update(uint64_t done, uint64_t total, bool verifying = false,
                       uint64_t verifyDone = 0, uint64_t verifyTotal = 0);

    // 结束传输：关闭进度窗口。
    static void End();

    // 是否已取消（用户点击了“取消”按钮）。
    static bool IsCancelled();

    // 是否有传输正在进行（用于防止并发传输互相干扰）。
    static bool IsBusy();

    // 更新进度窗口中显示的文件名（多文件传输时每切换一个文件调用一次）。
    static void SetFileName(const std::wstring& name);

    // 更新操作名称（如"下载"->"校验"），同步修改窗口标题。
    static void SetAction(const std::wstring& action);

    // 处理 UI 线程消息（由主窗口 WndProc 转发）。
    // 返回 true 表示消息已处理。
    static bool HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
