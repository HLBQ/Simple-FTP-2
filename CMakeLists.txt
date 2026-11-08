cmake_minimum_required(VERSION 3.15)
project(FtpTool CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8 /W3 /EHsc)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601)
endif()

add_executable(FtpTool WIN32
    src/main.cpp
    src/ftp_server.cpp
    src/gui.cpp
    src/config.cpp
    src/ftp_client.cpp
    src/ftp_drag.cpp
    src/ftp_progress.cpp
    src/ftp_fsops.cpp
    src/winlongpath.cpp
)

target_include_directories(FtpTool PRIVATE src)

target_link_libraries(FtpTool PRIVATE
    ws2_32 iphlpapi bcrypt
    comctl32 comdlg32 shell32 shlwapi ole32
    user32 gdi32 windowscodecs
)
