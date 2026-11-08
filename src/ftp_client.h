#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

// FTP 客户端：实现一个精简但完整的 FTP 协议客户端，
// 供内置“客户端”分页使用（浏览目录、上传、下载、删除、重命名等）。
//
// 说明：
//  - 所有方法均为阻塞式，调用方应在工作线程中执行耗时操作（如上传/下载）。
//  - 文件名统一使用 UTF-8 编码，内部按需转换为本地编码或 UTF-8 发送。
class FtpClient {
public:
    // 传输模式（决定是否使用 REST 续传 / XMD5 哈希 / FEAT 探测）
    //   Fast     = 续传开, 哈希关
    //   Balanced = 续传开, 哈希开（默认）
    //   Safe     = 续传关, 哈希开
    //   Standard = 续传关, 哈希关, 仍发 FEAT
    //   Disabled = 续传关, 哈希关, 连 FEAT 都不发（极端兼容）
    enum class TransferMode { Fast, Balanced, Safe, Standard, Disabled };

    FtpClient();
    ~FtpClient();

    // 设置传输模式（应在 Connect 之前调用，或下次下载生效）
    void SetTransferMode(TransferMode mode) { mode_ = mode; }
    TransferMode GetTransferMode() const { return mode_; }

    // 服务器是否支持 REST 续传（Connect 后通过 FEAT 探测）
    bool SupportsRest() const { return supportRest_; }
    // 服务器是否支持 XMD5 哈希（Connect 后通过 FEAT 探测）
    bool SupportsXmd5() const { return supportXmd5_; }

    // 连接并登录。成功返回 true。
    // host: 服务器地址；port: 端口；user/pass: 登录凭据。
    bool Connect(const std::string& host, uint16_t port,
                 const std::string& user, const std::string& pass);

    // 断开连接
    void Disconnect();

    bool IsConnected() const { return ctrlSock_ != (uintptr_t)-1; }

    // 当前工作目录（服务器端）
    const std::string& CurrentDir() const { return cwd_; }

    // 目录项
    struct Entry {
        std::string name;   // 文件名（UTF-8）
        bool isDir = false; // 是否目录
        uint64_t size = 0;  // 文件大小（字节）
        std::string modify; // 修改时间字符串（原始）
    };

    // 列出当前目录（LIST）。成功返回 true，结果写入 out。
    bool List(std::vector<Entry>& out);

    // 切换目录（CWD）。name 为相对或绝对路径。
    bool ChangeDir(const std::string& name);

    // 返回上级目录（CDUP）
    bool ChangeDirUp();

    // 进度回调：参数为 (已传输字节数, 总字节数)。
    // 总字节数可能为 0（未知大小）。回调在工作线程中调用。
    using ProgressFn = std::function<void(uint64_t done, uint64_t total)>;

    // 下载文件到本地路径。成功返回 true。
    bool Download(const std::string& remoteName, const std::string& localPath);

    // 带进度回调的下载。totalSize 为已知的远程文件大小（0 表示未知）。
    // hashProgress: 哈希校验阶段的进度回调（可选），参数为 (已哈希字节, 文件大小)。
    bool Download(const std::string& remoteName, const std::string& localPath,
                  uint64_t totalSize, const ProgressFn& progress,
                  const ProgressFn& hashProgress = nullptr);

    // ---- 流式下载（供拖拽虚拟文件使用）----
    // 打开远程文件的下载流：发送 RETR 并建立数据连接。
    // 成功返回数据连接句柄（>0），失败返回 (uintptr_t)-1。
    uintptr_t OpenDownloadStream(const std::string& remoteName);

    // 从下载流读取数据。返回实际读取字节数，0 表示结束，<0 表示错误。
    int ReadStreamData(uintptr_t stream, char* buf, int size);

    // 关闭下载流（关闭数据连接并读取传输完成响应）。
    void CloseDownloadStream(uintptr_t stream);

    // 上传本地文件到服务器（STOR）。成功返回 true。
    bool Upload(const std::string& localPath, const std::string& remoteName);

    // 带进度回调的上传。totalSize 为本地文件大小（0 表示未知）。
    // hashProgress: 哈希校验阶段的进度回调（可选）。
    bool Upload(const std::string& localPath, const std::string& remoteName,
                uint64_t totalSize, const ProgressFn& progress,
                const ProgressFn& hashProgress = nullptr);

    // 原始上传：从 offset 字节处开始上传（REST 续传）。
    bool UploadRaw(const std::string& localPath, const std::string& remoteName,
                   uint64_t offset, uint64_t totalSize, const ProgressFn& progress);

    // 删除文件（DELE）。注意：不能命名为 DeleteFile，会与 Windows 宏冲突。
    bool RemoveFile(const std::string& name);

    // 创建目录（MKD）
    bool MakeDir(const std::string& name);

    // 删除目录（RMD）
    bool RemoveDir(const std::string& name);

    // 重命名（RNFR/RNTO）
    bool Rename(const std::string& from, const std::string& to);

    // 获取远程文件大小（SIZE 命令），失败返回 (uint64_t)-1
    uint64_t GetRemoteSize(const std::string& remoteName);

    // 获取远程文件 MD5（XMD5 命令），失败返回空字符串
    std::string GetRemoteMd5(const std::string& remoteName);

    // 计算本地文件 MD5（小写十六进制），失败返回空字符串。
    // progress: 可选进度回调 (已读字节, 文件大小)
    static std::string ComputeLocalMd5(const std::string& localPath,
                                       const ProgressFn& progress = nullptr);

    // 最近一次错误信息
    const std::string& LastError() const { return lastError_; }

private:
    // 发送命令并读取一行响应（返回响应码，-1 表示失败）
    int SendCmd(const std::string& cmd, std::string* respLine = nullptr);

    // 读取一行响应（以 \r\n 结尾）
    bool ReadLine(std::string& line);

    // 读取完整响应（处理多行响应，如 220-...）
    int ReadResponse(std::string* firstLine = nullptr);

    // 发送 FEAT 并解析服务器支持的扩展（REST / XMD5 等）
    void DetectFeatures();

    // 进入被动模式，返回数据连接套接字（失败返回 -1）
    uintptr_t OpenPassive();

    // 原始下载：从 offset 字节处开始下载（REST 续传）。
    // offset=0 时为全新下载（截断本地文件）；offset>0 时追加到本地文件末尾。
    bool DownloadRaw(const std::string& remoteName, const std::string& localPath,
                     uint64_t offset, uint64_t totalSize, const ProgressFn& progress);

    // 编码/解码：UTF-8 <-> 本地编码
    std::string ToServer(const std::string& utf8) const;
    std::string FromServer(const std::string& raw) const;

    // 把 UTF-8 名字编码成发往服务器的字节串。
    // 返回 false 表示该名字在当前会话编码（GBK）下无法无损表示——
    // 此时不发送命令，并在 lastError_ 里说明原因，避免名字被悄悄
    // 换成 '?' 传到服务端后才报 550。
    bool EncodeName(const std::string& utf8, std::string& out) const;

    uintptr_t ctrlSock_ = (uintptr_t)-1;
    std::string cwd_ = "/";
    mutable std::string lastError_;
    bool utf8Mode_ = false;   // 服务器是否支持 UTF8
    TransferMode mode_ = TransferMode::Balanced;
    bool supportRest_ = false;   // 服务器 FEAT 中是否有 REST STREAM
    bool supportXmd5_ = false;   // 服务器 FEAT 中是否有 XMD5
};
