#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <cstdint>
#include <map>

// 权限位定义（修复原 Python 版本权限组合不完整的问题）
struct FtpPermissions {
    bool read = true;      // 下载文件 (RETR)
    bool write = false;    // 上传文件 (STOR/APPE)
    bool del = false;      // 删除文件 (DELE)
    bool mkdir = false;    // 创建目录 (MKD)
    bool rmdir = false;    // 删除目录 (RMD)
    bool rename = false;   // 重命名 (RNFR/RNTO)
    bool list = true;      // 列目录 (LIST/NLST)

    // 生成权限描述字符串，用于日志显示
    std::string ToString() const;

    // 生成权限单字母简写，用于列表显示
    // R=读 W=写 D=删文件 M=建目录 X=删目录 N=重命名 L=列目录
    std::string ToShortString() const;
};

// 单个文件/文件夹的权限覆盖（高级模式）
struct FilePermEntry {
    std::string path;      // 相对共享根目录的路径（以 / 开头）
    FtpPermissions perms;
    bool hidden = false;   // 是否对客户端隐藏（不出现在目录列表中）
};

// 性能/传输参数
struct PerfOptions {
    int  dataTimeoutSec = 60;   // 数据通道超时（秒），0=不超时
    bool tcpNoDelay = true;     // TCP_NODELAY
    int  sockBufKB = 256;       // SO_SNDBUF/SO_RCVBUF（KB）
    int  appBufKB = 64;         // 应用层传输缓冲区（KB）
    int  maxFileSizeMB = 0;     // 单文件最大大小（MB），0=不限
    int  maxConnPerIp = 20;     // 单 IP 最大并发连接数，0=不限
};

// 日志回调：level 0=info 1=warn 2=error
using LogCallback = std::function<void(int level, const std::string& msg)>;

// 敏感操作确认回调：返回 true=允许，false=拒绝
// action: 操作类型（如 "删除文件"）；target: 目标路径；clientIp: 客户端 IP
// 该回调会在客户端线程中同步调用，实现方需自行处理超时（超时应返回 false）
using ConfirmCallback = std::function<bool(const std::string& action,
                                           const std::string& target,
                                           const std::string& clientIp)>;

// 安全提醒配置：哪些敏感操作需要弹窗确认
struct SecurityOptions {
    bool confirmDeleteFile = true;   // 删除文件 (DELE)
    bool confirmDeleteDir  = true;   // 删除目录 (RMD)
    bool confirmRename     = false;  // 重命名/移动 (RNFR/RNTO)
    bool confirmOverwrite  = false;  // 上传覆盖已存在文件 (STOR)
    bool confirmDownload   = false;  // 下载文件 (RETR)，默认关闭
    bool deleteToRecycleBin = false; // 删除时移动到系统回收站（否则彻底删除）
    int  confirmTimeoutSec = 30;     // 弹窗等待超时（秒），超时视为拒绝
    bool ipBlacklist = false;        // IP 列表模式：false=白名单，true=黑名单
};

class FtpServer {
public:
    FtpServer();
    ~FtpServer();

    // 启动服务，返回是否成功
    // allowedIps: IP 白名单，为空表示允许所有 IP
    bool Start(const std::string& bindIp, uint16_t port,
               const std::string& rootPath, const FtpPermissions& perms,
               uint16_t pasvPortStart, uint16_t pasvPortEnd,
               const std::vector<std::string>& allowedIps,
               LogCallback logger,
               bool advancedMode = false,
               const std::vector<FilePermEntry>& filePerms = {},
               const PerfOptions& perf = PerfOptions(),
               const SecurityOptions& security = SecurityOptions(),
               ConfirmCallback confirm = nullptr,
               int fileNameEncoding = 0);

    // 停止服务
    void Stop();

    bool IsRunning() const { return running_.load(); }

    // 文件名编码：
    //   0=自动识别  1=强制 UTF-8  2=强制 GBK/本地编码
    //   3=兼容模式(尝试修复)：Windows 非法字符 -> 同形全角字符（'?' -> '？'）
    //   4=兼容模式(跳过解码)：Windows 非法字符 -> 空格
    // 3/4 仅在客户端把名字转坏（如资源管理器上传中文名留下 '?'）时使用；
    // 默认 0 不做任何改写，非法字符仍然直接报错。
    // 运行中修改立即对现有会话的下一条命令生效。
    void SetFileNameEncoding(int mode) { fileNameEncoding_.store(mode); }
    int FileNameEncoding() const { return fileNameEncoding_.load(); }

    // 运行时更新权限（无需重启服务；对现有会话的下一条命令立即生效）。
    // 高级模式开关、7 项全局权限、单文件/文件夹规则都通过这里同步。
    void UpdatePermissions(bool advancedMode, const FtpPermissions& perms,
                           const std::vector<FilePermEntry>& filePerms);

    // 查询某 FTP 路径的有效权限 / 是否隐藏（线程安全，供会话线程调用）
    FtpPermissions QueryEffectivePerms(const std::string& ftpPath) const;
    bool QueryHidden(const std::string& ftpPath) const;

    // 会话内部查询停止请求（OpenData 的 select 循环使用：
    // Windows 上 shutdown 唤醒不了 select 中的监听套接字）
    bool IsStopRequested() const { return stopFlag_.load(); }

    // 会话套接字注册（会话线程内部调用）：
    // Stop() 会对所有注册中的套接字执行 shutdown，强制解除 recv/send/accept
    // 阻塞，使会话线程能够快速退出，从而保证 Stop 返回后没有线程再访问本对象。
    void SessRegister(uintptr_t s);
    void SessUnregister(uintptr_t s);

    // 获取本机局域网 IP
    static std::string GetLocalIp();

private:
    void AcceptLoop();
    void HandleClient(uintptr_t clientSock, std::string clientIp);
    void SessShutdownAll();
    // 会话退出时递减 activeSessions_ 并唤醒 Stop()（HandleClient 内 RAII 调用）
    void SessLeave();

    std::atomic<bool> running_{false};
    // 停止标志：OpenData 的 select 循环每秒检查它——Windows 上 shutdown()
    // 无法唤醒 select 中的监听套接字，置位标志是让数据连接 accept 快速
    // 退出的唯一可靠手段（否则 Stop() 要等满 dataTimeoutSec 死线）
    std::atomic<bool> stopFlag_{false};
    uintptr_t listenSock_ = ~uintptr_t(0); // INVALID_SOCKET
    std::thread acceptThread_;

    // 活动会话跟踪（修复：原先客户端线程全部 detach，Stop() 仅 sleep 100ms
    // 就 WSACleanup，会话线程随后访问已释放的对象导致崩溃/UAF）。
    // 会话线程启动前 activeSessions_++，完全退出前 --；Stop() 等待归零。
    std::mutex sessMutex_;
    std::vector<uintptr_t> sessSocks_;   // 注册中的活动套接字（ctrl/data/pasv）
    std::condition_variable sessCv_;
    int activeSessions_ = 0;

    std::string rootPath_;

    // 权限配置的并发保护：GUI 可以在服务运行中修改权限，会话线程每条
    // 命令都要读取；Start/UpdatePermissions 写、Query* 读，统一加锁。
    mutable std::mutex permMutex_;
    FtpPermissions perms_;
    uint16_t pasvStart_ = 50000;
    uint16_t pasvEnd_ = 50100;
    std::string bindIp_;
    std::vector<std::string> allowedIps_;
    LogCallback logger_;
    std::atomic<int> fileNameEncoding_{0};   // 0=自动 1=UTF-8 2=GBK 3/4=兼容模式

    // 高级模式：按文件/文件夹覆盖权限
    bool advancedMode_ = false;
    std::vector<FilePermEntry> filePerms_;

    // 性能参数
    PerfOptions perf_;

    // 安全提醒
    SecurityOptions security_;
    ConfirmCallback confirm_;

    // 单 IP 当前连接数统计
    std::mutex ipCountMutex_;
    std::map<std::string, int> ipConnCount_;
};
