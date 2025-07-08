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

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "libssl.lib")
#pragma comment(lib, "libcrypto.lib")

#include "wintun.h"

using namespace boost::asio;
using namespace boost::system;

#define TUN_DEVICE_NAME L"VPNTunnel"
#define TUN_POOL_NAME L"VPNPool"

// define WinTUN function pointers
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
static SSL_CTX* ssl_ctx = nullptr;
static HANDLE quit_event = NULL;
// static volatile BOOL have_quit = false;
static boost::asio::io_context io_svc;
static boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard{io_svc.get_executor()};
static boost::thread_group thread_pool;
static std::atomic_bool have_quit(false);
static boost::asio::signal_set signals(io_svc, SIGINT, SIGTERM);

const std::string CA_CERT_PATH = "certs/server.crt";
const std::string CLIENT_CERT_PATH = "certs/client.crt";
const std::string CLIENT_KEY_PATH = "certs/client.key";
const std::string SSL_KEYLOG_FILE = "sslkeylog.txt";

// Usage(According to the official readme)
// Include the wintun.h file in project simply by copying it there 
// and dynamically load the wintun.dll using LoadLibraryEx() and GetProcAddress() to resolve each function, 
// using the typedefs provided in the header file
bool InitializeWintun(){
    wintun_module = LoadLibraryExW(L"wintun.dll", NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!wintun_module){
        std::cerr << "Failed to load wintun.dll. Error: " << GetLastError() << std::endl;
        return false;
    }
    // use X instead of LOAD_FUNC because the example.c does so and X costs fewer letters
#define X(Name) ((*(FARPROC *)&Name = GetProcAddress(wintun_module, #Name)) == NULL)
    if (X(WintunCreateAdapter) || X(WintunCloseAdapter) || X(WintunOpenAdapter) || X(WintunGetAdapterLUID) ||
        X(WintunGetRunningDriverVersion) || X(WintunDeleteDriver) || X(WintunSetLogger) || X(WintunStartSession) ||
        X(WintunEndSession) || X(WintunGetReadWaitEvent) || X(WintunReceivePacket) || X(WintunReleaseReceivePacket) ||
        X(WintunAllocateSendPacket) || X(WintunSendPacket))
#undef X
    {
        DWORD LastError = GetLastError();
        FreeLibrary(wintun_module);
        SetLastError(LastError);
        return false;
    }
    return true;
}


void CleanupWintun(){
    if(tun_session){
        WintunEndSession(tun_session);
        tun_session = NULL;
    }
    if(tun_adapter){
        WintunCloseAdapter(tun_adapter);
        tun_adapter = NULL;
    }
    if(wintun_module){
        FreeLibrary(wintun_module);
        wintun_module = NULL;
    }
}

static BOOL WINAPI CtrlHandler(DWORD CtrlType){
    switch (CtrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        std::cerr << "Shutting down..." << std::endl;
        have_quit = TRUE;
        SetEvent(quit_event);
        return TRUE;
    }
    return FALSE;
}


bool init_wintun_adapter(const char* ip, int prefix){
    GUID guid;
    CoCreateGuid(&guid);
    // With the library setup, Wintun can then be used by first creating an adapter, 
    // configuring it, and then setting its status to "up"
    tun_adapter = WintunCreateAdapter(TUN_POOL_NAME, TUN_DEVICE_NAME, &guid);
    if(!tun_adapter){
        tun_adapter = WintunOpenAdapter(TUN_DEVICE_NAME);
        if(!tun_adapter){
            std::cerr << "Failed to create/open WinTUN adapter. Error: " << GetLastError() << std::endl;
            return false;
        }
    }

    NET_LUID luid;
    WintunGetAdapterLUID(tun_adapter, &luid);

    // After creating an adapter, we can use it by starting a session
    tun_session = WintunStartSession(tun_adapter, 0x400000);
    if(!tun_session){
        std::cerr << "Failed to start WinTUN session. Error: " << GetLastError() << std::endl;
        return false;
    }

    // set IP
    MIB_UNICASTIPADDRESS_ROW ipRow;
    InitializeUnicastIpAddressEntry(&ipRow);
    ipRow.InterfaceLuid = luid;
    ipRow.Address.Ipv4.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &ipRow.Address.Ipv4.sin_addr);
    ipRow.OnLinkPrefixLength = prefix;
    ipRow.DadState = IpDadStatePreferred;

    DWORD result = CreateUnicastIpAddressEntry(&ipRow);
    if(result != ERROR_SUCCESS && result != ERROR_OBJECT_ALREADY_EXISTS){
        std::cerr << "Failed to set IP address. Error: " << result << std::endl;
        return false;
    }
    return true;
}


bool InitSSL(boost::asio::ssl::context& ctx){
    try{
        ctx.set_options(
            boost::asio::ssl::context::default_workarounds |
            boost::asio::ssl::context::no_sslv2 |
            boost::asio::ssl::context::no_sslv3 |
            boost::asio::ssl::context::no_tlsv1 |
            boost::asio::ssl::context::no_tlsv1_1
        );
        ctx.load_verify_file(CA_CERT_PATH);
        ctx.use_certificate_file(CLIENT_CERT_PATH, boost::asio::ssl::context::pem);
        ctx.use_private_key_file(CLIENT_KEY_PATH, boost::asio::ssl::context::pem);
        ctx.set_verify_mode(boost::asio::ssl::verify_peer);
        return true;
    }
    catch(const std::exception& e){
        std::cerr << "[SSL] Initialization failed: " << e.what() << std::endl;
        return false;
    }
}


bool read_full(boost::asio::ssl::stream<ip::tcp::socket>& socket, char* buf, size_t length){
    boost::system::error_code ec;
    size_t total_read = 0;

    while(total_read < length && !have_quit){
        size_t n = socket.read_some(boost::asio::buffer(buf + total_read, length - total_read), ec);
        if(ec){
            if(ec == boost::asio::ssl::error::stream_truncated || ec == boost::asio::error::eof){
                return false;
            }
            if(ec != boost::asio::error::would_block){
                std::cerr << "Read Error: " << ec.message() << std::endl;
                return false;
            }
            continue;
        }
        total_read += n;
    }

    return total_read == length;
}

void tun_to_ssl_thread(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& socket){
    HANDLE wait_handles[] = {
        WintunGetReadWaitEvent(tun_session)
    };

    while(!have_quit){
        DWORD wait_result = WaitForMultipleObjects(1, wait_handles, FALSE, 100);
        if(have_quit){
            break;
        }
        if(wait_result != WAIT_OBJECT_0){
            continue;
        }
        DWORD packet_size;
        BYTE* packet = WintunReceivePacket(tun_session, &packet_size);
        if(!packet){
            if(GetLastError() != ERROR_NO_MORE_ITEMS){
                break;
            }
            continue;
        }
        try{
            uint32_t net_len = htonl(packet_size);
            boost::asio::write(socket, boost::asio::buffer(&net_len, 4));
            boost::asio::write(socket, boost::asio::buffer(packet, packet_size));
        }
        catch(const std::exception& e){
            std::cerr << "Write to SSL failed: " << e.what() << std::endl;
            WintunReleaseReceivePacket(tun_session, packet);
            break;
        }
        WintunReleaseReceivePacket(tun_session, packet);
    }
    have_quit = true;
    io_svc.stop();
}

void ssl_to_tun_thread(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& socket){
    char buf[65536];
    while(!have_quit){
        uint32_t net_len;
        if(!read_full(socket, reinterpret_cast<char*>(&net_len), 4)){
            break;
        }
        uint32_t pkt_len = ntohl(net_len);
        if(pkt_len > sizeof(buf)){
            std::cerr << "Packet too large: " << pkt_len << std::endl;
            break;
        }
        if(!read_full(socket, buf, pkt_len)){
            break;
        }
        BYTE* packet = WintunAllocateSendPacket(tun_session, pkt_len);
        if(!packet){
            std::cerr << "Failed to allocate send packet" << std::endl;
            break;
        }
        memcpy(packet, buf, pkt_len);
        WintunSendPacket(tun_session, packet);
    }
    have_quit = true;
    io_svc.stop();
}

void vpn_client(const std::string& server_ip, int port){
    try{
        boost::asio::ssl::context ssl_ctx(boost::asio::ssl::context::tls_client);
        if(!InitSSL(ssl_ctx)){
            return;
        }
        ip::tcp::socket socket(io_svc);
        ip::tcp::resolver resolver(io_svc);
        auto endpoints = resolver.resolve(server_ip, std::to_string(port));

        boost::asio::connect(socket, endpoints);

        boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_stream(io_svc, ssl_ctx);
        ssl_stream.lowest_layer() = std::move(socket);

        // SSL handshake
        ssl_stream.handshake(boost::asio::ssl::stream_base::client);
        std::cout << "SSL connection established with " << server_ip << ":" << port << std::endl;
        // start data forwarding threads
        boost::thread tun_thread(boost::bind(tun_to_ssl_thread, boost::ref(ssl_stream)));
        boost::thread ssl_thread(boost::bind(ssl_to_tun_thread, boost::ref(ssl_stream)));

        signals.async_wait([&](const error_code&, int){
            have_quit = true;
            io_svc.stop();
        });

        io_svc.run();
        tun_thread.join();
        ssl_thread.join();

        try{
            ssl_stream.shutdown();
        }
        catch(const std::exception& e){

        }
    }
    catch(std::exception& e){
        std::cerr << "Exception in vpn_client: " << e.what() << std::endl;
        have_quit = true;
        io_svc.stop();
    }
}

int main(int argc, char* argv[]){
    if(argc != 3){
        std::cerr << "Usage: " << argv[0] << "<server_ip> <port>" << std::endl;
        return 1;
    }
    OpenSSL_add_ssl_algorithms();
    SSL_load_error_strings();

    signals.async_wait([&](const error_code&, int){
        have_quit.store(true);
        io_svc.stop();
    });

    for(int i = 0; i < 2; ++i){
        ::thread_pool.create_thread(boost::bind(&boost::asio::io_context::run, &io_svc));
    }

    if(!InitializeWintun()){
        return 1;
    }

    if(!init_wintun_adapter("10.8.0.2", 24)){
        CleanupWintun();
        return 1;
    }

    vpn_client(argv[1], std::stoi(argv[2]));

    CleanupWintun();

    io_svc.stop();
    ::thread_pool.join_all();

    EVP_cleanup();

    return 0;
}