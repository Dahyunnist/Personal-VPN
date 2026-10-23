#include "windows_kill_switch.hpp"
#include "windows_kill_switch_policy.hpp"

#if !defined(_WIN32)
#error "Windows kill switch must only be built on Windows"
#endif

#define WIN32_LEAN_AND_MEAN
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <winsock2.h>
#include <Windows.h>
#include <fwpmu.h>
#include <fwptypes.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace personal_vpn::client
{
namespace
{

// WFP built-in GUIDs are not declared by all MinGW SDK releases.
GUID kOutboundIpPacketV4{0x1e5c9fae, 0x8a84, 0x4135,
                         {0xa3, 0x31, 0x95, 0x0b, 0x54, 0x22, 0x9e, 0xcd}};
GUID kOutboundIpPacketV6{0xa3b3ab6b, 0x3564, 0x488c,
                         {0x91, 0x17, 0xf3, 0x4e, 0x82, 0x14, 0x27, 0x63}};
GUID kOutboundTransportV4{0x09e61aea, 0xd214, 0x46e2,
                          {0x9b, 0x21, 0xb2, 0x6b, 0x0b, 0x2f, 0x28, 0xc8}};
GUID kOutboundTransportV6{0xe1735bde, 0x013f, 0x4655,
                          {0xb3, 0x51, 0xa4, 0x9e, 0x15, 0x76, 0x2d, 0xf0}};
GUID kRemoteAddressCondition{0xb235ae9a, 0x1d64, 0x49b8,
                             {0xa4, 0x4c, 0x5f, 0xf3, 0xd9, 0x09, 0x50, 0x45}};
GUID kInterfaceIndexCondition{0x667fd755, 0xd695, 0x434a,
                              {0x8a, 0xf5, 0xd3, 0x83, 0x5a, 0x12, 0x59, 0xbc}};
GUID kProtocolCondition{0x3971ef2b, 0x623e, 0x4f9a,
                        {0x8c, 0xb1, 0x6e, 0x79, 0xb8, 0x06, 0xb9, 0xa7}};
GUID kRemotePortCondition{0xc35a604d, 0xd22b, 0x4e1a,
                          {0x91, 0xb4, 0x68, 0xf6, 0x74, 0xee, 0x67, 0x4b}};
GUID kProviderKey{0x8c73dcad, 0xa270, 0x45a9,
                  {0x90, 0x27, 0x2f, 0x5b, 0x13, 0x90, 0x7d, 0x11}};
GUID kSubLayerKey{0x02a254b3, 0x51d2, 0x4b0c,
                  {0x8e, 0xf7, 0x6a, 0x79, 0xa1, 0x7f, 0x42, 0x3e}};

constexpr UINT32 kProviderPersistent = 0x00000001U;
constexpr UINT16 kSubLayerPersistent = 0x0001U;
constexpr UINT32 kFilterPersistent = 0x00000001U;
constexpr UINT32 kFilterBootTime = 0x00000002U;
constexpr UINT32 kIncludeBootTimeFilters = 0x00000008U;
constexpr UINT32 kDynamicSession = 0x00000001U;
constexpr UINT64 kAllowWeight = 0xF000000000000000ULL;
constexpr UINT64 kBlockWeight = 0x1000000000000000ULL;

[[noreturn]] void throw_wfp_error(const char* operation, const DWORD error)
{
    throw std::system_error(static_cast<int>(error), std::system_category(), operation);
}

void check_wfp(const DWORD result, const char* operation)
{
    if (result != ERROR_SUCCESS)
    {
        throw_wfp_error(operation, result);
    }
}

class Engine final
{
   public:
    Engine()
    {
        FWPM_SESSION0 session{};
        check_wfp(FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &handle_),
                  "open Windows Filtering Platform engine");
    }

    ~Engine()
    {
        if (handle_ != nullptr)
        {
            static_cast<void>(FwpmEngineClose0(handle_));
        }
    }

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

   private:
    HANDLE handle_{nullptr};
};

class Transaction final
{
   public:
    explicit Transaction(HANDLE engine) : engine_(engine)
    {
        check_wfp(FwpmTransactionBegin0(engine_, 0U), "begin WFP transaction");
    }

    ~Transaction()
    {
        if (!committed_)
        {
            static_cast<void>(FwpmTransactionAbort0(engine_));
        }
    }

    void commit()
    {
        check_wfp(FwpmTransactionCommit0(engine_), "commit WFP transaction");
        committed_ = true;
    }

   private:
    HANDLE engine_;
    bool committed_{false};
};

void ensure_provider_and_sublayer(HANDLE engine)
{
    FWPM_PROVIDER0* existing_provider = nullptr;
    const auto provider_result = FwpmProviderGetByKey0(engine, &kProviderKey, &existing_provider);
    if (provider_result == ERROR_SUCCESS)
    {
        const bool persistent = (existing_provider->flags & kProviderPersistent) != 0U;
        FwpmFreeMemory0(reinterpret_cast<void**>(&existing_provider));
        if (!persistent)
        {
            throw std::runtime_error("existing PersonalVPN WFP provider is not persistent");
        }
    }
    else if (provider_result == static_cast<DWORD>(FWP_E_PROVIDER_NOT_FOUND))
    {
        FWPM_PROVIDER0 provider{};
        provider.providerKey = kProviderKey;
        provider.displayData.name = const_cast<wchar_t*>(L"PersonalVPN fail-closed policy");
        provider.flags = kProviderPersistent;
        check_wfp(FwpmProviderAdd0(engine, &provider, nullptr), "add persistent WFP provider");
    }
    else
    {
        throw_wfp_error("inspect WFP provider", provider_result);
    }

    FWPM_SUBLAYER0* existing_sublayer = nullptr;
    const auto sublayer_result =
        FwpmSubLayerGetByKey0(engine, &kSubLayerKey, &existing_sublayer);
    if (sublayer_result == ERROR_SUCCESS)
    {
        const bool trusted = (existing_sublayer->flags & kSubLayerPersistent) != 0U &&
                             existing_sublayer->providerKey != nullptr &&
                             IsEqualGUID(*existing_sublayer->providerKey, kProviderKey) != FALSE;
        FwpmFreeMemory0(reinterpret_cast<void**>(&existing_sublayer));
        if (!trusted)
        {
            throw std::runtime_error("existing PersonalVPN WFP sublayer has invalid ownership");
        }
    }
    else if (sublayer_result == static_cast<DWORD>(FWP_E_SUBLAYER_NOT_FOUND))
    {
        FWPM_SUBLAYER0 sublayer{};
        sublayer.subLayerKey = kSubLayerKey;
        sublayer.displayData.name = const_cast<wchar_t*>(L"PersonalVPN fail-closed policy");
        sublayer.flags = kSubLayerPersistent;
        sublayer.providerKey = &kProviderKey;
        sublayer.weight = 0x7fffU;
        check_wfp(FwpmSubLayerAdd0(engine, &sublayer, nullptr), "add persistent WFP sublayer");
    }
    else
    {
        throw_wfp_error("inspect WFP sublayer", sublayer_result);
    }
}

std::vector<GUID> enumerate_provider_filters(HANDLE engine)
{
    FWPM_FILTER_ENUM_TEMPLATE0 query{};
    query.providerKey = &kProviderKey;
    query.flags = kIncludeBootTimeFilters;
    HANDLE enumeration = nullptr;
    check_wfp(FwpmFilterCreateEnumHandle0(engine, &query, &enumeration),
              "create WFP filter enumeration");
    std::vector<GUID> keys;
    try
    {
        for (;;)
        {
            FWPM_FILTER0** entries = nullptr;
            UINT32 count = 0U;
            check_wfp(FwpmFilterEnum0(engine, enumeration, 128U, &entries, &count),
                      "enumerate PersonalVPN WFP filters");
            try
            {
                for (UINT32 index = 0U; index < count; ++index)
                {
                    keys.push_back(entries[index]->filterKey);
                }
            }
            catch (...)
            {
                FwpmFreeMemory0(reinterpret_cast<void**>(&entries));
                throw;
            }
            FwpmFreeMemory0(reinterpret_cast<void**>(&entries));
            if (count == 0U)
            {
                break;
            }
        }
    }
    catch (...)
    {
        static_cast<void>(FwpmFilterDestroyEnumHandle0(engine, enumeration));
        throw;
    }
    check_wfp(FwpmFilterDestroyEnumHandle0(engine, enumeration),
              "destroy WFP filter enumeration");
    return keys;
}

void delete_provider_filters(HANDLE engine)
{
    for (const auto& key : enumerate_provider_filters(engine))
    {
        const auto result = FwpmFilterDeleteByKey0(engine, &key);
        if (result != ERROR_SUCCESS &&
            result != static_cast<DWORD>(FWP_E_FILTER_NOT_FOUND))
        {
            throw_wfp_error("delete PersonalVPN WFP filter", result);
        }
    }
}

void add_filter(HANDLE engine,
                const GUID& layer,
                const UINT32 flags,
                const FWP_ACTION_TYPE action,
                const UINT64 weight,
                std::vector<FWPM_FILTER_CONDITION0>& conditions,
                const bool persistent_provider_owned = true)
{
    FWPM_FILTER0 filter{};
    filter.displayData.name = const_cast<wchar_t*>(L"PersonalVPN dual-stack fail-closed");
    filter.flags = flags;
    filter.providerKey = persistent_provider_owned ? &kProviderKey : nullptr;
    filter.layerKey = layer;
    filter.subLayerKey = kSubLayerKey;
    filter.weight.type = FWP_UINT64;
    filter.weight.uint64 = const_cast<UINT64*>(&weight);
    filter.numFilterConditions = static_cast<UINT32>(conditions.size());
    filter.filterCondition = conditions.empty() ? nullptr : conditions.data();
    filter.action.type = action;
    check_wfp(FwpmFilterAdd0(engine, &filter, nullptr, nullptr), "add WFP kill-switch filter");
}

void add_unconditional_filter(HANDLE engine,
                              const GUID& layer,
                              const UINT32 flags,
                              const FWP_ACTION_TYPE action,
                              const UINT64 weight)
{
    std::vector<FWPM_FILTER_CONDITION0> no_conditions;
    add_filter(engine, layer, flags, action, weight, no_conditions);
}

FWPM_FILTER_CONDITION0 interface_condition(const UINT32 interface_index,
                                           UINT32& value)
{
    value = interface_index;
    FWPM_FILTER_CONDITION0 condition{};
    condition.fieldKey = kInterfaceIndexCondition;
    condition.matchType = FWP_MATCH_EQUAL;
    condition.conditionValue.type = FWP_UINT32;
    condition.conditionValue.uint32 = value;
    return condition;
}

void add_interface_allow(HANDLE engine, const GUID& layer, const UINT32 interface_index)
{
    UINT32 value = 0U;
    std::vector<FWPM_FILTER_CONDITION0> conditions{
        interface_condition(interface_index, value)};
    add_filter(engine, layer, 0U, FWP_ACTION_PERMIT, kAllowWeight, conditions, false);
}

void add_endpoint_address_condition(const WindowsVpnEndpoint& endpoint,
                                    FWPM_FILTER_CONDITION0& condition,
                                    FWP_V4_ADDR_AND_MASK& v4,
                                    FWP_V6_ADDR_AND_MASK& v6)
{
    condition.fieldKey = kRemoteAddressCondition;
    condition.matchType = FWP_MATCH_EQUAL;
    if (endpoint.ipv6)
    {
        std::copy(endpoint.address.begin(), endpoint.address.end(), v6.addr);
        v6.prefixLength = 128U;
        condition.conditionValue.type = FWP_V6_ADDR_MASK;
        condition.conditionValue.v6AddrMask = &v6;
    }
    else
    {
        std::memcpy(&v4.addr, endpoint.address.data(), 4U);
        v4.addr = ntohl(v4.addr);
        v4.mask = 0xffffffffU;
        condition.conditionValue.type = FWP_V4_ADDR_MASK;
        condition.conditionValue.v4AddrMask = &v4;
    }
}

void add_endpoint_allow(HANDLE engine,
                        const GUID& packet_layer,
                        const GUID& transport_layer,
                        const WindowsVpnEndpoint& endpoint)
{
    FWPM_FILTER_CONDITION0 packet_address{};
    FWP_V4_ADDR_AND_MASK packet_v4{};
    FWP_V6_ADDR_AND_MASK packet_v6{};
    add_endpoint_address_condition(endpoint, packet_address, packet_v4, packet_v6);
    std::vector<FWPM_FILTER_CONDITION0> packet_conditions{packet_address};
    add_filter(engine,
               packet_layer,
               kFilterPersistent,
               FWP_ACTION_PERMIT,
               kAllowWeight,
               packet_conditions);

    FWPM_FILTER_CONDITION0 transport_address{};
    FWP_V4_ADDR_AND_MASK transport_v4{};
    FWP_V6_ADDR_AND_MASK transport_v6{};
    add_endpoint_address_condition(endpoint, transport_address, transport_v4, transport_v6);
    FWPM_FILTER_CONDITION0 protocol_condition{};
    protocol_condition.fieldKey = kProtocolCondition;
    protocol_condition.matchType = FWP_MATCH_EQUAL;
    protocol_condition.conditionValue.type = FWP_UINT8;
    protocol_condition.conditionValue.uint8 = IPPROTO_TCP;
    FWPM_FILTER_CONDITION0 port_condition{};
    port_condition.fieldKey = kRemotePortCondition;
    port_condition.matchType = FWP_MATCH_EQUAL;
    port_condition.conditionValue.type = FWP_UINT16;
    port_condition.conditionValue.uint16 = endpoint.port;
    std::vector<FWPM_FILTER_CONDITION0> transport_conditions{
        transport_address, protocol_condition, port_condition};
    add_filter(engine,
               transport_layer,
               kFilterPersistent,
               FWP_ACTION_PERMIT,
               kAllowWeight,
               transport_conditions);
}

void add_family_policy(HANDLE engine,
                       const GUID& packet_layer,
                       const GUID& transport_layer,
                       const detail::KillSwitchFamilyPolicy& policy)
{
    for (const auto& endpoint : policy.endpoints)
    {
        add_endpoint_allow(engine, packet_layer, transport_layer, endpoint);
    }
    if (policy.persistent_transport_block)
    {
        add_unconditional_filter(engine,
                                 transport_layer,
                                 kFilterPersistent,
                                 FWP_ACTION_BLOCK,
                                 kBlockWeight);
    }
    if (policy.persistent_ip_block)
    {
        add_unconditional_filter(engine,
                                 packet_layer,
                                 kFilterPersistent,
                                 FWP_ACTION_BLOCK,
                                 kBlockWeight);
    }
}

void remove_provider(HANDLE engine)
{
    const auto sublayer_result = FwpmSubLayerDeleteByKey0(engine, &kSubLayerKey);
    if (sublayer_result != ERROR_SUCCESS &&
        sublayer_result != static_cast<DWORD>(FWP_E_SUBLAYER_NOT_FOUND))
    {
        throw_wfp_error("remove PersonalVPN WFP sublayer", sublayer_result);
    }
    const auto provider_result = FwpmProviderDeleteByKey0(engine, &kProviderKey);
    if (provider_result != ERROR_SUCCESS &&
        provider_result != static_cast<DWORD>(FWP_E_PROVIDER_NOT_FOUND))
    {
        throw_wfp_error("remove PersonalVPN WFP provider", provider_result);
    }
}

} // namespace

void enable_windows_kill_switch(const std::vector<WindowsVpnEndpoint>& endpoints)
{
    const auto policy = detail::build_kill_switch_policy(endpoints);

    Engine engine;
    Transaction transaction(engine.get());
    ensure_provider_and_sublayer(engine.get());
    delete_provider_filters(engine.get());

    for (const auto& family : policy)
    {
        const bool ipv6 = family.family == detail::IpFamily::IPv6;
        const auto& packet_layer = ipv6 ? kOutboundIpPacketV6 : kOutboundIpPacketV4;
        const auto& transport_layer = ipv6 ? kOutboundTransportV6 : kOutboundTransportV4;
        if (family.boot_time_ip_block)
        {
            add_unconditional_filter(engine.get(),
                                     packet_layer,
                                     kFilterBootTime,
                                     FWP_ACTION_BLOCK,
                                     kBlockWeight);
        }
        add_family_policy(engine.get(), packet_layer, transport_layer, family);
    }
    transaction.commit();
}

void replace_windows_tunnel_permit(void*& dynamic_session,
                                   const std::uint32_t tunnel_interface_index)
{
    const auto policy = detail::build_tunnel_permit_policy(tunnel_interface_index);
    close_windows_tunnel_permit(dynamic_session);

    FWPM_SESSION0 session{};
    session.flags = kDynamicSession;
    HANDLE new_session = nullptr;
    check_wfp(FwpmEngineOpen0(nullptr,
                              RPC_C_AUTHN_WINNT,
                              nullptr,
                              &session,
                              &new_session),
              "open dynamic WFP tunnel-permit session");
    try
    {
        Transaction transaction(new_session);
        for (const auto family : policy.families)
        {
            const bool ipv6 = family == detail::IpFamily::IPv6;
            add_interface_allow(new_session,
                                ipv6 ? kOutboundIpPacketV6 : kOutboundIpPacketV4,
                                policy.interface_index);
            add_interface_allow(new_session,
                                ipv6 ? kOutboundTransportV6 : kOutboundTransportV4,
                                policy.interface_index);
        }
        transaction.commit();
    }
    catch (...)
    {
        static_cast<void>(FwpmEngineClose0(new_session));
        throw;
    }
    dynamic_session = new_session;
}

void close_windows_tunnel_permit(void*& dynamic_session) noexcept
{
    if (dynamic_session != nullptr)
    {
        static_cast<void>(FwpmEngineClose0(dynamic_session));
        dynamic_session = nullptr;
    }
}

void disable_windows_kill_switch()
{
    Engine engine;
    Transaction transaction(engine.get());
    const auto filters = enumerate_provider_filters(engine.get());
    if (filters.empty())
    {
        remove_provider(engine.get());
        transaction.commit();
        return;
    }
    delete_provider_filters(engine.get());
    remove_provider(engine.get());
    transaction.commit();
}

} // namespace personal_vpn::client
