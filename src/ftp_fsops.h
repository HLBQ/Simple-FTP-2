#pragma once

#include "ftp_client.h"
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <functional>

// ============================================================================
//  FtpOps —— 递归文件操作（上传 / 下载 / 删除）
//
//  纯逻辑模块，不依赖任何 UI：支持文件与文件夹（可任意混合）的递归处理，
//  供内置 GUI 的右键菜单与 OLE 拖放共同复用，也可直接用于其他项目。
//
//  线程约定：
//    - 所有方法阻塞执行，调用方应在工作线程中调用；
//    - FTP 连接不可跨线程并发，调用方需自行保证独占（如外部加锁）。
// ============================================================================

class FtpOps {
public:
    // 远程条目（对应客户端列表中的选中项）
    struct RemoteItem {
        std::string name;    // 文件名（UTF-8，相对当前目录）
        bool isDir = false;
        uint64_t size = 0;   // 文件大小（目录可留 0）
    };

    // 进度快照
    struct Progress {
        std::wstring current;   // 当前正在处理的条目显示名
        uint64_t done = 0;      // 已完成量（上传/下载时为字节，删除时为条目数）
        uint64_t total = 0;     // 总量（单位与 done 相同）
        bool verifying = false; // 是否处于哈希校验阶段
        uint64_t verifyDone = 0;  // 校验阶段：已校验字节数
        uint64_t verifyTotal = 0; // 校验阶段：当前文件总字节（0=未知）
    };

    // 进度回调：当前条目变化或完成量变化时触发（在调用线程中同步调用）
    using ProgressCb = std::function<void(const Progress&)>;
    // 取消查询：返回 true 时尽快中止当前操作（可为 nullptr）
    using CancelFn = std::function<bool()>;

    // ---- 上传 ----------------------------------------------------------------
    // 统计本地路径列表（文件/文件夹混合、递归）的总字节数
    static uint64_t MeasureLocal(const std::vector<std::filesystem::path>& paths);

    // 上传本地路径列表到 FTP 当前目录。
    // 任一条目失败不影响其他条目，整体有失败时返回 false。
    static bool Upload(FtpClient& client,
                       const std::vector<std::filesystem::path>& paths,
                       uint64_t totalBytes,
                       const ProgressCb& onProgress = nullptr,
                       const CancelFn& isCancelled = nullptr);

    // ---- 下载 ----------------------------------------------------------------
    // 统计远程条目（文件/文件夹混合、递归）的总字节数。
    // 尽力统计：无法进入的目录跳过其内容（不中断整体）。
    static bool MeasureRemote(FtpClient& client,
                              const std::vector<RemoteItem>& items,
                              uint64_t& totalBytes);

    // 下载远程条目到本地目录（自动创建各级子目录）。
    // 任一条目失败不影响其他条目，整体有失败时返回 false。
    static bool Download(FtpClient& client,
                         const std::vector<RemoteItem>& items,
                         const std::filesystem::path& localDir,
                         uint64_t totalBytes,
                         const ProgressCb& onProgress = nullptr,
                         const CancelFn& isCancelled = nullptr);

    // ---- 删除 ----------------------------------------------------------------
    // 统计远程条目包含的总条目数（文件 + 目录），用于删除进度显示。
    // 尽力统计：无法进入的目录按已见内容计数。
    static bool CountRemote(FtpClient& client,
                            const std::vector<RemoteItem>& items,
                            uint64_t& totalEntries);

    // 递归删除远程条目：先清空目录内的全部内容，再删除目录本身。
    // 任一条目失败不影响其他条目，整体有失败时返回 false。
    static bool Remove(FtpClient& client,
                       const std::vector<RemoteItem>& items,
                       uint64_t totalEntries,
                       const ProgressCb& onProgress = nullptr,
                       const CancelFn& isCancelled = nullptr);
};
