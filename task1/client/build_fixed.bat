@echo off
echo === VPN客户端修复版编译脚本 ===

REM 检查编译器
where g++ >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo 错误: g++ 未找到，请确保MinGW-w64已安装并添加到PATH
    pause
    exit /b 1
)

REM 检查必要文件
if not exist "wintun.h" (
    echo 错误: wintun.h 文件不存在
    echo 请从 https://www.wintun.net/ 下载WinTun SDK
    pause
    exit /b 1
)

if not exist "wintun.dll" (
    echo 错误: wintun.dll 文件不存在
    echo 请从 https://www.wintun.net/ 下载WinTun驱动
    pause
    exit /b 1
)

REM 检查证书文件
if not exist "certs\server.crt" (
    echo 警告: SSL证书文件不存在
    echo 请确保以下文件存在:
    echo   - certs\server.crt
    echo   - certs\client.crt
    echo   - certs\client.key
    echo.
)

REM 编译
echo 开始编译...
g++ client_fixed.cpp -o client_fixed.exe ^
    -lws2_32 -liphlpapi -lole32 -lssl -lcrypto -lboost_thread-mt ^
    -std=c++11

if %ERRORLEVEL% equ 0 (
    echo ✅ 编译成功！
    echo.
    echo 运行方法:
    echo client_fixed.exe ^<服务器IP^> 443
    echo.
    echo 注意事项:
    echo 1. 需要管理员权限运行
    echo 2. 确保wintun.dll在同一目录
    echo 3. 确保证书文件在certs目录中
) else (
    echo ❌ 编译失败！
    echo.
    echo 可能的原因:
    echo 1. 缺少Boost库
    echo 2. 缺少OpenSSL库
    echo 3. 编译器版本不兼容
)

pause