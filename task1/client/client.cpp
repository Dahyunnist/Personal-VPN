/*compile command in cmd:
g++ client.cpp -o client.exe -lws2_32 -liphlpapi -lole32 -lssl -lcrypto -lboost_thread-mt
*/

/*
lws2_32 refers to libws2_32.a in lib directory
*/

/*start program
client.exe 172.19.36.222 443
*/

/*check VPN:
Get-NetAdapter -Name "*VPN*" | Select-Object Name, ifIndex
*/

/*add route
route add 110.242.68.66 mask 255.255.255.255 10.8.0.1 metric 1 if 53
(53 is the ifIndex of VPN)
*/

/*check route add result
route print | findstr "110.242.68.66"
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

// libs needed: -lws2_32 -liphlpapi -lole32 -lssl -lcrypto -lboost_thread-mt
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")

#include "wintun.h"

using namespace boost::asio;
using namespace boost::system;

#define TUN_DEVICE_NAME L"VPNTunnel"
#define TUN_POOL_NAME L"VPNPool"
#define MAX_BUF_SIZE 65536  // 缓冲区大小（大于MTU=1500）

// WinTUN函数指针（保持不变）
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
static NET_LUID tun_luid;  // TUN设备LUID（全局变量）
static std::atomic_bool have_quit(false);
static io_context io_svc;
static executor_work_guard<io_context::executor_type> work_guard{io_svc.get_executor()};
static boost::thread_group thread_pool;
static signal_set signals(io_svc, SIGINT, SIGTERM);

const std::string CA_CERT_PATH = "certs/server.crt";
const std::string CLIENT_CERT_PATH = "certs/client.crt";
const std::string CLIENT_KEY_PATH = "certs/client.key";


// 初始化WinTUN（保持不变）
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


// 清理WinTUN（保持不变）
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


// 初始化TUN设备（保持不变，修正LUID获取）
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

    // 获取TUN设备LUID（关键：必须正确赋值）
    WintunGetAdapterLUID(tun_adapter, &tun_luid);
    if (tun_luid.Value == 0) {
        std::cerr << "Error: TUN adapter LUID is invalid (Value=0)." << std::endl;
        WintunCloseAdapter(tun_adapter);
        tun_adapter = NULL;
        return false;
    }
    std::cout << "TUN adapter LUID: 0x" << std::hex << tun_luid.Value << std::dec << std::endl;

    // 启动TUN会话（缓冲区大小设为4MB，足够容纳大数据包）
    tun_session = WintunStartSession(tun_adapter, 0x400000);
    if (!tun_session) {
        std::cerr << "Failed to start WinTUN session. Error: " << GetLastError() << std::endl;
        return false;
    }

    // 配置TUN设备IP（保持不变）
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
    return true;
}


// 读取完整数据（保持不变，确保读取指定长度）
bool read_full(ssl::stream<ip::tcp::socket>& socket, char* buf, size_t length) {
    size_t total_read = 0;
    while (total_read < length && !have_quit) {
        boost::system::error_code ec;
        size_t n = socket.read_some(buffer(buf + total_read, length - total_read), ec);
        if (ec) {
            if (ec == ssl::error::stream_truncated || ec == error::eof) {
                return false;
            }
            if (ec != error::would_block) {
                std::cerr << "Read Error: " << ec.message() << std::endl;
                return false;
            }
            continue;
        }
        total_read += n;
    }
    return total_read == length;
}


// TUN→SSL：读取TUN原始IP包，直接发送给服务端（移除长度头）
void tun_to_ssl_thread(ssl::stream<ip::tcp::socket>& socket) {
    HANDLE tun_event = WintunGetReadWaitEvent(tun_session);
    if (!tun_event) {
        std::cerr << "Failed to get TUN read event. Error: " << GetLastError() << std::endl;
        have_quit = true;
        return;
    }

    while (!have_quit) {
        // 等待TUN设备有数据
        DWORD wait_result = WaitForSingleObject(tun_event, 100);
        if (have_quit) break;
        if (wait_result != WAIT_OBJECT_0) continue;

        // 读取TUN数据包（原始IP包）
        DWORD packet_size;
        BYTE* packet = WintunReceivePacket(tun_session, &packet_size);
        if (!packet) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) {
                std::cerr << "Failed to receive TUN packet. Error: " << GetLastError() << std::endl;
                break;
            }
            continue;
        }

        // 验证IP包长度（至少20字节，IPv4头部最小长度）
        if (packet_size < 20) {
            std::cerr << "Invalid TUN packet size: " << packet_size << " bytes (discarded)." << std::endl;
            WintunReleaseReceivePacket(tun_session, packet);
            continue;
        }

        try {
            // 直接发送原始IP包（无长度头）
            write(socket, buffer(packet, packet_size));
            std::cout << "TUN -> SSL: Sent " << packet_size << " bytes." << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "TUN -> SSL: Write failed: " << e.what() << std::endl;
            WintunReleaseReceivePacket(tun_session, packet);
            break;
        }

        WintunReleaseReceivePacket(tun_session, packet);
    }

    have_quit = true;
    io_svc.stop();
}


// SSL→TUN：接收服务端原始IP包，解析后写入TUN（通过IP头部总长度确定包边界）
void ssl_to_tun_thread(ssl::stream<ip::tcp::socket>& socket) {
    char buf[MAX_BUF_SIZE];  // 缓冲区足够大（大于MTU=1500）

    while (!have_quit) {
        // 1. 读取IP头部前4字节（获取总长度）
        char ip_header[4];
        if (!read_full(socket, ip_header, 4)) {
            std::cerr << "SSL -> TUN: Failed to read IP header. Error: " << GetLastError() << std::endl;
            break;
        }

        // 2. 提取IP总长度（IP头部第2-3字节，大端序）
        uint16_t total_len = ntohs(*reinterpret_cast<const uint16_t*>(ip_header + 2));
        if (total_len < 20 || total_len > MAX_BUF_SIZE) {
            std::cerr << "total_len = " << total_len << ", MAX_BUF_SIZE = " << MAX_BUF_SIZE;
            std::cerr << "SSL -> TUN: Invalid packet length: " << total_len << " bytes (discarded)." << std::endl;
            break;
        }

        // 3. 读取剩余字节（总长度-4）
        if (!read_full(socket, buf + 4, total_len - 4)) {
            std::cerr << "SSL -> TUN: Failed to read full packet. Error: " << GetLastError() << std::endl;
            break;
        }

        // 4. 拼接完整IP包（前4字节+剩余字节）
        memcpy(buf, ip_header, 4);

        // 5. 写入TUN设备
        BYTE* tun_packet = WintunAllocateSendPacket(tun_session, total_len);
        if (!tun_packet) {
            std::cerr << "SSL -> TUN: Failed to allocate TUN packet. Error: " << GetLastError() << std::endl;
            break;
        }
        memcpy(tun_packet, buf, total_len);
        WintunSendPacket(tun_session, tun_packet);
        std::cout << "SSL -> TUN: Received " << total_len << " bytes (written to TUN)." << std::endl;
    }

    have_quit = true;
    io_svc.stop();
}


// 初始化SSL上下文（保持不变）
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


// VPN客户端主逻辑（保持不变，修正线程启动）
void vpn_client(const std::string& server_ip, int port) {
    try {
        // 初始化SSL上下文
        ssl::context ssl_ctx(ssl::context::tls_client);
        if (!InitSSL(ssl_ctx)) return;

        // 连接服务端
        ip::tcp::socket socket(io_svc);
        ip::tcp::resolver resolver(io_svc);
        auto endpoints = resolver.resolve(server_ip, std::to_string(port));
        connect(socket, endpoints);

        // 建立SSL连接
        ssl::stream<ip::tcp::socket> ssl_stream(io_svc, ssl_ctx);
        ssl_stream.lowest_layer() = std::move(socket);
        ssl_stream.handshake(ssl::stream_base::client);
        std::cout << "SSL connection established with " << server_ip << ":" << port << std::endl;

        // 启动数据转发线程（TUN→SSL和SSL→TUN）
        boost::thread tun_thread(boost::bind(&tun_to_ssl_thread, boost::ref(ssl_stream)));
        boost::thread ssl_thread(boost::bind(&ssl_to_tun_thread, boost::ref(ssl_stream)));

        // 处理信号（Ctrl+C退出）
        signals.async_wait([&](const error_code&, int) {
            have_quit = true;
            io_svc.stop();
        });

        // 运行IO服务
        io_svc.run();

        // 等待线程结束
        tun_thread.join();
        ssl_thread.join();

        // 关闭SSL连接
        ssl_stream.shutdown();
    } catch (const std::exception& e) {
        std::cerr << "VPN client exception: " << e.what() << std::endl;
        have_quit = true;
        io_svc.stop();
    }
}


// 主函数（保持不变，修正线程池创建）
int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <server_ip> <port>" << std::endl;
        return 1;
    }

    // 初始化WinSock和OpenSSL
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

    // 创建线程池（运行IO服务）
    for (int i = 0; i < 2; ++i) {
        ::thread_pool.create_thread(boost::bind(&io_context::run, &io_svc));
    }

    // 初始化WinTUN和TUN设备
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
    ::thread_pool.join_all();
    io_svc.stop();
    WSACleanup();
    EVP_cleanup();

    return 0;
}