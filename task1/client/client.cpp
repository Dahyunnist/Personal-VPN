/*compile command in cmd:
g++ client.cpp -o client.exe -lws2_32 -liphlpapi -lole32 -lssl -lcrypto -lboost_thread-mt
*/

/*
lws2_32 refers to libws2_32.a in lib directory
*/

/*start program
client.exe 172.20.68.63 10043
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
#include <chrono>
#include <thread>
#include <iomanip>
#include "wintun.h"
#include <openssl/ssl.h>
#include <openssl/bio.h>

// libs needed: -lws2_32 -liphlpapi -lole32 -lssl -lcrypto -lboost_thread-mt
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")


using namespace boost::asio;
using namespace boost::system;


#define TUN_DEVICE_NAME L"VPNTunnel"
#define TUN_POOL_NAME L"VPNPool"
#define MAX_BUF_SIZE 65536  // 缓冲区大小（大于MTU=1500）

/*How to use Wintun(from official readme file): 
1. Include the wintun.h file and copy the wintun.dll into the same directory with the program
2. Declare Wintun function pointers, set correspondence with alias predefined in Wintun SDK
3. Load wintun.dll with `LoadLibraryEx()`, get a handle(base address of mirrored wintun in memory) as return
4. Use `GetProcAddress()`(need handle and function name as parameter) to assign function pointers with corresponding procedure address in wintun.dll
*/


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
static WINTUN_ADAPTER_HANDLE tun_adapter = NULL; //create and handle virtual NIC
static WINTUN_SESSION_HANDLE tun_session = NULL; //create and handle data transmit on virtual NIC
static NET_LUID tun_luid;  // Locally Unique Identifier for local network interface
static std::atomic_bool have_quit(false);
static io_context io_svc; //handle I/O events
static executor_work_guard<io_context::executor_type> work_guard{io_svc.get_executor()}; //manually add undone count to keep io_context running even when no async operation undone
static boost::thread_group thread_pool;
static signal_set signals(io_svc, SIGINT, SIGTERM);

const std::string CA_CERT_PATH = "certs/server.crt";
const std::string CLIENT_CERT_PATH = "certs/client.crt";
const std::string CLIENT_KEY_PATH = "certs/client.key";


// load Wintun handle to assign fnuction pointers
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


// load adapter handle and create adapter
bool init_wintun_adapter(const char* ip, int prefix, char* target_ip) {
    GUID guid; //Global Unique ID
    CoCreateGuid(&guid);
    tun_adapter = WintunCreateAdapter(TUN_POOL_NAME, TUN_DEVICE_NAME, &guid);
    if (!tun_adapter) {
        tun_adapter = WintunOpenAdapter(TUN_DEVICE_NAME);
        if (!tun_adapter) {
            std::cerr << "Failed to create/open WinTUN adapter. Error: " << GetLastError() << std::endl;
            return false;
        }
    }
    // assign LUID
    WintunGetAdapterLUID(tun_adapter, &tun_luid);
    if (tun_luid.Value == 0) {
        std::cerr << "Error: TUN adapter LUID is invalid (Value=0)." << std::endl;
        WintunCloseAdapter(tun_adapter);
        tun_adapter = NULL;
        return false;
    }
    std::cout << "TUN adapter LUID: 0x" << std::hex << tun_luid.Value << std::dec << std::endl;
    // create session handle
    tun_session = WintunStartSession(tun_adapter, 0x400000);
    if (!tun_session) {
        std::cerr << "Failed to start WinTUN session. Error: " << GetLastError() << std::endl;
        return false;
    }
    // assign IP for tun device
    MIB_UNICASTIPADDRESS_ROW ipRow;
    InitializeUnicastIpAddressEntry(&ipRow);
    ipRow.InterfaceLuid = tun_luid; //bind tun's luid
    ipRow.Address.Ipv4.sin_family = AF_INET; //declare Ipv4
    inet_pton(AF_INET, ip, &ipRow.Address.Ipv4.sin_addr);
    ipRow.OnLinkPrefixLength = prefix; 
    ipRow.DadState = IpDadStatePreferred; //duplicate address detection, set Preferred for virtual NIC
    DWORD result = CreateUnicastIpAddressEntry(&ipRow);
    if (result != ERROR_SUCCESS && result != ERROR_OBJECT_ALREADY_EXISTS) {
        std::cerr << "Failed to set IP address. Error: " << result << std::endl;
        return false;
    }
    // add route
    MIB_IPFORWARD_ROW2 route;
    InitializeIpForwardEntry(&route);
    route.InterfaceLuid = tun_luid;
    route.DestinationPrefix.Prefix.si_family = AF_INET;
    inet_pton(AF_INET, target_ip, &route.DestinationPrefix.Prefix.Ipv4.sin_addr);
    route.DestinationPrefix.PrefixLength = 32;
    inet_pton(AF_INET, "10.8.0.1", &route.NextHop.Ipv4.sin_addr);
    route.Metric = 1;
    route.Protocol = static_cast<NL_ROUTE_PROTOCOL>(3);
    DWORD route_result = CreateIpForwardEntry2(&route);
    if(route_result != ERROR_SUCCESS && route_result != ERROR_OBJECT_ALREADY_EXISTS){
        std::cerr << "Failed to add route for " << target_ip << " Error: " << route_result << std::endl;
        return false;
    }
    FlushIpPathTable(AF_INET);
    std::cout << "Route added for " << target_ip << " -> " << ip << "(via TUN)" << std::endl;
    return true;
}



void print_hex_ascii(const void* data, size_t size) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; i += 16) { // 每行16字节
        // 打印地址偏移（可选）
        std::cout << std::setw(4) << std::setfill('0') << std::hex << i << "  ";
        // 打印十六进制部分
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < size) {
                std::cout << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i + j]) << " ";
            } else {
                std::cout << "   "; // 不足16字节时补空格
            }
            if (j == 7) std::cout << " "; // 第8字节后多空一格，对齐
        }
        // 打印ASCII部分
        std::cout << " | ";
        for (size_t j = 0; j < 16 && i + j < size; ++j) {
            unsigned char c = bytes[i + j];
            // std::cout << (isprint(c) ? static_cast<char>(c) : '.');
        }
        std::cout << std::dec << std::endl;
    }
}



void tun_to_tls_thread(ssl::stream<ip::tcp::socket>& ssl_stream) {
    std::cout << "=== TUN to SSL thread started... ===" << std::endl;
    
    while (!have_quit) {
        // read packet from TUN
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
        std::cout << "TUN received a packet, size: " << packet_size << " bytes, first 16 bytes: ";
        for (DWORD i = 0; i < 16 && i < packet_size; ++i) {
            printf("%02X ", packet[i]); 
        }
        std::cout << std::endl;

        // send to server via SSL
        try {
            size_t bytes_written = write(ssl_stream, buffer(packet, packet_size));
            std::cout << "TUN -> SSL: Sent " << bytes_written << " bytes" << std::endl;
            
            if (bytes_written > 0 && bytes_written <= 128) {
                std::cout << "            Content: " << std::endl;
                print_hex_ascii(packet, bytes_written);
            }
        } 
        catch (const boost::system::system_error& e) {
            if (e.code() == boost::asio::error::eof || e.code() == boost::asio::error::connection_reset) {
                std::cerr << "SSL connection closed" << std::endl;
            } 
            else if (e.code() == boost::asio::ssl::error::stream_truncated) {
                std::cerr << "SSL stream truncated" << std::endl;
            }
            else {
                std::cerr << "Asio write error: " << e.what() << std::endl;
            }
            have_quit = true;
        }

        WintunReleaseReceivePacket(tun_session, packet); 
    }
    std::cout << "=== TUN to SSL thread exiting... ===" << std::endl;
}



void tls_to_tun_thread(ssl::stream<ip::tcp::socket>& socket) {
    std::cout << "=== SSL to TUN thread started... ===" << std::endl;
    char buf[MAX_BUF_SIZE];  

    while (!have_quit) {
        boost::system::error_code ec;
        size_t bytes_read = socket.read_some(boost::asio::buffer(buf, MAX_BUF_SIZE), ec);
        if(ec){
            if(ec == ssl::error::stream_truncated || ec == boost::asio::error::eof){
                std::cerr << "SSL -> TUN: Connection closed" << std::endl;
                break;
            }
            else if(ec == boost::asio::error::would_block){
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            else{
                std::cerr << "SSL -> TUN: Read Error: " << ec.message();
                break;
            }
        }

        if(bytes_read > 0){
            std::cout << "SSL -> TUN: Received " << bytes_read << " bytes" << std::endl;
            
            if (bytes_read > 0 && bytes_read <= 128) {
                std::cout << "            Content(first 16 bytes): ";
                for(size_t i = 0; i < std::min(bytes_read, static_cast<size_t>(16)); i++){
                    printf("%02X", (unsigned char)buf[i]);
                }
                std::cout << std::endl;
            }

            BYTE* tun_packet = WintunAllocateSendPacket(tun_session, bytes_read);
            if(!tun_packet){
                std::cerr << "SSL -> TUN: Failed to allocate TUN packet. Error: " << GetLastError() << std::endl;
                continue;
            }

            memcpy(tun_packet, buf, bytes_read);
            WintunSendPacket(tun_session, tun_packet);

            std::cout << "SSL -> TUN: Received and sent " << bytes_read << " bytes" << std::endl;
        }        
    }
    have_quit = true;
    io_svc.stop();
    std::cout << "=== SSL to TUN thread exiting... ===" << std::endl;
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


// Main logic of client program
void vpn_client(const std::string& server_ip, int port) {
    try {
        std::cout << "=== VPN Client Starting... ===" << std::endl;
        // initialize SSL context
        ssl::context ssl_ctx(ssl::context::tls_client);
        if (!InitSSL(ssl_ctx)){
            return;
        }
        // connect to server
        ip::tcp::socket socket(io_svc);
        ip::tcp::resolver resolver(io_svc);
        auto endpoints = resolver.resolve(server_ip, std::to_string(port)); //resolve server address
        std::cout << "Connecting to " << server_ip << ":" << port << "..." << std::endl;
        connect(socket, endpoints);
        // SSL connection
        ssl::stream<ip::tcp::socket> ssl_stream(io_svc, ssl_ctx);
        ssl_stream.lowest_layer() = std::move(socket);
        // SSL handshake and error check
        boost::system::error_code ec;
        ssl_stream.handshake(ssl::stream_base::client, ec);
        if(ec){
            std::cerr << "SSL handshake failed: " << ec.message() << std::endl;
            return;
        }
        std::cout << "SSL connection established with " << server_ip << ":" << port << std::endl;

        // start data forwarding threads
        boost::thread send_thread(boost::bind(&tun_to_tls_thread, boost::ref(ssl_stream)));
        boost::thread receive_thread(boost::bind(&tls_to_tun_thread, boost::ref(ssl_stream)));
        
        // 处理信号（Ctrl+C退出）
        signals.async_wait([&](const error_code&, int) {
            have_quit = true;
            io_svc.stop();
        });
        
        // start io service
        io_svc.run();
        
        // 等待线程结束
        send_thread.join();
        receive_thread.join();

        // 关闭SSL连接
        ssl_stream.shutdown();
        std::cout << "=== VPN client exiting... ===" << std::endl;
    } 
    catch (const std::exception& e) {
        std::cerr << "VPN client exception: " << e.what() << std::endl;
        have_quit = true;
        io_svc.stop();
    }
}


int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <server_ip> <port> <ip you would like to route to>" << std::endl;
        return 1;
    }

    // Initialize Winsock and OpenSSL
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

    // create two threads for tun_to_ssl and ssl_to_tun
    for (int i = 0; i < 2; ++i) {
        ::thread_pool.create_thread(boost::bind(&io_context::run, &io_svc));
    }
    // Initialize Wintun and create tun adapter
    if (!InitializeWintun()) {
        WSACleanup();
        return 1;
    }
    if (!init_wintun_adapter("10.8.0.2", 24, argv[3])) {
        CleanupWintun();
        WSACleanup();
        return 1;
    }
    // start vpn client
    vpn_client(argv[1], std::stoi(argv[2]));
    // clean up
    CleanupWintun();
    ::thread_pool.join_all();
    io_svc.stop();
    WSACleanup();
    EVP_cleanup();

    return 0;
}