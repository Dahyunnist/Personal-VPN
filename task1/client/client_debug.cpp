/*compile command in cmd:
g++ client_debug.cpp -o client_debug.exe -lws2_32 -liphlpapi -lole32 -lssl -lcrypto -lboost_thread-mt
*/

#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/thread.hpp>
#include <boost/bind/bind.hpp>
#include <boost/smart_ptr.hpp>
#include <windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <atomic>
#include <cstring>
#include <chrono>
#include <thread>
#include <iomanip>
#include "wintun.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")

namespace asio = boost::asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp; 

#define TUN_DEVICE_NAME L"VPNTunnel"
#define TUN_POOL_NAME L"VPNPool"
#define MAX_BUF_SIZE 65536

// Declare Wintun function pointers
static WINTUN_CREATE_ADAPTER_FUNC *WintunCreateAdapter;
static WINTUN_CLOSE_ADAPTER_FUNC *WintunCloseAdapter;
static WINTUN_OPEN_ADAPTER_FUNC *WintunOpenAdapter;
static WINTUN_GET_ADAPTER_LUID_FUNC *WintunGetAdapterLUID;
static WINTUN_GET_RUNNING_DRIVER_VERSION_FUNC *WintunGetRunningDriverVersion;
static WINTUN_DELETE_DRIVER_FUNC *WintunDeleteDriver;
static WINTUN_SET_LOGGER_FUNC *WintunSetLogger;
static WINTUN_START_SESSION_FUNC *WintunStartSession;
static WINTUN_END_SESSION_FUNC *WintunEndSession;
static WINTUN_GET_READ_WAIT_EVENT_FUNC *WintunGetReadWaitEvent;
static WINTUN_RECEIVE_PACKET_FUNC *WintunReceivePacket;
static WINTUN_RELEASE_RECEIVE_PACKET_FUNC *WintunReleaseReceivePacket;
static WINTUN_ALLOCATE_SEND_PACKET_FUNC *WintunAllocateSendPacket;
static WINTUN_SEND_PACKET_FUNC *WintunSendPacket;

static HMODULE wintun_module = NULL;
static WINTUN_ADAPTER_HANDLE tun_adapter = NULL;
static WINTUN_SESSION_HANDLE tun_session = NULL;
static NET_LUID tun_luid;
static std::atomic_bool have_quit(false);
static asio::io_context io_ctx;
static asio::executor_work_guard<asio::io_context::executor_type> work_guard{io_ctx.get_executor()};

// 简化SSL配置，避免证书问题
bool InitSSL(ssl::context& ctx) {
    try {
        std::cout << "🔐 初始化SSL上下文..." << std::endl;
        
        // 基本配置
        ctx.set_options(
            ssl::context::default_workarounds | 
            ssl::context::no_sslv2 | 
            ssl::context::no_sslv3
        );
        
        // 测试模式：不验证服务端证书
        ctx.set_verify_mode(ssl::verify_none);
        
        std::cout << "✅ SSL上下文初始化成功（测试模式：不验证证书）" << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "❌ SSL初始化失败: " << e.what() << std::endl;
        return false;
    }
}

bool InitializeWintun() {
    std::cout << "🔧 加载WinTun库..." << std::endl;
    wintun_module = LoadLibraryExW(L"wintun.dll", NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!wintun_module) {
        std::cerr << "❌ 无法加载wintun.dll. 错误: " << GetLastError() << std::endl;
        return false;
    }

#define X(Name) ((*(FARPROC *)&Name = GetProcAddress(wintun_module, #Name)) == NULL)
    if (X(WintunCreateAdapter) || X(WintunCloseAdapter) || X(WintunOpenAdapter) || X(WintunGetAdapterLUID) ||
        X(WintunGetRunningDriverVersion) || X(WintunDeleteDriver) || X(WintunSetLogger) || X(WintunStartSession) ||
        X(WintunEndSession) || X(WintunGetReadWaitEvent) || X(WintunReceivePacket) || X(WintunReleaseReceivePacket) ||
        X(WintunAllocateSendPacket) || X(WintunSendPacket)) {
        DWORD LastError = GetLastError();
        FreeLibrary(wintun_module);
        SetLastError(LastError);
        std::cerr << "❌ 无法加载WinTun函数" << std::endl;
        return false;
    }
#undef X
    
    std::cout << "✅ WinTun库加载成功" << std::endl;
    return true;
}

void CleanupWintun() {
    if (tun_session) {
        WintunEndSession(tun_session);
        tun_session = NULL;
    }
    if (tun_adapter) {
        WintunCloseAdapter(tun_adapter);
        tun_adapter = NULL;
    }
    if (wintun_module) {
        FreeLibrary(wintun_module);
        wintun_module = NULL;
    }
}

bool init_wintun_adapter(const char* ip, int prefix) {
    std::cout << "🌐 创建TUN适配器..." << std::endl;
    
    GUID guid;
    CoCreateGuid(&guid);
    tun_adapter = WintunCreateAdapter(TUN_POOL_NAME, TUN_DEVICE_NAME, &guid);
    if (!tun_adapter) {
        tun_adapter = WintunOpenAdapter(TUN_DEVICE_NAME);
        if (!tun_adapter) {
            std::cerr << "❌ 无法创建/打开WinTUN适配器. 错误: " << GetLastError() << std::endl;
            return false;
        }
    }
    
    WintunGetAdapterLUID(tun_adapter, &tun_luid);
    if (tun_luid.Value == 0) {
        std::cerr << "❌ TUN适配器LUID无效" << std::endl;
        WintunCloseAdapter(tun_adapter);
        tun_adapter = NULL;
        return false;
    }
    
    std::cout << "✅ TUN适配器创建成功, LUID: 0x" << std::hex << tun_luid.Value << std::dec << std::endl;
    
    tun_session = WintunStartSession(tun_adapter, 0x400000);
    if (!tun_session) {
        std::cerr << "❌ 无法启动WinTUN会话. 错误: " << GetLastError() << std::endl;
        return false;
    }
    
    // 配置IP地址
    MIB_UNICASTIPADDRESS_ROW ipRow;
    InitializeUnicastIpAddressEntry(&ipRow);
    ipRow.InterfaceLuid = tun_luid;
    ipRow.Address.Ipv4.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &ipRow.Address.Ipv4.sin_addr);
    ipRow.OnLinkPrefixLength = prefix;
    ipRow.DadState = IpDadStatePreferred;
    
    DWORD result = CreateUnicastIpAddressEntry(&ipRow);
    if (result != ERROR_SUCCESS && result != ERROR_OBJECT_ALREADY_EXISTS) {
        std::cerr << "⚠️ 设置IP地址失败. 错误: " << result << " (可能已存在)" << std::endl;
    } else {
        std::cout << "✅ TUN设备IP配置成功: " << ip << "/" << prefix << std::endl;
    }
    
    return true;
}

// 改进的TUN读取线程
void tun_to_ssl_thread(ssl::stream<tcp::socket>& socket) {
    std::cout << "🔄 TUN→SSL线程启动" << std::endl;
    
    auto buffer = std::make_shared<std::vector<uint8_t>>(MAX_BUF_SIZE);
    int packet_count = 0;
    
    std::function<void()> async_send = [&, buffer]() {
        if (have_quit) {
            std::cout << "🛑 TUN→SSL线程退出" << std::endl;
            return;
        }
        
        DWORD packet_size;
        BYTE* packet = WintunReceivePacket(tun_session, &packet_size);
        
        if (!packet) {
            DWORD err = GetLastError();
            if (err == ERROR_NO_MORE_ITEMS) {
                // 没有数据包，继续等待
                asio::post(io_ctx, [async_send]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    async_send();
                });
                return;
            } else {
                std::cerr << "❌ TUN读取错误: " << err << std::endl;
                return;
            }
        }
        
        packet_count++;
        std::cout << "\n📤 发送数据包 #" << packet_count << " (大小: " << packet_size << " 字节)" << std::endl;
        
        // 复制数据到buffer
        memcpy(buffer->data(), packet, packet_size);
        
        // 异步发送
        asio::async_write(socket, asio::buffer(*buffer, packet_size),
            [&, packet, async_send](boost::system::error_code ec, size_t bytes_sent) {
                WintunReleaseReceivePacket(tun_session, packet);
                
                if (!ec) {
                    std::cout << "   ✅ SSL发送成功: " << bytes_sent << " 字节" << std::endl;
                    async_send(); // 继续读取下一个包
                } else {
                    std::cerr << "   ❌ SSL发送失败: " << ec.message() << std::endl;
                    if (ec != asio::error::operation_aborted) {
                        have_quit = true;
                    }
                }
            });
    };
    
    async_send();
}

// 改进的SSL读取线程
void ssl_to_tun_thread(ssl::stream<tcp::socket>& socket) {
    std::cout << "🔄 SSL→TUN线程启动" << std::endl;
    
    auto buffer = std::make_shared<std::vector<uint8_t>>(MAX_BUF_SIZE);
    int packet_count = 0;
    
    std::function<void()> async_read = [&, buffer]() {
        if (have_quit) {
            std::cout << "🛑 SSL→TUN线程退出" << std::endl;
            return;
        }
        
        socket.async_read_some(asio::buffer(*buffer),
            [&, buffer, async_read](boost::system::error_code ec, size_t bytes_read) {
                if (ec) {
                    if (ec == ssl::error::stream_truncated || ec == asio::error::eof) {
                        std::cout << "🔌 服务端正常关闭连接" << std::endl;
                    } else if (ec == asio::error::operation_aborted) {
                        std::cout << "🛑 SSL读取操作被取消" << std::endl;
                    } else {
                        std::cerr << "❌ SSL读取错误: " << ec.message() << std::endl;
                    }
                    have_quit = true;
                    return;
                }
                
                packet_count++;
                std::cout << "\n📥 收到数据包 #" << packet_count << " (大小: " << bytes_read << " 字节)" << std::endl;
                
                // 发送到TUN设备
                BYTE* tun_packet = WintunAllocateSendPacket(tun_session, bytes_read);
                if (!tun_packet) {
                    std::cerr << "   ❌ TUN包分配失败: " << GetLastError() << std::endl;
                } else {
                    memcpy(tun_packet, buffer->data(), bytes_read);
                    WintunSendPacket(tun_session, tun_packet);
                    std::cout << "   ✅ TUN发送成功" << std::endl;
                }
                
                async_read(); // 继续读取
            });
    };
    
    async_read();
}

void vpn_client(const std::string& server_ip, int server_port) {
    try {
        std::cout << "\n🚀 启动VPN客户端..." << std::endl;
        
        // 初始化SSL上下文
        ssl::context ssl_ctx(ssl::context::tls_client);
        if (!InitSSL(ssl_ctx)) {
            return;
        }
        
        // 创建SSL流
        ssl::stream<tcp::socket> socket(io_ctx, ssl_ctx);
        
        std::cout << "🔗 连接到服务器 " << server_ip << ":" << server_port << "..." << std::endl;
        
        // 解析地址
        asio::ip::tcp::resolver resolver(io_ctx);
        auto endpoints = resolver.resolve(server_ip, std::to_string(server_port));
        
        // 连接
        boost::system::error_code connect_ec;
        asio::connect(socket.lowest_layer(), endpoints, connect_ec);
        if (connect_ec) {
            std::cerr << "❌ 连接失败: " << connect_ec.message() << std::endl;
            return;
        }
        std::cout << "✅ TCP连接建立成功" << std::endl;
        
        // SSL握手
        boost::system::error_code handshake_ec;
        socket.handshake(ssl::stream_base::client, handshake_ec);
        if (handshake_ec) {
            std::cerr << "❌ SSL握手失败: " << handshake_ec.message() << std::endl;
            
            // 打印详细的SSL错误信息
            unsigned long ssl_err = ERR_get_error();
            if (ssl_err != 0) {
                char err_buf[256];
                ERR_error_string_n(ssl_err, err_buf, sizeof(err_buf));
                std::cerr << "   SSL错误详情: " << err_buf << std::endl;
            }
            return;
        }
        
        std::cout << "🔐 SSL连接建立成功!" << std::endl;
        std::cout << "   版本: " << SSL_get_version(socket.native_handle()) << std::endl;
        std::cout << "   加密套件: " << SSL_get_cipher_name(socket.native_handle()) << std::endl;
        
        // 启动数据转发线程
        boost::thread tun_thread(boost::bind(&tun_to_ssl_thread, boost::ref(socket)));
        boost::thread ssl_thread(boost::bind(&ssl_to_tun_thread, boost::ref(socket)));
        
        std::cout << "\n📡 数据转发已启动，等待网络流量..." << std::endl;
        std::cout << "💡 提示: 请尝试ping 8.8.8.8或访问网页来测试VPN" << std::endl;
        
        // 运行I/O上下文
        io_ctx.run();
        
        // 等待线程结束
        if (tun_thread.joinable()) tun_thread.join();
        if (ssl_thread.joinable()) ssl_thread.join();
        
        std::cout << "🏁 VPN客户端退出" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ VPN客户端异常: " << e.what() << std::endl;
        have_quit = true;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "用法: " << argv[0] << " <服务器IP> <端口>" << std::endl;
        return 1;
    }

    std::cout << "=== VPN客户端调试版 ===" << std::endl;
    std::cout << "目标: " << argv[1] << ":" << argv[2] << std::endl;
    std::cout << "========================\n" << std::endl;

    // 初始化Winsock
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "❌ WSAStartup失败. 错误: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // 初始化OpenSSL
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    try {
        // 初始化WinTun
        if (!InitializeWintun()) {
            WSACleanup();
            return 1;
        }

        // 创建TUN适配器
        if (!init_wintun_adapter("10.8.0.2", 24)) {
            CleanupWintun();
            WSACleanup();
            return 1;
        }

        // 启动VPN客户端
        vpn_client(argv[1], std::stoi(argv[2]));

    } catch (const std::exception& e) {
        std::cerr << "❌ 程序异常: " << e.what() << std::endl;
    }

    // 清理资源
    have_quit = true;
    io_ctx.stop();
    CleanupWintun();
    WSACleanup();
    EVP_cleanup();

    return 0;
}