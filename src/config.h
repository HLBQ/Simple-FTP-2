#pragma once

#include <string>
#include <vector>
#include "ftp_server.h"

// 快捷方式连接本条目
struct FtpBookmark {
    std::string name;      // 显示名称
    std::string host;      // IP 或主机名
    int port = 21;         // 端口
    std::string user;      // 用户名
    std::string pass;      // 密码
};

// 连接记录条目
struct ConnectRecord {
    std::string time;      // 时间
    std::string target;    // 目标（IP:端口 或 客户端IP）
    std::string detail;    // 详情
};

// 高级模式：单个文件/文件夹的权限
struct FilePermission {
    std::string path;      // 相对共享根目录的路径
    FtpPermissions perms;  // 该路径的权限
    bool hidden = false;   // 是否对客户端隐藏（不出现在目录列表中）
};

// 性能/传输参数（高级设置页，重启服务后生效）
struct PerfConfig {
    int  dataTimeoutSec = 60;      // 数据通道超时（秒），0=不超时
    bool tcpNoDelay = true;        // TCP_NODELAY（关闭 Nagle 合并）
    int  sockBufKB = 256;          // SO_SNDBUF/SO_RCVBUF 内核缓冲（KB）
    int  appBufKB = 64;            // 应用层传输缓冲区（KB）
    int  maxFileSizeMB = 0;        // 单文件最大大小（MB），0=不限
    int  maxConnPerIp = 20;        // 单 IP 最大并发连接数，0=不限
};

// 安全提醒配置（安全提醒页，重启服务后生效）
struct SecurityConfig {
    bool confirmDeleteFile = true;   // 删除文件时弹窗确认
    bool confirmDeleteDir  = true;   // 删除目录时弹窗确认
    bool confirmRename     = false;  // 重命名/移动时弹窗确认
    bool confirmOverwrite  = false;  // 上传覆盖已存在文件时弹窗确认
    bool confirmDownload   = false;  // 下载文件时弹窗确认（默认关闭）
    bool deleteToRecycleBin = false; // 删除时移动到系统回收站（否则彻底删除）
    int  confirmTimeoutSec = 30;     // 弹窗等待超时（秒），超时视为拒绝
    bool ipBlacklist = false;        // IP 列表模式：false=白名单，true=黑名单
};


// 应用配置
struct AppConfig {
    // 基本设置
    int port = 21;
    std::string sharePath;
    int pasvStart = 50000;
    int pasvEnd = 50100;
    FtpPermissions perms;
    std::vector<std::string> allowedIps;

    // 其他设置：服务监听的网卡地址（"0.0.0.0" = 所有网卡）
    std::string bindIp = "0.0.0.0";
    // 文件名编码：0=自动识别，1=仅 UTF-8，2=仅 GBK/本地编码，
    // 3=兼容模式(尝试修复：非法字符换全角字符)，4=兼容模式(跳过解码：换空格)
    int fileNameEncoding = 0;

    // 快捷方式连接本
    std::vector<FtpBookmark> bookmarks;

    // 连接记录
    std::vector<ConnectRecord> records;

    // 高级模式：启用后按文件/文件夹单独控制权限
    bool advancedMode = false;
    // 界面模式：false=高级模式（完整界面），true=精简模式
    bool compactMode = false;
    // 精简模式的三类权限（三类都包含列目录；写入=上传/建目录/重命名，
    // 删除=删文件/删目录）
    bool compactPermRead = true;
    bool compactPermWrite = false;
    bool compactPermDelete = false;
    std::vector<FilePermission> filePerms;

    // 性能/传输参数
    PerfConfig perf;

    // 安全提醒
    SecurityConfig security;

    // 客户端页：上次连接的服务器地址与端口（下次启动自动回填）
    std::string cliHost = "127.0.0.1";
    int cliPort = 21;
    std::string cliUser = "anonymous";  // 内置客户端默认登录名
    std::string cliPass = "";           // 内置客户端默认密码
    bool cliAnonymous = true;           // 登录方式：true=匿名，false=用户名+密码

    // 客户端传输模式（0=快速 1=均衡 2=安全 3=纯标准 4=禁用）
    //  快速: REST续传开, 哈希关
    //  均衡: REST续传开, 哈希开（默认）
    //  安全: REST续传关, 哈希开
    //  纯标准: REST续传关, 哈希关, 仍发FEAT探测
    //  禁用: REST续传关, 哈希关, 连FEAT都不发（极端兼容）
    int transferMode = 1;

    // 运行日志页：分类过滤开关（下次启动自动恢复）
    bool logFilterUpload = true;    // 上传日志
    bool logFilterDownload = true;  // 下载日志
    bool logFilterClient = true;    // 客户端日志
    bool logFilterOther = true;     // 其他日志
    bool logFilterWarn = true;      // 警告日志

    // 加载配置（文件不存在则返回默认值）
    static AppConfig Load(const std::string& file);
    // 保存配置
    void Save(const std::string& file) const;

    // 默认配置文件路径（exe 同目录 config.ini）
    static std::string DefaultPath();
};
