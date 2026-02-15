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

#include "client.h"
#include <stdio.h>
#include <stdlib.h>


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
#include <uuids.h>
#include <sstream>
#include <random>
#include <openssl/pem.h>
#include <nlohmann/json.hpp>
#include <fstream>

// libs needed: -lws2_32 -liphlpapi -lole32 -lssl -lcrypto -lboost_thread-mt

// #pragma comment(lib, "ws2_32.lib")
// #pragma comment(lib, "iphlpapi.lib")
// #pragma comment(lib, "libssl.lib")
// #pragma comment(lib, "libcrypto.lib")
// #pragma comment(lib, "ole32.lib")

using namespace boost::asio;
using namespace boost::system;

using json = nlohmann::json;

#define TUN_DEVICE_NAME L"VPNClientTunnel"
#define TUN_POOL_NAME L"VPNClientPool"
#define MAX_BUF_SIZE 65536    // 缓冲区大小（大于MTU=1500）

// #define VIRTUAL_SUBNET "10.8.0."
// #define START_IP_SUFFIX 2
// #define MAX_IP_SUFFIX 254
// static int ip_counter = START_IP_SUFFIX;

/*How to use Wintun(from official readme file):
1. Include the wintun.h file and copy the wintun.dll into the same directory with the program
2. Declare Wintun function pointers, set correspondence with alias predefined in Wintun SDK
3. Load wintun.dll with `LoadLibraryEx()`, get a handle(base address of mirrored wintun in memory) as return
4. Use `GetProcAddress()`(need handle and function name as parameter) to assign function pointers with corresponding procedure address in wintun.dll
*/

// Declare Wintun function pointers
static WINTUN_CREATE_ADAPTER_FUNC* WintunCreateAdapter;
static WINTUN_CLOSE_ADAPTER_FUNC* WintunCloseAdapter;
static WINTUN_OPEN_ADAPTER_FUNC* WintunOpenAdapter;
static WINTUN_GET_ADAPTER_LUID_FUNC* WintunGetAdapterLUID;
static WINTUN_GET_RUNNING_DRIVER_VERSION_FUNC* WintunGetRunningDriverVersion;
static WINTUN_DELETE_DRIVER_FUNC* WintunDeleteDriver;
static WINTUN_SET_LOGGER_FUNC* WintunSetLogger;
static WINTUN_START_SESSION_FUNC* WintunStartSession;
static WINTUN_END_SESSION_FUNC* WintunEndSession;
static WINTUN_GET_READ_WAIT_EVENT_FUNC* WintunGetReadWaitEvent;
static WINTUN_RECEIVE_PACKET_FUNC* WintunReceivePacket;
static WINTUN_RELEASE_RECEIVE_PACKET_FUNC* WintunReleaseReceivePacket;
static WINTUN_ALLOCATE_SEND_PACKET_FUNC* WintunAllocateSendPacket;
static WINTUN_SEND_PACKET_FUNC* WintunSendPacket;

static HMODULE wintun_module = NULL;
static WINTUN_ADAPTER_HANDLE tun_adapter = NULL;    // create and handle virtual NIC
static WINTUN_SESSION_HANDLE tun_session = NULL;    // create and handle data transmit on virtual NIC
static NET_LUID tun_luid;                           // Locally Unique Identifier for local network interface
static std::atomic_bool have_quit(false);
static io_context io_svc;                                                                   // handle I/O events
static executor_work_guard<io_context::executor_type> work_guard{io_svc.get_executor()};    // manually add undone count to keep io_context running even when no async operation undone
static boost::thread_group thread_pool;
static signal_set signals(io_svc, SIGINT, SIGTERM);

/*
 ****Read Certificates from cert files****
 */
/*
 ****Fixed Path****
 */
// const std::string CA_CERT_PATH = "C:/tasks/task1/client/certs/server.crt";
// const std::string CLIENT_CERT_PATH = "C:/tasks/task1/client/certs/client.crt";
// const std::string CLIENT_KEY_PATH = "C:/tasks/task1/client/certs/client.key";

/*
 ****Environment Path****
 */
// const std::string CA_CERT_PATH = [](){
//     const char* path = std::getenv("CA_CERT_PATH");
//     return path ? std::string(path) : "";
// }();
// const std::string CLIENT_CERT_PATH = [](){
//     const char* path = std::getenv("CLIENT_CERT_PATH");
//     return path ? std::string(path) : "";
// }();
// const std::string CLIENT_KEY_PATH = [](){
//     const char* path = std::getenv("CLIENT_KEY_PATH");
//     return path ? std::string(path) : "";
// }();

/*
 ****Read Certificates from only one config file's content****
 */
/*
 ****Fixed path****
 */
const std::string CONFIG_PATH = "config.json";
/*
 ****Environment Path****
 */
// const std::string CONFIG_PATH = [](){
//     const char* path = std::getenv("CONFIG_PATH");
//     return path ? std::string(path) : "";
// }();

// load Wintun handle to assign function pointers
bool InitializeWintun()
{
    wintun_module = LoadLibraryExW(L"wintun.dll", NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!wintun_module)
    {
        std::cerr << "Failed to load wintun.dll. Error: " << GetLastError() << std::endl;
        return false;
    }
#define X(Name) ((*(FARPROC*)& Name = GetProcAddress(wintun_module, #Name)) == NULL)
    if (X(WintunCreateAdapter) || X(WintunCloseAdapter) || X(WintunOpenAdapter) || X(WintunGetAdapterLUID) || X(WintunGetRunningDriverVersion) || X(WintunDeleteDriver) || X(WintunSetLogger) ||
        X(WintunStartSession) || X(WintunEndSession) || X(WintunGetReadWaitEvent) || X(WintunReceivePacket) || X(WintunReleaseReceivePacket) || X(WintunAllocateSendPacket) || X(WintunSendPacket))
    {
        DWORD LastError = GetLastError();
        FreeLibrary(wintun_module);
        SetLastError(LastError);
        return false;
    }
#undef X
    return true;
}

void CleanupWintun()
{
    if (tun_session)
    {
        WintunEndSession(tun_session);
        tun_session = NULL;
    }
    if (tun_adapter)
    {
        WintunCloseAdapter(tun_adapter);
        tun_adapter = NULL;
    }
    if (wintun_module)
    {
        FreeLibrary(wintun_module);
        wintun_module = NULL;
    }
}

std::wstring generate_unique_tun_name()
{
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(1000, 9999);
    int rand_num = dist(gen);

    std::wstringstream wss;
    wss << TUN_DEVICE_NAME << L"_" << ms << L"_" << rand_num;
    return wss.str();
}

// load adapter handle and create adapter
bool init_wintun_adapter(const char* ip, int prefix, const char* target_ip)
{
    GUID guid;    // Global Unique ID
    CoCreateGuid(&guid);
    auto tun_name = generate_unique_tun_name();
    tun_adapter = WintunCreateAdapter(TUN_POOL_NAME, tun_name.c_str(), &guid);
    if (!tun_adapter)
    {
        tun_adapter = WintunOpenAdapter(tun_name.c_str());
        if (!tun_adapter)
        {
            std::cerr << "Failed to create/open WinTUN adapter. Error: " << GetLastError() << std::endl;
            return false;
        }
    }
    // assign LUID
    WintunGetAdapterLUID(tun_adapter, &tun_luid);
    if (tun_luid.Value == 0)
    {
        std::cerr << "Error: TUN adapter LUID is invalid (Value=0)." << std::endl;
        WintunCloseAdapter(tun_adapter);
        tun_adapter = NULL;
        return false;
    }
    std::cout << "TUN adapter LUID: 0x" << std::hex << tun_luid.Value << std::dec << std::endl;
    // create session handle
    tun_session = WintunStartSession(tun_adapter, 0x400000);
    if (!tun_session)
    {
        std::cerr << "Failed to start WinTUN session. Error: " << GetLastError() << std::endl;
        return false;
    }
    // assign IP for tun device
    MIB_UNICASTIPADDRESS_ROW ipRow;
    InitializeUnicastIpAddressEntry(&ipRow);
    ipRow.InterfaceLuid = tun_luid;             // bind tun's luid
    ipRow.Address.Ipv4.sin_family = AF_INET;    // declare Ipv4
    inet_pton(AF_INET, ip, &ipRow.Address.Ipv4.sin_addr);
    ipRow.OnLinkPrefixLength = prefix;
    ipRow.DadState = IpDadStatePreferred;    // duplicate address detection, set Preferred for virtual NIC
    DWORD result = CreateUnicastIpAddressEntry(&ipRow);
    if (result != ERROR_SUCCESS && result != ERROR_OBJECT_ALREADY_EXISTS)
    {
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
    if (route_result != ERROR_SUCCESS && route_result != ERROR_OBJECT_ALREADY_EXISTS)
    {
        std::cerr << "Failed to add route for " << target_ip << " Error: " << route_result << std::endl;
        return false;
    }
    FlushIpPathTable(AF_INET);
    std::cout << "Route added for " << target_ip << " -> " << ip << "(via TUN)" << std::endl;
    return true;
}

void print_hex_ascii(const void* data, size_t size)
{
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; i += 16)
    {    // 每行16字节
        // 打印地址偏移（可选）
        std::cout << std::setw(4) << std::setfill('0') << std::hex << i << "  ";
        // 打印十六进制部分
        for (size_t j = 0; j < 16; ++j)
        {
            if (i + j < size)
            {
                std::cout << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i + j]) << " ";
            }
            else
            {
                std::cout << "   ";    // 不足16字节时补空格
            }
            if (j == 7)
                std::cout << " ";    // 第8字节后多空一格，对齐
        }
        // 打印ASCII部分
        std::cout << " | ";
        for (size_t j = 0; j < 16 && i + j < size; ++j)
        {
            unsigned char c = bytes[i + j];
            std::cout << (isprint(c) ? static_cast<char>(c) : '.');
        }
        std::cout << std::dec << std::endl;
    }
}

bool InitSSL(ssl::context& ctx, const char* config_path)
{
    try
    {
        ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::no_sslv3 | ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1);
        /*
            Read from cert files
        */
        // ctx.load_verify_file(CA_CERT_PATH);
        // ctx.use_certificate_file(CLIENT_CERT_PATH, ssl::context::pem);
        // ctx.use_private_key_file(CLIENT_KEY_PATH, ssl::context::pem);
        // ctx.set_verify_mode(ssl::verify_peer);
        // std::cout << "[SSL] Initialized with CA cert: " << CA_CERT_PATH << std::endl;
        // std::cout << "[SSL] Initialized with client cert: " << CLIENT_CERT_PATH << std::endl;
        // std::cout << "[SSL] Initialized with client key: " << CLIENT_KEY_PATH << std::endl;
        // return true;

        /*
            Read from config.json
        */
        std::ifstream config_file(config_path);
        if (!config_file)
        {
            throw std::runtime_error("config.json not found");
        }
        json config;
        config_file >> config;

        const std::string& ca_cert = config["certs"]["server_crt"];
        const std::string& client_cert = config["certs"]["client_crt"];
        const std::string& client_key = config["certs"]["client_key"];

        BIO* bio_ca = BIO_new_mem_buf(ca_cert.data(), ca_cert.size());
        X509_STORE* store = SSL_CTX_get_cert_store(ctx.native_handle());
        X509* x509 = PEM_read_bio_X509(bio_ca, NULL, NULL, NULL);
        if (!x509 || !X509_STORE_add_cert(store, x509))
        {
            BIO_free(bio_ca);
            X509_free(x509);
            throw std::runtime_error("Failed to load CA cert");
        }

        BIO* bio_crt = BIO_new_mem_buf(client_cert.data(), client_cert.size());
        BIO* bio_key = BIO_new_mem_buf(client_key.data(), client_key.size());
        X509* cert = PEM_read_bio_X509(bio_crt, NULL, NULL, NULL);
        EVP_PKEY* key = PEM_read_bio_PrivateKey(bio_key, NULL, NULL, NULL);

        if (!cert || !key || !SSL_CTX_use_certificate(ctx.native_handle(), cert) || !SSL_CTX_use_PrivateKey(ctx.native_handle(), key))
        {
            throw std::runtime_error("Failed to load client cert/key");
        }

        BIO_free(bio_ca);
        BIO_free(bio_crt);
        BIO_free(bio_key);
        X509_free(x509);
        X509_free(cert);
        EVP_PKEY_free(key);

        ctx.set_verify_mode(ssl::verify_peer);
        std::cout << "[SSL] Certificates loaded from config.json\n";
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[SSL] Initialization failed: " << e.what() << std::endl;
        return false;
    }
}

// Get Packet through TUN and send to Server
void send(ssl::stream<ip::tcp::socket>& ssl_stream)
{
    
    std::cout << "=== TUN to SSL thread started... ===" << std::endl;

    while (!have_quit)
    {
        // read packet from TUN
        DWORD packet_size;
        BYTE* packet = WintunReceivePacket(tun_session, &packet_size);
        if (!packet)
        {
            DWORD err = GetLastError();
            if (err == ERROR_NO_MORE_ITEMS)
            {
                Sleep(10);
                continue;
            }
            std::cerr << "TUN 读取失败，错误码: " << err << std::endl;
            break;
        }
        
        std::cout << "TUN received a packet, size: " << packet_size << " bytes, first 16 bytes: ";
        for (DWORD i = 0; i < 16 && i < packet_size; ++i)
        {
            printf("%02X ", packet[i]);
        }
        std::cout << std::endl;

        // send to server via SSL
        try
        {
            size_t bytes_written = write(ssl_stream, buffer(packet, packet_size));
            
            std::cout << "TUN -> SSL: Sent " << bytes_written << " bytes" << std::endl;

            if (bytes_written > 0 && bytes_written <= 128)
            {
                std::cout << "            Content: " << std::endl;
                print_hex_ascii(packet, bytes_written);
            }
        }
        catch (const boost::system::system_error& e)
        {
            if (e.code() == boost::asio::error::eof || e.code() == boost::asio::error::connection_reset)
            {
                std::cerr << "SSL connection closed" << std::endl;
            }
            else if (e.code() == boost::asio::ssl::error::stream_truncated)
            {
                std::cerr << "SSL stream truncated" << std::endl;
            }
            else
            {
                std::cerr << "Asio write error: " << e.what() << std::endl;
            }
            have_quit = true;
        }

        WintunReleaseReceivePacket(tun_session, packet);
    }
    
    std::cout << "=== TUN to SSL thread exiting... ===" << std::endl;
}

// Read from Server and send out through TUN
void receive(ssl::stream<ip::tcp::socket>& socket)
{
    
    std::cout << "=== SSL to TUN thread started... ===" << std::endl;
    char buf[MAX_BUF_SIZE];

    while (!have_quit)
    {
        boost::system::error_code ec;
        size_t bytes_read = 0;
        bool read_completed = false;
        socket.async_read_some(
            boost::asio::buffer(buf, MAX_BUF_SIZE),
            [&](const boost::system::error_code& error, size_t bytes) {
                ec = error;
                bytes_read = bytes;
                read_completed = true;
            }
        );
        while (!read_completed && !have_quit) {
            io_svc.run_one_for(std::chrono::milliseconds(10)); // 每次最多阻塞10ms
        }
        if (ec)
        {
            if (ec == ssl::error::stream_truncated || ec == boost::asio::error::eof)
            {
                std::cerr << "SSL -> TUN: Connection closed" << std::endl;
                break;
            }
            else if (ec == boost::asio::error::would_block)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            else
            {
                std::cerr << "SSL -> TUN: Read Error: " << ec.message();
                break;
            }
        }
        if (bytes_read > 0)
        {
            
            std::cout << "SSL -> TUN: Received " << bytes_read << " bytes" << std::endl;

            if (bytes_read > 0 && bytes_read <= 128)
            {
                
                std::cout << "            Content(first 16 bytes): ";
                for (size_t i = 0; i < std::min(bytes_read, static_cast<size_t>(16)); i++)
                {
                    printf("%02X", (unsigned char)buf[i]);
                }
                
                std::cout << std::endl;
            }

            BYTE* tun_packet = WintunAllocateSendPacket(tun_session, bytes_read);
            if (!tun_packet)
            {
                std::cerr << "SSL -> TUN: Failed to allocate TUN packet. Error: " << GetLastError() << std::endl;
                continue;
            }

            memcpy(tun_packet, buf, bytes_read);
            WintunSendPacket(tun_session, tun_packet);

            std::cout << "SSL -> TUN: Received and sent " << bytes_read << " bytes" << std::endl;
        }
    }

    std::cout << "=== SSL to TUN thread exiting... ===" << std::endl;
}

// Main logic of client program
void vpn_client(const std::string& server_ip, int port, const std::string& tun_ip, const char* config_path)
{
    try
    {
        std::cout << "=== VPN Client Starting... ===" << std::endl;
        // initialize SSL context
        ssl::context ssl_ctx(ssl::context::tls_client);
        if (!InitSSL(ssl_ctx, config_path))
        {
            return;
        }

        // connect to server
        ip::tcp::socket socket(io_svc);
        ip::tcp::resolver resolver(io_svc);
        auto endpoints = resolver.resolve(server_ip, std::to_string(port));    // resolve server address
        std::cout << "Connecting to " << server_ip << ":" << port << "..." << std::endl;
        connect(socket, endpoints);

        // SSL connection
        ssl::stream<ip::tcp::socket> ssl_stream(io_svc, ssl_ctx);
        ssl_stream.lowest_layer() = std::move(socket);

        // SSL handshake and error check
        boost::system::error_code ec;
        ssl_stream.handshake(ssl::stream_base::client, ec);
        if (ec)
        {
            std::cerr << "SSL handshake failed: " << ec.message() << std::endl;
            return;
        }
        std::cout << "SSL connection established with " << server_ip << ":" << port << std::endl;

        // send tun ip to server
        std::cout << "Sending TUN ip " << tun_ip << " to server" << std::endl;
        boost::asio::write(ssl_stream, boost::asio::buffer(tun_ip + "\n"), ec);
        if (ec)
        {
            std::cerr << "Failed to send TUN ip: " << ec.message() << std::endl;
            return;
        }

        // start data forwarding threads
        boost::asio::thread_pool pool(2);
        boost::asio::post(pool, boost::bind(&send, boost::ref(ssl_stream)));
        boost::asio::post(pool, boost::bind(&receive, boost::ref(ssl_stream)));

        // start io service
        io_svc.run();

        // wait for thread to finish
        pool.join();

        // shut down SSL connection
        boost::system::error_code shut_ec;
        ssl_stream.shutdown(shut_ec);
        if (shut_ec && shut_ec != boost::asio::error::eof && shut_ec != boost::asio::ssl::error::stream_truncated) {
            std::cerr << "[WARN] SSL shutdown error: " << shut_ec.message() << std::endl;
        }

        std::cout << "=== VPN client exiting... ===" << std::endl;
        return;
    }
    catch (const std::exception& e)
    {
        std::cerr << "VPN client exception: " << e.what() << std::endl;
        have_quit = true;
        io_svc.stop();
    }
}

int start_vpn_client(const char* config_path, const char* route_ip)
{
    // Read configuration info from config file
    if (!config_path || !route_ip)
    {
        std::cerr << "参数错误：config_path 或 route_ip 为空" << std::endl;
        return 0;
    }
    std::ifstream config_file(config_path);
    if (!config_file)
    {
        std::cerr << "无法打开config.json" << std::endl;
        return -1;
    }
    json config;
    config_file >> config;
    const std::string server_ip = config["server"]["ip"];
    const int port = config["server"]["port"];
    const std::string tun_ip = config["tun"]["ip"];

    // Initialize Winsock and OpenSSL
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        std::cerr << "WSAStartup failed. Error: " << WSAGetLastError() << std::endl;
        return 1;
    }
    OpenSSL_add_ssl_algorithms();
    SSL_load_error_strings();

    signals.async_wait(
        [&](const error_code&, int)
        {
            have_quit = true;
            io_svc.stop();
        });

    // Initialize Wintun and create tun adapter
    if (!InitializeWintun())
    {
        WSACleanup();
        return 1;
    }
    if (!init_wintun_adapter(tun_ip.c_str(), 24, route_ip))
    {
        CleanupWintun();
        WSACleanup();
        return 1;
    }

    // start vpn client
    vpn_client(server_ip, port, tun_ip, config_path);
    // clean up
    CleanupWintun();
    ::thread_pool.join_all();
    // io_svc.stop();
    WSACleanup();
    EVP_cleanup();

    return 0;
}

int main(int argc, char* argv[]) {
    /*
        Read from cert files and manually set connection options
    */
    // if (argc != 5) {
    //     std::cerr << "Usage: " << argv[0] << " <server_ip> <port> <ip to assign for tun device> <ip you would like to route to>" << std::endl;
    //     return 1;
    // }

    /*
        Read everything needed(except route ip which requires user to choose) from config.json
    */
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <ip you would like to route to>" << std::endl;
        return 1;
    }

    const char* path = CONFIG_PATH.data();

    return start_vpn_client(path, argv[1]);

}