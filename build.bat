@echo off
chcp 65001 >nul
echo ============================================================
echo  使用 Visual Studio 2022 的 MSVC 编译器构建 FtpTool
echo ============================================================


set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)

if not exist "%VCVARS%" (
    echo [错误] 未找到 vcvars64.bat，请确认已安装 Visual Studio 2022 C++ 工具集。
    exit /b 1
)

call "%VCVARS%" >nul

if not exist build mkdir build
cd build

echo [1/4] 编译源码...
cl /nologo /std:c++17 /EHsc /utf-8 /W3 /O2 /MT ^
   /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 ^
   /I..\src ^
   ..\src\main.cpp ..\src\ftp_server.cpp ..\src\gui.cpp ..\src\config.cpp ^
   ..\src\ftp_client.cpp ..\src\ftp_drag.cpp ..\src\ftp_progress.cpp ^
   ..\src\ftp_fsops.cpp ..\src\winlongpath.cpp ^
   /c

if errorlevel 1 (
    echo [错误] 编译失败。
    exit /b 1
)

echo [2/4] 编译资源（图标 + 版本信息）...
rc /nologo /fo app.res ..\src\app.rc
if errorlevel 1 (
    echo [错误] 资源编译失败。
    exit /b 1
)

echo [3/4] 链接...
link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:EMBED ^
     /MANIFESTINPUT:..\src\app.manifest ^
     /OUT:FtpTool.exe ^
     main.obj ftp_server.obj gui.obj config.obj ftp_client.obj ftp_drag.obj ftp_progress.obj ftp_fsops.obj winlongpath.obj app.res ^
     ws2_32.lib iphlpapi.lib comctl32.lib shell32.lib ole32.lib user32.lib gdi32.lib shlwapi.lib

if errorlevel 1 (
    echo [错误] 链接失败。
    exit /b 1
)

echo [4/4] 完成
echo.
echo 构建完成: build\FtpTool.exe
endlocal
