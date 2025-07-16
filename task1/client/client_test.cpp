/*compile command in cmd:
g++ client_test.cpp -o client_test.exe -lws2_32 -liphlpapi -lole32 -lssl -lcrypto -lboost_thread-mt
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

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")

#include "wintun.h"

using namespace boost::asio;
using namespace boost::system;

#define TUN_DEVICE_NAME L"VPNTun"
#define TUN_ADAPTER_NAME L"VPN Connection"
#define MAX_BUF_SIZE 1500

// Global variables
std::atomic<bool> have_quit(false);
WINTUN_SESSION_HANDLE tun_session = NULL;
io_context io_svc;

typedef BOOL(__stdcall* WintunCloseAdapter)(WINTUN_ADAPTER_HANDLE Adapter);
typedef WINTUN_ADAPTER_HANDLE(__stdcall* WintunCreateAdapter)(LPCWSTR Name, LPCWSTR TunnelType, const GUID* RequestedGUID);
typedef BOOL(__stdcall* WintunDeleteDriver)();
typedef VOID(__stdcall* WintunEndSession)(WINTUN_SESSION_HANDLE Session);
typedef WINTUN_ADAPTER_HANDLE(__stdcall* WintunOpenAdapter)(LPCWSTR Name);
typedef BYTE* (__stdcall* WintunReceivePacket)(WINTUN_SESSION_HANDLE Session, DWORD* PacketSize);
typedef VOID(__stdcall* WintunReleaseReceivePacket)(WINTUN_SESSION_HANDLE Session, const BYTE* Packet);
typedef BOOL(__stdcall* WintunSendPacket)(WINTUN_SESSION_HANDLE Session, const BYTE* Packet, DWORD PacketSize);
typedef WINTUN_SESSION_HANDLE(__stdcall* WintunStartSession)(WINTUN_ADAPTER_HANDLE Adapter, DWORD Capacity);

static WintunCreateAdapter WintunCreateAdapter_;
static WintunCloseAdapter WintunCloseAdapter_;
static WintunOpenAdapter WintunOpenAdapter_;
static WintunStartSession WintunStartSession_;
static WintunEndSession WintunEndSession_;
static WintunReceivePacket WintunReceivePacket_;
static WintunReleaseReceivePacket WintunReleaseReceivePacket_;
static WintunSendPacket WintunSendPacket_;
static WintunDeleteDriver WintunDeleteDriver_;

HMODULE wintun_lib = NULL;

// Load WinTun library
bool LoadWintunLibrary() {
    wintun_lib = LoadLibraryW(L"wintun.dll");
    if (!wintun_lib) {
        std::cerr << "❌ 无法加载wintun.dll" << std::endl;
        return false;
    }

#define LOAD_FUNC(Name) \
    Name##_ = (Name)GetProcAddress(wintun_lib, #Name); \
    if (!Name##_) { \
        std::cerr << "❌ 无法找到函数: " #Name << std::endl; \
        return false; \
    }

    LOAD_FUNC(WintunCreateAdapter);
    LOAD_FUNC(WintunCloseAdapter);
    LOAD_FUNC(WintunOpenAdapter);
    LOAD_FUNC(WintunStartSession);
    LOAD_FUNC(WintunEndSession);
    LOAD_FUNC(WintunReceivePacket);
    LOAD_FUNC(WintunReleaseReceivePacket);
    LOAD_FUNC(WintunSendPacket);
    LOAD_FUNC(WintunDeleteDriver);

#undef LOAD_FUNC

    std::cout << "✅ WinTun库加载成功" << std::endl;
    return true;
}

// Initialize SSL context
bool InitSSL(ssl::context& ssl_ctx) {
    try {
        ssl_ctx.set_default_verify_paths();
        ssl_ctx.set_verify_mode(ssl::verify_none); // 测试时不验证证书
        std::cout << "✅ SSL上下文初始化成功" << std::endl;
        return true;
    } catch (std::exception& e) {
        std::cerr << "❌ SSL初始化失败: " << e.what() << std::endl;
        return false;
    }
}

// 从服务端接收确认消息的线程
void ssl_to_client_thread(ssl::stream<ip::tcp::socket>& ssl_socket) {
    char buffer[1024];
    int ack_count = 0;
    
    while (!have_quit) {
        try {
            boost::system::error_code ec;
            size_t bytes_read = ssl_socket.read_some(asio::buffer(buffer, sizeof(buffer)), ec);
            
            if (ec) {
                if (ec == ssl::error::stream_truncated || ec == asio::error::eof) {
                    std::cout << "🔌 服务端关闭连接" << std::endl;
                    break;
                } else if (ec == asio::error::would_block) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                } else {
                    std::cerr << "❌ 接收确认失败: " << ec.message() << std::endl;
                    break;
                }
            }
            
            if (bytes_read > 0) {
                ack_count++;
                std::string ack_msg(buffer, bytes_read);
                std::cout << "✅ 收到服务端确认 #" << ack_count << ": " << ack_msg << std::endl;
            }
            
        } catch (std::exception& e) {
            std::cerr << "❌ 接收确认异常: " << e.what() << std::endl;
            break;
        }
    }
}

// 从TUN设备读取包并发送到服务端
void tun_to_ssl_thread(SSL* ssl) {
    int packet_sent = 0;
    
    while (!have_quit) {
        DWORD packet_size;
        BYTE* packet = WintunReceivePacket_(tun_session, &packet_size);
        if (!packet) {
            DWORD err = GetLastError();
            if (err == ERROR_NO_MORE_ITEMS) {
                Sleep(10); // 无数据时休眠
                continue;
            }
            std::cerr << "❌ TUN读取失败，错误码: " << err << std::endl;
            break;
        }

        packet_sent++;
        std::cout << "\n📤 发送数据包 #" << packet_sent << std::endl;
        std::cout << "   大小: " << packet_size << " 字节" << std::endl;
        
        // 打印前16字节的十六进制
        std::cout << "   前16字节: ";
        for (int i = 0; i < 16 && i < packet_size; ++i) {
            printf("%02X ", packet[i]);
        }
        std::cout << std::endl;

        // 发送到SSL服务端
        int ssl_result = SSL_write(ssl, packet, packet_size);
        
        if (ssl_result <= 0) {
            int ssl_error = SSL_get_error(ssl, ssl_result);
            std::cerr << "   ❌ SSL发送失败，错误: " << ssl_error << std::endl;
            
            char error_buf[256];
            ERR_error_string_n(ERR_get_error(), error_buf, sizeof(error_buf));
            std::cerr << "   OpenSSL错误: " << error_buf << std::endl;
            
            WintunReleaseReceivePacket_(tun_session, packet);
            break;
        } else {
            std::cout << "   ✅ SSL发送成功: " << ssl_result << " 字节" << std::endl;
        }

        WintunReleaseReceivePacket_(tun_session, packet);
    }
    
    std::cout << "🏁 TUN→SSL线程退出，共发送 " << packet_sent << " 个包" << std::endl;
}

// 初始化TUN设备
bool InitTunDevice() {
    // 创建TUN适配器
    WINTUN_ADAPTER_HANDLE tun_adapter = WintunCreateAdapter_(TUN_ADAPTER_NAME, TUN_DEVICE_NAME, nullptr);
    if (!tun_adapter) {
        // 尝试打开现有适配器
        tun_adapter = WintunOpenAdapter_(TUN_ADAPTER_NAME);
        if (!tun_adapter) {
            std::cerr << "❌ 无法创建或打开TUN适配器" << std::endl;
            return false;
        }
    }

    // 启动会话
    tun_session = WintunStartSession_(tun_adapter, 0x400000); // 4MB ring
    if (!tun_session) {
        std::cerr << "❌ 无法启动TUN会话" << std::endl;
        WintunCloseAdapter_(tun_adapter);
        return false;
    }

    std::cout << "✅ TUN设备初始化成功" << std::endl;
    return true;
}

// 测试数据传输的主函数
void test_data_transmission(const std::string& server_ip, int port) {
    try {
        // 初始化SSL上下文
        ssl::context ssl_ctx(ssl::context::tls_client);
        if (!InitSSL(ssl_ctx)) {
            return;
        }

        // 连接到服务器
        ip::tcp::socket socket(io_svc);
        ip::tcp::resolver resolver(io_svc);
        auto endpoints = resolver.resolve(server_ip, std::to_string(port));
        std::cout << "🔗 正在连接到 " << server_ip << ":" << port << "..." << std::endl;
        connect(socket, endpoints);
        
        // SSL连接
        ssl::stream<ip::tcp::socket> ssl_stream(io_svc, ssl_ctx);
        ssl_stream.lowest_layer() = std::move(socket);
        
        // 执行SSL握手
        boost::system::error_code ec;
        ssl_stream.handshake(ssl::stream_base::client, ec);
        if (ec) {
            std::cerr << "❌ SSL握手失败: " << ec.message() << std::endl;
            return;
        }
        
        // 验证SSL连接状态
        SSL* raw_ssl = ssl_stream.native_handle();
        if (!raw_ssl) {
            std::cerr << "❌ 无法获取SSL handle" << std::endl;
            return;
        }
        
        if (SSL_get_state(raw_ssl) != TLS_ST_OK) {
            std::cerr << "❌ SSL连接状态异常" << std::endl;
            return;
        }
        
        std::cout << "✅ SSL连接建立成功" << std::endl;

        // 启动接收确认消息的线程
        boost::thread receive_thread(boost::bind(&ssl_to_client_thread, boost::ref(ssl_stream)));

        // 启动TUN数据发送线程
        boost::thread tun_thread(boost::bind(&tun_to_ssl_thread, raw_ssl));

        std::cout << "\n🚀 数据传输测试已开始" << std::endl;
        std::cout << "📋 测试说明:" << std::endl;
        std::cout << "   - 请在Windows上ping其他网络地址 (如 ping 8.8.8.8)" << std::endl;
        std::cout << "   - 或访问网页，触发网络数据包" << std::endl;
        std::cout << "   - 观察服务端是否接收到数据包" << std::endl;
        std::cout << "   - 按Ctrl+C退出测试\n" << std::endl;

        // 等待用户中断
        std::string input;
        std::cout << "按回车键停止测试...";
        std::getline(std::cin, input);

        // 设置退出标志
        have_quit = true;

        // 等待线程结束
        if (tun_thread.joinable()) {
            tun_thread.join();
        }
        if (receive_thread.joinable()) {
            receive_thread.join();
        }

        std::cout << "🏁 测试完成" << std::endl;

    } catch (std::exception& e) {
        std::cerr << "❌ 测试异常: " << e.what() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "用法: " << argv[0] << " <服务器IP> <端口>" << std::endl;
        return 1;
    }

    std::string server_ip = argv[1];
    int port = std::atoi(argv[2]);

    std::cout << "=== VPN数据传输测试客户端 ===" << std::endl;
    std::cout << "目标服务器: " << server_ip << ":" << port << std::endl;
    std::cout << "================================\n" << std::endl;

    // 初始化Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "❌ WSAStartup失败" << std::endl;
        return 1;
    }

    // 加载WinTun库
    if (!LoadWintunLibrary()) {
        WSACleanup();
        return 1;
    }

    // 初始化TUN设备
    if (!InitTunDevice()) {
        FreeLibrary(wintun_lib);
        WSACleanup();
        return 1;
    }

    // 开始数据传输测试
    test_data_transmission(server_ip, port);

    // 清理资源
    if (tun_session) {
        WintunEndSession_(tun_session);
    }
    if (wintun_lib) {
        FreeLibrary(wintun_lib);
    }
    WSACleanup();

    return 0;
}