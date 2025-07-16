/*compile command in cmd:
g++ client_fixed.cpp -o client_fixed.exe -lws2_32 -liphlpapi -lole32 -lssl -lcrypto -lboost_thread-mt
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

#define TUN_DEVICE_NAME L"VPNTunnel"
#define TUN_POOL_NAME L"VPNPool"
#define MAX_BUF_SIZE 65536

// Wintun function pointers
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
static io_context io_svc;
static executor_work_guard<io_context::executor_type> work_guard{io_svc.get_executor()};
static boost::thread_group thread_pool;
static signal_set signals(io_svc, SIGINT, SIGTERM);

const std::string CA_CERT_PATH = "certs/server.crt";
const std::string CLIENT_CERT_PATH = "certs/client.crt";
const std::string CLIENT_KEY_PATH = "certs/client.key";

bool InitializeWintun() {
    wintun_module = LoadLibraryExW(L"wintun.dll", NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!wintun_module) {
        std::cerr << "Failed to load wintun.dll. Error: " << GetLastError() << std::endl;
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
        return false;
    }
#undef X
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
    GUID guid;
    CoCreateGuid(&guid);
    tun_adapter = WintunCreateAdapter(TUN_POOL_NAME, TUN_DEVICE_NAME, &guid);
    if (!tun_adapter) {
        tun_adapter = WintunOpenAdapter(TUN_DEVICE_NAME);
        if (!tun_adapter) {
            std::cerr << "Failed to create/open WinTUN adapter. Error: " << GetLastError() << std::endl;
            return false;
        }
    }

    WintunGetAdapterLUID(tun_adapter, &tun_luid);
    if (tun_luid.Value == 0) {
        std::cerr << "Error: TUN adapter LUID is invalid (Value=0)." << std::endl;
        WintunCloseAdapter(tun_adapter);
        tun_adapter = NULL;
        return false;
    }
    std::cout << "TUN adapter LUID: 0x" << std::hex << tun_luid.Value << std::dec << std::endl;

    tun_session = WintunStartSession(tun_adapter, 0x400000);
    if (!tun_session) {
        std::cerr << "Failed to start WinTUN session. Error: " << GetLastError() << std::endl;
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
        std::cerr << "Failed to set IP address. Error: " << result << std::endl;
        return false;
    }

    // 添加路由
    MIB_IPFORWARD_ROW2 route;
    InitializeIpForwardEntry(&route);
    route.InterfaceLuid = tun_luid;
    route.DestinationPrefix.Prefix.si_family = AF_INET;
    inet_pton(AF_INET, "8.8.8.8", &route.DestinationPrefix.Prefix.Ipv4.sin_addr);
    route.DestinationPrefix.PrefixLength = 32;
    inet_pton(AF_INET, "10.8.0.1", &route.NextHop.Ipv4.sin_addr);
    route.Metric = 1;
    route.Protocol = static_cast<NL_ROUTE_PROTOCOL>(3);
    DWORD route_result = CreateIpForwardEntry2(&route);
    if(route_result != ERROR_SUCCESS && route_result != ERROR_OBJECT_ALREADY_EXISTS){
        std::cerr << "Failed to add route for 8.8.8.8 Error: " << route_result << std::endl;
        return false;
    }
    FlushIpPathTable(AF_INET);
    std::cout << "Route added for 8.8.8.8 -> " << ip << "(via TUN)" << std::endl;
    return true;
}

// TUN → SSL 数据转发（修复版）
void tun_to_ssl_thread(SSL* ssl) {
    std::cout << "TUN → SSL 线程启动" << std::endl;
    
    while (!have_quit) {
        DWORD packet_size;
        BYTE* packet = WintunReceivePacket(tun_session, &packet_size);
        if (!packet) {
            DWORD err = GetLastError();
            if (err == ERROR_NO_MORE_ITEMS) {
                Sleep(10);
                continue;
            }
            std::cerr << "TUN 读取失败，错误码: " << err << std::endl;
            break;
        }

        std::cout << "TUN → SSL: 捕获数据包 " << packet_size << " 字节" << std::endl;
        
        // 打印数据包前几个字节
        std::cout << "数据包内容（前16字节）: ";
        for (DWORD i = 0; i < std::min(packet_size, static_cast<DWORD>(16)); ++i) {
            printf("%02X ", packet[i]);
        }
        std::cout << std::endl;

        if (ssl) {
            // 验证SSL连接状态
            if (SSL_get_state(ssl) != TLS_ST_OK) {
                std::cerr << "SSL connection is not in valid state" << std::endl;
                WintunReleaseReceivePacket(tun_session, packet);
                break;
            }
            
            // 发送数据包到服务端
            int bytes_sent = SSL_write(ssl, packet, packet_size);
            if (bytes_sent <= 0) {
                int ssl_error = SSL_get_error(ssl, bytes_sent);
                switch (ssl_error) {
                    case SSL_ERROR_WANT_WRITE:
                    case SSL_ERROR_WANT_READ:
                        std::cout << "SSL write would block, retrying..." << std::endl;
                        Sleep(1);
                        WintunReleaseReceivePacket(tun_session, packet);
                        continue;
                    case SSL_ERROR_ZERO_RETURN:
                        std::cerr << "SSL connection closed by peer" << std::endl;
                        break;
                    default:
                        std::cerr << "SSL send failed. Error: " << ssl_error << std::endl;
                        break;
                }
                WintunReleaseReceivePacket(tun_session, packet);
                break;
            }
            std::cout << "TUN → SSL: 成功发送 " << bytes_sent << " 字节到服务端" << std::endl;
        } else {
            std::cerr << "SSL connection not established" << std::endl;
            WintunReleaseReceivePacket(tun_session, packet);
            break;
        }

        WintunReleaseReceivePacket(tun_session, packet);
    }
    std::cout << "TUN → SSL 线程退出" << std::endl;
}

// SSL → TUN 数据转发（修复版）
void ssl_to_tun_thread(ssl::stream<ip::tcp::socket>& socket) {
    std::cout << "SSL → TUN 线程启动" << std::endl;
    char buf[MAX_BUF_SIZE];
    
    while (!have_quit) {
        boost::system::error_code ec;
        size_t bytes_read = socket.read_some(asio::buffer(buf, MAX_BUF_SIZE), ec);
        
        if (ec) {
            if (ec == ssl::error::stream_truncated || ec == asio::error::eof) {
                std::cerr << "SSL → TUN: 服务端关闭连接" << std::endl;
                break;
            } else if (ec == asio::error::would_block) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            } else {
                std::cerr << "SSL → TUN: 读取错误: " << ec.message() << std::endl;
                break;
            }
        }
        
        if (bytes_read > 0) {
            std::cout << "SSL → TUN: 接收到 " << bytes_read << " 字节数据" << std::endl;
            
            // 打印数据包前几个字节
            std::cout << "数据包内容（前16字节）: ";
            for (size_t i = 0; i < std::min(bytes_read, static_cast<size_t>(16)); ++i) {
                printf("%02X ", (unsigned char)buf[i]);
            }
            std::cout << std::endl;
            
            // 写入TUN设备
            BYTE* tun_packet = WintunAllocateSendPacket(tun_session, bytes_read);
            if (!tun_packet) {
                std::cerr << "SSL → TUN: Failed to allocate TUN packet. Error: " << GetLastError() << std::endl;
                continue;
            }
            
            memcpy(tun_packet, buf, bytes_read);
            WintunSendPacket(tun_session, tun_packet);
            
            std::cout << "SSL → TUN: 成功写入 " << bytes_read << " 字节到TUN设备" << std::endl;
        }
    }
    
    have_quit = true;
    io_svc.stop();
    std::cout << "SSL → TUN 线程退出" << std::endl;
}

bool InitSSL(ssl::context& ctx) {
    try {
        ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::no_sslv3 | ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1);
        ctx.load_verify_file(CA_CERT_PATH);
        ctx.use_certificate_file(CLIENT_CERT_PATH, ssl::context::pem);
        ctx.use_private_key_file(CLIENT_KEY_PATH, ssl::context::pem);
        ctx.set_verify_mode(ssl::verify_peer);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[SSL] Initialization failed: " << e.what() << std::endl;
        return false;
    }
}

// VPN客户端主逻辑（修复版）
void vpn_client(const std::string& server_ip, int port) {
    try {
        std::cout << "=== VPN客户端启动 ===" << std::endl;
        
        // 初始化SSL上下文
        ssl::context ssl_ctx(ssl::context::tls_client);
        if (!InitSSL(ssl_ctx)) {
            return;
        }
        
        // 连接到服务器
        ip::tcp::socket socket(io_svc);
        ip::tcp::resolver resolver(io_svc);
        auto endpoints = resolver.resolve(server_ip, std::to_string(port));
        std::cout << "正在连接到 " << server_ip << ":" << port << "..." << std::endl;
        connect(socket, endpoints);
        
        // 建立SSL连接
        ssl::stream<ip::tcp::socket> ssl_stream(io_svc, ssl_ctx);
        ssl_stream.lowest_layer() = std::move(socket);
        
        boost::system::error_code ec;
        ssl_stream.handshake(ssl::stream_base::client, ec);
        if (ec) {
            std::cerr << "SSL握手失败: " << ec.message() << std::endl;
            return;
        }
        std::cout << "SSL连接建立成功: " << server_ip << ":" << port << std::endl;

        // 获取SSL handle并验证
        SSL* raw_ssl = ssl_stream.native_handle();
        if (!raw_ssl) {
            std::cerr << "Failed to get SSL native handle" << std::endl;
            return;
        }
        
        if (SSL_get_state(raw_ssl) != TLS_ST_OK) {
            std::cerr << "SSL connection is not in OK state" << std::endl;
            return;
        }
        
        std::cout << "SSL状态验证通过，启动数据转发线程..." << std::endl;

        // 启动双向数据转发线程
        boost::thread tun_thread(boost::bind(&tun_to_ssl_thread, raw_ssl));
        boost::thread ssl_thread(boost::bind(&ssl_to_tun_thread, boost::ref(ssl_stream)));
        
        // 信号处理
        signals.async_wait([&](const error_code&, int) {
            std::cout << "接收到退出信号，正在关闭连接..." << std::endl;
            have_quit = true;
            io_svc.stop();
        });
        
        // 运行IO服务
        io_svc.run();
        
        // 等待线程结束
        tun_thread.join();
        ssl_thread.join();

        ssl_stream.shutdown();
        std::cout << "VPN客户端已退出" << std::endl;
    } 
    catch (const std::exception& e) {
        std::cerr << "VPN客户端异常: " << e.what() << std::endl;
        have_quit = true;
        io_svc.stop();
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "用法: " << argv[0] << " <server_ip> <port>" << std::endl;
        return 1;
    }

    // 初始化Winsock和OpenSSL
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "WSAStartup failed. Error: " << WSAGetLastError() << std::endl;
        return 1;
    }
    OpenSSL_add_ssl_algorithms();
    SSL_load_error_strings();

    // 初始化信号处理
    signals.async_wait([&](const error_code&, int) {
        have_quit.store(true);
        io_svc.stop();
    });

    // 创建线程池
    for (int i = 0; i < 2; ++i) {
        thread_pool.create_thread(boost::bind(&io_context::run, &io_svc));
    }

    // 初始化Wintun
    if (!InitializeWintun()) {
        WSACleanup();
        return 1;
    }
    if (!init_wintun_adapter("10.8.0.2", 24)) {
        CleanupWintun();
        WSACleanup();
        return 1;
    }

    // 启动VPN客户端
    vpn_client(argv[1], std::stoi(argv[2]));

    // 清理资源
    CleanupWintun();
    thread_pool.join_all();
    io_svc.stop();
    WSACleanup();
    EVP_cleanup();

    return 0;
}