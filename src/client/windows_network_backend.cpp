#include "personal_vpn/windows_network_backend.hpp"

#if !defined(_WIN32)
#error "WindowsNetworkBackend must only be built on Windows"
#endif

#define WIN32_LEAN_AND_MEAN
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2ipdef.h>
#include <Windows.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

namespace personal_vpn::client
{
namespace
{

using AdapterHandle = void*;
using SessionHandle = void*;
using OpenAdapterFunction = AdapterHandle(WINAPI*)(LPCWSTR);
using CreateAdapterFunction = AdapterHandle(WINAPI*)(LPCWSTR, LPCWSTR, const GUID*);
using CloseAdapterFunction = void(WINAPI*)(AdapterHandle);
using GetAdapterLuidFunction = void(WINAPI*)(AdapterHandle, NET_LUID*);
using StartSessionFunction = SessionHandle(WINAPI*)(AdapterHandle, DWORD);
using EndSessionFunction = void(WINAPI*)(SessionHandle);
using GetReadWaitEventFunction = HANDLE(WINAPI*)(SessionHandle);
using ReceivePacketFunction = BYTE*(WINAPI*)(SessionHandle, DWORD*);
using ReleaseReceivePacketFunction = void(WINAPI*)(SessionHandle, const BYTE*);
using AllocateSendPacketFunction = BYTE*(WINAPI*)(SessionHandle, DWORD);
using SendPacketFunction = void(WINAPI*)(SessionHandle, const BYTE*);

[[noreturn]] void throw_windows_error(const std::string& operation, const DWORD error)
{
    throw std::system_error(static_cast<int>(error), std::system_category(), operation);
}

template <typename Function>
Function load_function(const HMODULE module, const char* name)
{
    static_assert(std::is_pointer<Function>::value, "Wintun export type must be a pointer");
    const auto procedure = GetProcAddress(module, name);
    if (procedure == nullptr)
    {
        throw_windows_error(std::string("missing Wintun export ") + name, GetLastError());
    }
    static_assert(sizeof(procedure) == sizeof(Function), "function pointer size mismatch");
    Function function = nullptr;
    std::memcpy(&function, &procedure, sizeof(function));
    return function;
}

void copy_address(const core::Ipv4Address& source, IN_ADDR& destination) noexcept
{
    static_assert(sizeof(destination) == 4U, "Windows IPv4 address has an unexpected size");
    std::memcpy(&destination, source.data(), source.size());
}

MIB_UNICASTIPADDRESS_ROW address_row(const NET_LUID luid, const InterfaceAddress& address)
{
    MIB_UNICASTIPADDRESS_ROW row{};
    InitializeUnicastIpAddressEntry(&row);
    row.InterfaceLuid = luid;
    row.Address.Ipv4.sin_family = AF_INET;
    copy_address(address.address, row.Address.Ipv4.sin_addr);
    row.OnLinkPrefixLength = address.prefix_length;
    row.PrefixOrigin = IpPrefixOriginManual;
    row.SuffixOrigin = IpSuffixOriginManual;
    row.DadState = IpDadStatePreferred;
    return row;
}

MIB_IPFORWARD_ROW2 route_row(const NET_LUID luid, const Ipv4Route& route)
{
    MIB_IPFORWARD_ROW2 row{};
    InitializeIpForwardEntry(&row);
    row.InterfaceLuid = luid;
    row.DestinationPrefix.Prefix.Ipv4.sin_family = AF_INET;
    copy_address(route.network, row.DestinationPrefix.Prefix.Ipv4.sin_addr);
    row.DestinationPrefix.PrefixLength = route.prefix_length;
    row.NextHop.Ipv4.sin_family = AF_INET;
    copy_address(route.gateway, row.NextHop.Ipv4.sin_addr);
    row.SitePrefixLength = route.prefix_length;
    row.Metric = 5U;
    row.Protocol = static_cast<NL_ROUTE_PROTOCOL>(MIB_IPPROTO_NETMGMT);
    return row;
}

} // namespace

class WindowsNetworkBackend::Impl
{
   public:
    explicit Impl(std::wstring adapter_name) : adapter_name_(std::move(adapter_name))
    {
        if (adapter_name_.empty() || adapter_name_.size() > 128U)
        {
            throw std::invalid_argument("Wintun adapter name must contain 1 to 128 characters");
        }
        interrupt_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (interrupt_event_ == nullptr)
        {
            throw_windows_error("create Wintun interrupt event", GetLastError());
        }
    }

    ~Impl()
    {
        stop_packet_session();
        close_adapter();
        if (module_ != nullptr)
        {
            FreeLibrary(module_);
        }
        CloseHandle(interrupt_event_);
    }

    void load_library()
    {
        if (module_ != nullptr)
        {
            return;
        }
        module_ = LoadLibraryExW(L"wintun.dll",
                                 nullptr,
                                 LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                     LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module_ == nullptr)
        {
            throw_windows_error("load wintun.dll", GetLastError());
        }
        try
        {
            open_adapter_ = load_function<OpenAdapterFunction>(module_, "WintunOpenAdapter");
            create_adapter_ =
                load_function<CreateAdapterFunction>(module_, "WintunCreateAdapter");
            close_adapter_ = load_function<CloseAdapterFunction>(module_, "WintunCloseAdapter");
            get_adapter_luid_ =
                load_function<GetAdapterLuidFunction>(module_, "WintunGetAdapterLUID");
            start_session_ = load_function<StartSessionFunction>(module_, "WintunStartSession");
            end_session_ = load_function<EndSessionFunction>(module_, "WintunEndSession");
            get_read_wait_event_ =
                load_function<GetReadWaitEventFunction>(module_, "WintunGetReadWaitEvent");
            receive_packet_ =
                load_function<ReceivePacketFunction>(module_, "WintunReceivePacket");
            release_receive_packet_ = load_function<ReleaseReceivePacketFunction>(
                module_, "WintunReleaseReceivePacket");
            allocate_send_packet_ = load_function<AllocateSendPacketFunction>(
                module_, "WintunAllocateSendPacket");
            send_packet_ = load_function<SendPacketFunction>(module_, "WintunSendPacket");
        }
        catch (...)
        {
            FreeLibrary(module_);
            module_ = nullptr;
            throw;
        }
    }

    void open_adapter()
    {
        if (adapter_ != nullptr)
        {
            throw std::logic_error("Wintun adapter is already open");
        }
        load_library();
        adapter_ = open_adapter_(adapter_name_.c_str());
        if (adapter_ == nullptr)
        {
            adapter_ = create_adapter_(adapter_name_.c_str(), L"PersonalVPN", nullptr);
        }
        if (adapter_ == nullptr)
        {
            throw_windows_error("open or create Wintun adapter", GetLastError());
        }
        get_adapter_luid_(adapter_, &luid_);
        if (luid_.Value == 0U)
        {
            close_adapter();
            throw std::runtime_error("Wintun returned an invalid interface LUID");
        }
    }

    void start_packet_session()
    {
        if (adapter_ == nullptr || session_ != nullptr)
        {
            throw std::logic_error("Wintun session requires one open adapter and no prior session");
        }
        constexpr DWORD ring_capacity = 4U * 1024U * 1024U;
        ResetEvent(interrupt_event_);
        session_ = start_session_(adapter_, ring_capacity);
        if (session_ == nullptr)
        {
            throw_windows_error("start Wintun packet session", GetLastError());
        }
    }

    bool add_interface_address(const InterfaceAddress& address) const
    {
        const auto row = address_row(require_luid(), address);
        const auto result = CreateUnicastIpAddressEntry(&row);
        if (result == ERROR_OBJECT_ALREADY_EXISTS)
        {
            return false;
        }
        if (result != NO_ERROR)
        {
            throw_windows_error("add Wintun interface address", result);
        }
        return true;
    }

    bool add_route(const Ipv4Route& route) const
    {
        const auto row = route_row(require_luid(), route);
        const auto result = CreateIpForwardEntry2(&row);
        if (result == ERROR_OBJECT_ALREADY_EXISTS)
        {
            return false;
        }
        if (result != NO_ERROR)
        {
            throw_windows_error("add Wintun route", result);
        }
        static_cast<void>(FlushIpPathTable(AF_INET));
        return true;
    }

    std::optional<std::uint32_t> set_interface_mtu(const std::uint32_t mtu) const
    {
        auto row = interface_row();
        if (row.NlMtu == mtu)
        {
            return std::nullopt;
        }
        const auto prior_mtu = row.NlMtu;
        row.NlMtu = mtu;
        const auto result = SetIpInterfaceEntry(&row);
        if (result != NO_ERROR)
        {
            throw_windows_error("set Wintun interface MTU", result);
        }
        return prior_mtu;
    }

    void remove_route(const Ipv4Route& route) const noexcept
    {
        if (luid_.Value == 0U)
        {
            return;
        }
        const auto row = route_row(luid_, route);
        static_cast<void>(DeleteIpForwardEntry2(&row));
        static_cast<void>(FlushIpPathTable(AF_INET));
    }

    void restore_interface_mtu(const std::uint32_t mtu) const noexcept
    {
        try
        {
            auto row = interface_row();
            row.NlMtu = mtu;
            static_cast<void>(SetIpInterfaceEntry(&row));
        }
        catch (...)
        {
        }
    }

    void remove_interface_address(const InterfaceAddress& address) const noexcept
    {
        if (luid_.Value == 0U)
        {
            return;
        }
        const auto row = address_row(luid_, address);
        static_cast<void>(DeleteUnicastIpAddressEntry(&row));
    }

    void stop_packet_session() noexcept
    {
        interrupt_receive();
        if (session_ != nullptr)
        {
            end_session_(session_);
            session_ = nullptr;
        }
    }

    void close_adapter() noexcept
    {
        stop_packet_session();
        if (adapter_ != nullptr)
        {
            close_adapter_(adapter_);
            adapter_ = nullptr;
        }
        luid_.Value = 0U;
    }

    std::optional<std::vector<std::uint8_t>> receive_packet() const
    {
        require_session();
        const HANDLE events[]{interrupt_event_, get_read_wait_event_(session_)};
        for (;;)
        {
            const auto result = WaitForMultipleObjects(2U, events, FALSE, INFINITE);
            if (result == WAIT_OBJECT_0)
            {
                return std::nullopt;
            }
            if (result != WAIT_OBJECT_0 + 1U)
            {
                throw_windows_error("wait for Wintun packet", GetLastError());
            }
            auto packet = try_receive_packet();
            if (packet.has_value())
            {
                return packet;
            }
        }
    }

    void interrupt_receive() const noexcept { SetEvent(interrupt_event_); }

    std::optional<std::vector<std::uint8_t>> try_receive_packet() const
    {
        require_session();
        DWORD packet_size = 0U;
        const auto* packet = receive_packet_(session_, &packet_size);
        if (packet == nullptr)
        {
            const auto error = GetLastError();
            if (error == ERROR_NO_MORE_ITEMS)
            {
                return std::nullopt;
            }
            throw_windows_error("receive Wintun packet", error);
        }
        struct PacketRelease
        {
            SessionHandle session;
            const BYTE* packet;
            ReleaseReceivePacketFunction release;
            ~PacketRelease() { release(session, packet); }
        } release{session_, packet, release_receive_packet_};
        return std::vector<std::uint8_t>(packet, packet + packet_size);
    }

    void send_packet(const std::vector<std::uint8_t>& packet) const
    {
        require_session();
        if (packet.empty() || packet.size() > std::numeric_limits<DWORD>::max())
        {
            throw std::invalid_argument("Wintun packet size is invalid");
        }
        auto* destination = allocate_send_packet_(session_, static_cast<DWORD>(packet.size()));
        if (destination == nullptr)
        {
            throw_windows_error("allocate Wintun send packet", GetLastError());
        }
        std::copy(packet.begin(), packet.end(), destination);
        send_packet_(session_, destination);
    }

   private:
    NET_LUID require_luid() const
    {
        if (adapter_ == nullptr || luid_.Value == 0U)
        {
            throw std::logic_error("Wintun adapter is not open");
        }
        return luid_;
    }

    MIB_IPINTERFACE_ROW interface_row() const
    {
        MIB_IPINTERFACE_ROW row{};
        InitializeIpInterfaceEntry(&row);
        row.Family = AF_INET;
        row.InterfaceLuid = require_luid();
        const auto result = GetIpInterfaceEntry(&row);
        if (result != NO_ERROR)
        {
            throw_windows_error("read Wintun IP interface", result);
        }
        return row;
    }

    void require_session() const
    {
        if (session_ == nullptr)
        {
            throw std::logic_error("Wintun packet session is not active");
        }
    }

    std::wstring adapter_name_;
    HMODULE module_{nullptr};
    HANDLE interrupt_event_{nullptr};
    AdapterHandle adapter_{nullptr};
    SessionHandle session_{nullptr};
    NET_LUID luid_{};
    OpenAdapterFunction open_adapter_{nullptr};
    CreateAdapterFunction create_adapter_{nullptr};
    CloseAdapterFunction close_adapter_{nullptr};
    GetAdapterLuidFunction get_adapter_luid_{nullptr};
    StartSessionFunction start_session_{nullptr};
    EndSessionFunction end_session_{nullptr};
    GetReadWaitEventFunction get_read_wait_event_{nullptr};
    ReceivePacketFunction receive_packet_{nullptr};
    ReleaseReceivePacketFunction release_receive_packet_{nullptr};
    AllocateSendPacketFunction allocate_send_packet_{nullptr};
    SendPacketFunction send_packet_{nullptr};
};

WindowsNetworkBackend::WindowsNetworkBackend(std::wstring adapter_name)
    : impl_(std::make_unique<Impl>(std::move(adapter_name)))
{
}

WindowsNetworkBackend::~WindowsNetworkBackend() = default;

void WindowsNetworkBackend::open_adapter() { impl_->open_adapter(); }
void WindowsNetworkBackend::start_packet_session() { impl_->start_packet_session(); }
bool WindowsNetworkBackend::add_interface_address(const InterfaceAddress& address)
{
    return impl_->add_interface_address(address);
}
std::optional<std::uint32_t> WindowsNetworkBackend::set_interface_mtu(
    const std::uint32_t mtu)
{
    return impl_->set_interface_mtu(mtu);
}
bool WindowsNetworkBackend::add_route(const Ipv4Route& route) { return impl_->add_route(route); }
void WindowsNetworkBackend::remove_route(const Ipv4Route& route) noexcept
{
    impl_->remove_route(route);
}
void WindowsNetworkBackend::restore_interface_mtu(const std::uint32_t mtu) noexcept
{
    impl_->restore_interface_mtu(mtu);
}
void WindowsNetworkBackend::remove_interface_address(const InterfaceAddress& address) noexcept
{
    impl_->remove_interface_address(address);
}
void WindowsNetworkBackend::stop_packet_session() noexcept { impl_->stop_packet_session(); }
void WindowsNetworkBackend::close_adapter() noexcept { impl_->close_adapter(); }
std::optional<std::vector<std::uint8_t>> WindowsNetworkBackend::receive_packet()
{
    return impl_->receive_packet();
}
void WindowsNetworkBackend::interrupt_receive() noexcept { impl_->interrupt_receive(); }
void WindowsNetworkBackend::send_packet(const std::vector<std::uint8_t>& packet)
{
    impl_->send_packet(packet);
}

} // namespace personal_vpn::client
