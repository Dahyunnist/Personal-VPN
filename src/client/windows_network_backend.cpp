#include "personal_vpn/windows_network_backend.hpp"
#include "windows_kill_switch.hpp"

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
#include <array>
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

MIB_UNICASTIPADDRESS_ROW address_row(const NET_LUID luid,
                                     const Ipv6InterfaceAddress& address)
{
    MIB_UNICASTIPADDRESS_ROW row{};
    InitializeUnicastIpAddressEntry(&row);
    row.InterfaceLuid = luid;
    row.Address.Ipv6.sin6_family = AF_INET6;
    std::memcpy(row.Address.Ipv6.sin6_addr.u.Byte, address.address.data(), address.address.size());
    row.OnLinkPrefixLength = address.prefix_length;
    row.PrefixOrigin = IpPrefixOriginManual;
    row.SuffixOrigin = IpSuffixOriginManual;
    row.DadState = IpDadStatePreferred;
    return row;
}

MIB_IPFORWARD_ROW2 route_row(const NET_LUID luid, const Ipv6Route& route)
{
    MIB_IPFORWARD_ROW2 row{};
    InitializeIpForwardEntry(&row);
    row.InterfaceLuid = luid;
    row.DestinationPrefix.Prefix.Ipv6.sin6_family = AF_INET6;
    std::memcpy(row.DestinationPrefix.Prefix.Ipv6.sin6_addr.u.Byte,
                route.network.data(), route.network.size());
    row.DestinationPrefix.PrefixLength = route.prefix_length;
    row.NextHop.Ipv6.sin6_family = AF_INET6;
    std::memcpy(row.NextHop.Ipv6.sin6_addr.u.Byte, route.gateway.data(), route.gateway.size());
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
        close_windows_tunnel_permit(tunnel_policy_session_);
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
            return;
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

    void enable_kill_switch(const std::vector<WindowsVpnEndpoint>& endpoints)
    {
        open_adapter();
        enable_windows_kill_switch(endpoints);
        remove_owned_default_routes(luid_);
        add_transport_bypass_routes(endpoints, luid_);
        NET_IFINDEX interface_index = 0U;
        const auto index_result = ConvertInterfaceLuidToIndex(&luid_, &interface_index);
        if (index_result != NO_ERROR)
        {
            throw_windows_error("read Wintun interface index for WFP", index_result);
        }
        replace_windows_tunnel_permit(tunnel_policy_session_, interface_index);
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

    bool add_interface_address(const Ipv6InterfaceAddress& address) const
    {
        const auto row = address_row(require_luid(), address);
        const auto result = CreateUnicastIpAddressEntry(&row);
        if (result == ERROR_OBJECT_ALREADY_EXISTS)
        {
            return false;
        }
        if (result != NO_ERROR)
        {
            throw_windows_error("add Wintun IPv6 interface address", result);
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

    bool add_route(const Ipv6Route& route) const
    {
        const auto row = route_row(require_luid(), route);
        const auto result = CreateIpForwardEntry2(&row);
        if (result == ERROR_OBJECT_ALREADY_EXISTS)
        {
            return false;
        }
        if (result != NO_ERROR)
        {
            throw_windows_error("add Wintun IPv6 route", result);
        }
        static_cast<void>(FlushIpPathTable(AF_INET6));
        return true;
    }

    std::optional<InterfaceMtuSnapshot> set_interface_mtu(const std::uint32_t mtu) const
    {
        auto ipv4 = interface_row(AF_INET);
        auto ipv6 = interface_row(AF_INET6);
        if (ipv4.NlMtu == mtu && ipv6.NlMtu == mtu)
        {
            return std::nullopt;
        }
        const InterfaceMtuSnapshot prior{ipv4.NlMtu, ipv6.NlMtu};
        ipv4.NlMtu = mtu;
        auto result = SetIpInterfaceEntry(&ipv4);
        if (result != NO_ERROR)
        {
            throw_windows_error("set Wintun interface MTU", result);
        }
        ipv6.NlMtu = mtu;
        result = SetIpInterfaceEntry(&ipv6);
        if (result != NO_ERROR)
        {
            ipv4.NlMtu = prior.ipv4;
            static_cast<void>(SetIpInterfaceEntry(&ipv4));
            throw_windows_error("set Wintun IPv6 interface MTU", result);
        }
        return prior;
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

    void remove_route(const Ipv6Route& route) const noexcept
    {
        if (luid_.Value == 0U)
        {
            return;
        }
        const auto row = route_row(luid_, route);
        static_cast<void>(DeleteIpForwardEntry2(&row));
        static_cast<void>(FlushIpPathTable(AF_INET6));
    }

    void restore_interface_mtu(const InterfaceMtuSnapshot& mtu) const noexcept
    {
        try
        {
            auto ipv4 = interface_row(AF_INET);
            ipv4.NlMtu = mtu.ipv4;
            static_cast<void>(SetIpInterfaceEntry(&ipv4));
            auto ipv6 = interface_row(AF_INET6);
            ipv6.NlMtu = mtu.ipv6;
            static_cast<void>(SetIpInterfaceEntry(&ipv6));
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

    void remove_interface_address(const Ipv6InterfaceAddress& address) const noexcept
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

    static void remove_owned_default_routes(const NET_LUID luid)
    {
        PMIB_IPFORWARD_TABLE2 table = nullptr;
        const auto table_result = GetIpForwardTable2(AF_UNSPEC, &table);
        if (table_result != NO_ERROR)
        {
            throw_windows_error("enumerate stale VPN default routes", table_result);
        }
        struct TableOwner
        {
            PMIB_IPFORWARD_TABLE2 value;
            ~TableOwner() { FreeMibTable(value); }
        } table_owner{table};

        for (ULONG index = 0U; index < table->NumEntries; ++index)
        {
            const auto& route = table->Table[index];
            bool default_route = route.DestinationPrefix.PrefixLength == 0U;
            if (default_route && route.DestinationPrefix.Prefix.si_family == AF_INET)
            {
                default_route = route.DestinationPrefix.Prefix.Ipv4.sin_addr.S_un.S_addr == 0U;
            }
            else if (default_route && route.DestinationPrefix.Prefix.si_family == AF_INET6)
            {
                static constexpr std::array<unsigned char, 16U> any_address{};
                default_route = std::memcmp(route.DestinationPrefix.Prefix.Ipv6.sin6_addr.u.Byte,
                                            any_address.data(),
                                            any_address.size()) == 0;
            }
            else
            {
                default_route = false;
            }
            if (!default_route || route.InterfaceLuid.Value != luid.Value ||
                route.Protocol != MIB_IPPROTO_NETMGMT || route.Metric != 5U)
            {
                continue;
            }
            auto removable_route = route;
            const auto delete_result = DeleteIpForwardEntry2(&removable_route);
            if (delete_result != NO_ERROR && delete_result != ERROR_NOT_FOUND)
            {
                throw_windows_error("remove stale VPN default route", delete_result);
            }
        }
        static_cast<void>(FlushIpPathTable(AF_INET));
        static_cast<void>(FlushIpPathTable(AF_INET6));
    }

    static void add_transport_bypass_routes(
        const std::vector<WindowsVpnEndpoint>& endpoints,
        const NET_LUID tunnel_luid)
    {
        for (const auto& endpoint : endpoints)
        {
            MIB_IPFORWARD_ROW2 prior_route{};
            SOCKADDR_INET destination{};
            SOCKADDR_INET source{};
            if (endpoint.ipv6)
            {
                destination.Ipv6.sin6_family = AF_INET6;
                std::memcpy(destination.Ipv6.sin6_addr.u.Byte,
                            endpoint.address.data(),
                            endpoint.address.size());
            }
            else
            {
                destination.Ipv4.sin_family = AF_INET;
                std::memcpy(&destination.Ipv4.sin_addr,
                            endpoint.address.data(),
                            sizeof(destination.Ipv4.sin_addr));
            }
            const auto route_result = GetBestRoute2(nullptr,
                                                    0U,
                                                    nullptr,
                                                    &destination,
                                                    0U,
                                                    &prior_route,
                                                    &source);
            if (route_result != NO_ERROR)
            {
                throw_windows_error("find physical route to VPN server", route_result);
            }
            if (prior_route.InterfaceLuid.Value == tunnel_luid.Value)
            {
                throw std::runtime_error(
                    "VPN server route resolves through the tunnel adapter; refusing recursion");
            }

            MIB_IPFORWARD_ROW2 bypass{};
            InitializeIpForwardEntry(&bypass);
            bypass.InterfaceLuid = prior_route.InterfaceLuid;
            bypass.Metric = 1U;
            bypass.Protocol = static_cast<NL_ROUTE_PROTOCOL>(MIB_IPPROTO_NETMGMT);
            if (endpoint.ipv6)
            {
                bypass.DestinationPrefix.Prefix.Ipv6.sin6_family = AF_INET6;
                std::memcpy(bypass.DestinationPrefix.Prefix.Ipv6.sin6_addr.u.Byte,
                            endpoint.address.data(),
                            endpoint.address.size());
                bypass.DestinationPrefix.PrefixLength = 128U;
                bypass.NextHop.Ipv6 = prior_route.NextHop.Ipv6;
                bypass.SitePrefixLength = 128U;
            }
            else
            {
                bypass.DestinationPrefix.Prefix.Ipv4.sin_family = AF_INET;
                std::memcpy(&bypass.DestinationPrefix.Prefix.Ipv4.sin_addr,
                            endpoint.address.data(),
                            sizeof(bypass.DestinationPrefix.Prefix.Ipv4.sin_addr));
                bypass.DestinationPrefix.PrefixLength = 32U;
                bypass.NextHop.Ipv4 = prior_route.NextHop.Ipv4;
                bypass.SitePrefixLength = 32U;
            }
            const auto add_result = CreateIpForwardEntry2(&bypass);
            if (add_result != NO_ERROR && add_result != ERROR_OBJECT_ALREADY_EXISTS)
            {
                throw_windows_error("add VPN server bypass route", add_result);
            }
        }
        static_cast<void>(FlushIpPathTable(AF_INET));
        static_cast<void>(FlushIpPathTable(AF_INET6));
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

    MIB_IPINTERFACE_ROW interface_row(const ADDRESS_FAMILY family) const
    {
        MIB_IPINTERFACE_ROW row{};
        InitializeIpInterfaceEntry(&row);
        row.Family = family;
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
    HANDLE tunnel_policy_session_{nullptr};
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

bool WindowsNetworkBackend::add_interface_address(const Ipv6InterfaceAddress& address)
{
    return impl_->add_interface_address(address);
}
std::optional<InterfaceMtuSnapshot> WindowsNetworkBackend::set_interface_mtu(
    const std::uint32_t mtu)
{
    return impl_->set_interface_mtu(mtu);
}
bool WindowsNetworkBackend::add_route(const Ipv4Route& route) { return impl_->add_route(route); }
bool WindowsNetworkBackend::add_route(const Ipv6Route& route) { return impl_->add_route(route); }
void WindowsNetworkBackend::remove_route(const Ipv4Route& route) noexcept
{
    impl_->remove_route(route);
}

void WindowsNetworkBackend::remove_route(const Ipv6Route& route) noexcept
{
    impl_->remove_route(route);
}
void WindowsNetworkBackend::restore_interface_mtu(const InterfaceMtuSnapshot& mtu) noexcept
{
    impl_->restore_interface_mtu(mtu);
}
void WindowsNetworkBackend::remove_interface_address(const InterfaceAddress& address) noexcept
{
    impl_->remove_interface_address(address);
}

void WindowsNetworkBackend::remove_interface_address(const Ipv6InterfaceAddress& address) noexcept
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

void WindowsNetworkBackend::enable_kill_switch(
    const std::vector<WindowsVpnEndpoint>& endpoints)
{
    impl_->enable_kill_switch(endpoints);
}

void WindowsNetworkBackend::disable_kill_switch()
{
    NET_LUID luid{};
    if (ConvertInterfaceAliasToLuid(L"PersonalVPN", &luid) == NO_ERROR)
    {
        Impl::remove_owned_default_routes(luid);
    }
    disable_windows_kill_switch();
}

} // namespace personal_vpn::client
