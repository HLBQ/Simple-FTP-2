#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

class FtpClient;

// 拖拽项：描述一个要从 FTP 拖出的文件或文件夹
struct FtpDragItem {
    std::string remotePath;   // 服务器上的完整路径（如 /dir/file.txt）
    std::wstring name;        // 显示名（文件名/文件夹名）
    bool isDir = false;       // 是否文件夹
    uint64_t size = 0;        // 文件大小（字节），文件夹为 0
};

// 启动一次 FTP 虚拟文件拖拽。
//   hwnd   : 发起拖拽的窗口（用于 DoDragDrop）
//   client : 已连接的 FTP 客户端（拖拽期间由本模块加锁使用）
//   items  : 要拖出的项（文件/文件夹）
//   mtx    : 保护 client 的互斥量（必须与 GUI 其它线程共用同一把锁，
//            否则拖出下载流与 GUI 操作会互相损坏传输）
// 说明：本函数会阻塞直到拖拽结束（DoDragDrop 内部消息循环）。
//       数据通过 CFSTR_FILECONTENTS 虚拟流按需从 FTP 拉取，
//       不经过本地临时文件，因此不会占用 C 盘空间。
void StartFtpDrag(HWND hwnd, FtpClient* client,
                  const std::vector<FtpDragItem>& items, std::mutex* mtx);

// 注册一个“拖入”目标：把资源管理器中的本地文件/文件夹拖到该窗口上时，
// 自动上传到 FTP 当前目录。
//   hwnd    : 接收拖放的窗口（注册 OLE drop target 的窗口，通常是主窗口）
//   hitHwnd : 命中判定控件（通常是客户端文件列表）；仅当鼠标位于该控件
//             范围内时才接受拖放；传 nullptr 表示整个窗口都接受
//   client  : 已连接的 FTP 客户端（拖放期间由本模块加锁使用）
//   mtx     : 保护 client 的互斥量（与其它线程共享同一把锁）
// 说明：内部会创建 IDropTarget 并调用 RegisterDragDrop。
//       上传在后台线程执行，不阻塞 UI。
void RegisterFtpDropTarget(HWND hwnd, HWND hitHwnd, FtpClient* client,
                           std::mutex* mtx);

// 注销拖入目标（窗口销毁前调用）
void RevokeFtpDropTarget(HWND hwnd);
