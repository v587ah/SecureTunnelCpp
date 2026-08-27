#include "tunnel/windows_net_config.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <windows.h>

#include <cstring>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace tunnel {
namespace {

using SetInterfaceDnsSettingsFn = DWORD(WINAPI*)(
    GUID,
    const DNS_INTERFACE_SETTINGS*);
using FreeInterfaceDnsSettingsFn = VOID(WINAPI*)(DNS_INTERFACE_SETTINGS*);

bool ensure_winsock() {
    static bool ready = false;
    static bool ok = false;
    if (ready) {
        return ok;
    }
    WSADATA data{};
    ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    ready = true;
    return ok;
}

std::string format_iphlp_error(const DWORD status) {
    return "IP Helper 错误 " + std::to_string(status);
}

void fill_ipv4_forward_row(
    MIB_IPFORWARD_ROW2& row,
    const std::uint64_t interface_luid,
    const std::uint32_t destination_host_order,
    const std::uint8_t prefix_length,
    const std::uint32_t gateway_host_order) {
    InitializeIpForwardEntry(&row);
    row.InterfaceLuid.Value = interface_luid;
    row.DestinationPrefix.Prefix.si_family = AF_INET;
    row.DestinationPrefix.Prefix.Ipv4.sin_addr.S_un.S_addr =
        htonl(destination_host_order);
    row.DestinationPrefix.PrefixLength = prefix_length;
    row.NextHop.Ipv4.sin_family = AF_INET;
    row.NextHop.Ipv4.sin_addr.S_un.S_addr = htonl(gateway_host_order);
    row.Metric = 0;
    row.Protocol = MIB_IPPROTO_NETMGMT;
}

bool delete_forward_row(
    const std::uint64_t interface_luid,
    const std::uint32_t destination_host_order,
    const std::uint8_t prefix_length,
    const std::uint32_t gateway_host_order) {
    MIB_IPFORWARD_ROW2 row{};
    fill_ipv4_forward_row(
        row, interface_luid, destination_host_order, prefix_length, gateway_host_order);
    const DWORD status = DeleteIpForwardEntry2(&row);
    return status == NO_ERROR || status == ERROR_NOT_FOUND;
}

}  // namespace

bool WindowsNetConfigurator::parse_ipv4(
    const std::string& text,
    std::uint32_t& host_order_address,
    std::string& error) {
    if (!ensure_winsock()) {
        error = "WSAStartup 失败";
        return false;
    }

    IN_ADDR addr{};
    if (InetPtonA(AF_INET, text.c_str(), &addr) != 1) {
        error = "无效的 IPv4 地址: " + text;
        return false;
    }

    host_order_address = ntohl(addr.S_un.S_addr);
    error.clear();
    return true;
}

bool WindowsNetConfigurator::apply_ipv4(
    const std::uint64_t luid_value,
    const Ipv4InterfaceConfig& config) {
    last_error_.clear();

    if (luid_value == 0) {
        last_error_ = "适配器 LUID 无效";
        return false;
    }
    if (config.prefix_length == 0 || config.prefix_length > 32) {
        last_error_ = "IPv4 前缀长度必须在 1 到 32 之间";
        return false;
    }

    std::uint32_t host_ip = 0;
    if (!parse_ipv4(config.address, host_ip, last_error_)) {
        return false;
    }

    NET_LUID luid{};
    luid.Value = luid_value;

    MIB_UNICASTIPADDRESS_ROW row{};
    InitializeUnicastIpAddressEntry(&row);
    row.Address.Ipv4.sin_family = AF_INET;
    row.Address.Ipv4.sin_addr.S_un.S_addr = htonl(host_ip);
    row.InterfaceLuid = luid;
    row.OnLinkPrefixLength = config.prefix_length;
    row.DadState = IpDadStatePreferred;

    const DWORD status = CreateUnicastIpAddressEntry(&row);
    if (status != NO_ERROR && status != ERROR_OBJECT_ALREADY_EXISTS) {
        last_error_ = "添加 IPv4 地址失败。" + format_iphlp_error(status);
        return false;
    }

    if (!config.dns.empty() && !apply_dns(luid_value, config.dns)) {
        return false;
    }

    return true;
}

bool WindowsNetConfigurator::clear_ipv4(
    const std::uint64_t luid_value,
    const Ipv4InterfaceConfig& config) noexcept {
    try {
        last_error_.clear();
        if (luid_value == 0 || config.address.empty()) {
            return true;
        }

        std::uint32_t host_ip = 0;
        std::string parse_error;
        if (!parse_ipv4(config.address, host_ip, parse_error)) {
            last_error_ = parse_error;
            return false;
        }

        NET_LUID luid{};
        luid.Value = luid_value;

        MIB_UNICASTIPADDRESS_ROW row{};
        InitializeUnicastIpAddressEntry(&row);
        row.Address.Ipv4.sin_family = AF_INET;
        row.Address.Ipv4.sin_addr.S_un.S_addr = htonl(host_ip);
        row.InterfaceLuid = luid;
        row.OnLinkPrefixLength = config.prefix_length;

        const DWORD status = DeleteUnicastIpAddressEntry(&row);
        if (status != NO_ERROR && status != ERROR_NOT_FOUND) {
            last_error_ = "删除 IPv4 地址失败。" + format_iphlp_error(status);
            return false;
        }
        return true;
    } catch (...) {
        last_error_ = "清理 IPv4 时发生未知异常";
        return false;
    }
}

bool WindowsNetConfigurator::apply_dns(
    const std::uint64_t luid_value,
    const std::string& dns_ipv4) {
    last_error_.clear();

    std::uint32_t ignored = 0;
    if (!parse_ipv4(dns_ipv4, ignored, last_error_)) {
        return false;
    }

    NET_LUID luid{};
    luid.Value = luid_value;
    GUID guid{};
    const DWORD convert_status = ConvertInterfaceLuidToGuid(&luid, &guid);
    if (convert_status != NO_ERROR) {
        last_error_ =
            "LUID 转 GUID 失败。" + format_iphlp_error(convert_status);
        return false;
    }

    HMODULE iphlpapi = GetModuleHandleW(L"iphlpapi.dll");
    if (iphlpapi == nullptr) {
        iphlpapi = LoadLibraryW(L"iphlpapi.dll");
    }
    if (iphlpapi == nullptr) {
        last_error_ = "无法加载 iphlpapi.dll";
        return false;
    }

    const auto set_dns = reinterpret_cast<SetInterfaceDnsSettingsFn>(
        GetProcAddress(iphlpapi, "SetInterfaceDnsSettings"));
    if (set_dns == nullptr) {
        last_error_ =
            "当前系统不支持 SetInterfaceDnsSettings（需要 Windows 10 1809+）";
        return false;
    }

    std::wstring dns_wide(dns_ipv4.begin(), dns_ipv4.end());

    DNS_INTERFACE_SETTINGS settings{};
    settings.Version = DNS_INTERFACE_SETTINGS_VERSION1;
    settings.Flags = DNS_SETTING_NAMESERVER;
    settings.NameServer = dns_wide.data();

    const DWORD status = set_dns(guid, &settings);
    if (status != NO_ERROR) {
        last_error_ = "设置接口 DNS 失败。" + format_iphlp_error(status);
        return false;
    }
    return true;
}

bool WindowsNetConfigurator::clear_dns(const std::uint64_t luid_value) noexcept {
    try {
        last_error_.clear();
        if (luid_value == 0) {
            return true;
        }

        NET_LUID luid{};
        luid.Value = luid_value;
        GUID guid{};
        if (ConvertInterfaceLuidToGuid(&luid, &guid) != NO_ERROR) {
            return false;
        }

        HMODULE iphlpapi = GetModuleHandleW(L"iphlpapi.dll");
        if (iphlpapi == nullptr) {
            return false;
        }

        const auto set_dns = reinterpret_cast<SetInterfaceDnsSettingsFn>(
            GetProcAddress(iphlpapi, "SetInterfaceDnsSettings"));
        if (set_dns == nullptr) {
            return false;
        }

        DNS_INTERFACE_SETTINGS settings{};
        settings.Version = DNS_INTERFACE_SETTINGS_VERSION1;
        settings.Flags = DNS_SETTING_NAMESERVER;
        settings.NameServer = const_cast<PWSTR>(L"");

        return set_dns(guid, &settings) == NO_ERROR;
    } catch (...) {
        return false;
    }
}

bool WindowsNetConfigurator::apply_endpoint_bypass_route(
    const std::string& endpoint_host,
    const std::uint64_t tunnel_luid_value,
    std::string& error) {
    last_error_.clear();
    error.clear();

    if (endpoint_host == "127.0.0.1" || endpoint_host == "localhost") {
        return true;
    }

    if (!ensure_winsock()) {
        error = last_error_ = "WSAStartup 失败";
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(endpoint_host.c_str(), nullptr, &hints, &result) != 0 ||
        result == nullptr) {
        error = last_error_ = "无法解析节点地址: " + endpoint_host;
        return false;
    }

    const auto server_addr =
        reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
    const std::uint32_t destination_host_order = ntohl(server_addr.S_un.S_addr);
    freeaddrinfo(result);

    PMIB_IPFORWARD_TABLE2 table = nullptr;
    const DWORD table_status = GetIpForwardTable2(AF_INET, &table);
    if (table_status != NO_ERROR) {
        error = last_error_ = "获取路由表失败。" + format_iphlp_error(table_status);
        return false;
    }

    PMIB_IPFORWARD_ROW2 best = nullptr;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        MIB_IPFORWARD_ROW2& row = table->Table[i];
        if (row.DestinationPrefix.PrefixLength != 0) {
            continue;
        }
        if (row.InterfaceLuid.Value == tunnel_luid_value) {
            continue;
        }
        if (row.NextHop.Ipv4.sin_family != AF_INET) {
            continue;
        }
        if (row.NextHop.Ipv4.sin_addr.S_un.S_addr == 0) {
            continue;
        }
        if (best == nullptr || row.Metric < best->Metric) {
            best = &row;
        }
    }

    if (best == nullptr) {
        FreeMibTable(table);
        error = last_error_ = "未找到可用的默认路由作为旁路网关";
        return false;
    }

    const std::uint32_t gateway_host_order =
        ntohl(best->NextHop.Ipv4.sin_addr.S_un.S_addr);
    const std::uint64_t interface_luid = best->InterfaceLuid.Value;
    FreeMibTable(table);

    MIB_IPFORWARD_ROW2 row{};
    fill_ipv4_forward_row(
        row,
        interface_luid,
        destination_host_order,
        32,
        gateway_host_order);

    const DWORD create_status = CreateIpForwardEntry2(&row);
    if (create_status != NO_ERROR && create_status != ERROR_OBJECT_ALREADY_EXISTS) {
        error = last_error_ =
            "添加节点旁路路由失败。" + format_iphlp_error(create_status);
        return false;
    }

    bypass_route_applied_ = true;
    bypass_interface_luid_ = interface_luid;

    char gateway_dotted[INET_ADDRSTRLEN]{};
    IN_ADDR addr{};
    addr.S_un.S_addr = htonl(gateway_host_order);
    bypass_gateway_ =
        InetNtopA(AF_INET, &addr, gateway_dotted, sizeof(gateway_dotted)) != nullptr
            ? gateway_dotted
            : "0.0.0.0";

    char server_dotted[INET_ADDRSTRLEN]{};
    IN_ADDR server{};
    server.S_un.S_addr = htonl(destination_host_order);
    bypass_destination_ =
        InetNtopA(AF_INET, &server, server_dotted, sizeof(server_dotted)) != nullptr
            ? server_dotted
            : endpoint_host;

    error.clear();
    return true;
}

bool WindowsNetConfigurator::apply_global_default_route(
    const std::uint64_t luid_value,
    const std::string& gateway_ipv4,
    std::string& error) {
    last_error_.clear();
    error.clear();

    if (luid_value == 0) {
        error = last_error_ = "适配器 LUID 无效";
        return false;
    }

    std::uint32_t gateway_host_order = 0;
    if (!parse_ipv4(gateway_ipv4, gateway_host_order, error)) {
        last_error_ = error;
        return false;
    }

    MIB_IPFORWARD_ROW2 row{};
    fill_ipv4_forward_row(row, luid_value, 0, 0, gateway_host_order);

    const DWORD status = CreateIpForwardEntry2(&row);
    if (status != NO_ERROR && status != ERROR_OBJECT_ALREADY_EXISTS) {
        error = last_error_ = "添加全局默认路由失败。" + format_iphlp_error(status);
        return false;
    }

    default_route_applied_ = true;
    default_tunnel_luid_ = luid_value;
    default_gateway_ = gateway_ipv4;
    error.clear();
    return true;
}

bool WindowsNetConfigurator::apply_prefix_route(
    const std::uint64_t tunnel_luid_value,
    const std::string& destination_ipv4,
    const std::uint8_t prefix_length,
    const std::string& gateway_ipv4,
    std::string& error) {
    last_error_.clear();
    error.clear();

    if (tunnel_luid_value == 0) {
        error = last_error_ = "适配器 LUID 无效";
        return false;
    }
    if (prefix_length > 32) {
        error = last_error_ = "前缀长度非法";
        return false;
    }

    std::uint32_t destination_host_order = 0;
    if (!parse_ipv4(destination_ipv4, destination_host_order, error)) {
        last_error_ = error;
        return false;
    }

    if (prefix_length < 32) {
        const std::uint32_t mask =
            prefix_length == 0 ? 0U : (~0U << (32U - prefix_length));
        destination_host_order &= mask;
    }

    std::uint32_t gateway_host_order = 0;
    if (!parse_ipv4(gateway_ipv4, gateway_host_order, error)) {
        last_error_ = error;
        return false;
    }

    SelectiveRouteKey key{
        tunnel_luid_value,
        destination_host_order,
        prefix_length,
        gateway_ipv4,
    };
    for (const auto& existing : selective_routes_) {
        if (existing == key) {
            error.clear();
            return true;
        }
    }

    MIB_IPFORWARD_ROW2 row{};
    fill_ipv4_forward_row(
        row,
        tunnel_luid_value,
        destination_host_order,
        prefix_length,
        gateway_host_order);

    const DWORD status = CreateIpForwardEntry2(&row);
    if (status != NO_ERROR && status != ERROR_OBJECT_ALREADY_EXISTS) {
        error = last_error_ = "添加前缀路由失败。" + format_iphlp_error(status);
        return false;
    }

    selective_routes_.push_back(key);
    error.clear();
    return true;
}

void WindowsNetConfigurator::clear_client_routes() noexcept {
    try {
        for (const auto& route : selective_routes_) {
            std::uint32_t gateway_host_order = 0;
            std::string parse_error;
            if (parse_ipv4(route.gateway, gateway_host_order, parse_error)) {
                (void)delete_forward_row(
                    route.tunnel_luid,
                    route.destination_host_order,
                    route.prefix_length,
                    gateway_host_order);
            }
        }
        selective_routes_.clear();

        if (default_route_applied_) {
            std::uint32_t gateway_host_order = 0;
            std::string parse_error;
            if (parse_ipv4(default_gateway_, gateway_host_order, parse_error)) {
                (void)delete_forward_row(
                    default_tunnel_luid_, 0, 0, gateway_host_order);
            }
            default_route_applied_ = false;
            default_tunnel_luid_ = 0;
            default_gateway_.clear();
        }

        if (bypass_route_applied_) {
            std::uint32_t destination_host_order = 0;
            std::uint32_t gateway_host_order = 0;
            std::string parse_error;
            if (parse_ipv4(bypass_destination_, destination_host_order, parse_error) &&
                parse_ipv4(bypass_gateway_, gateway_host_order, parse_error)) {
                (void)delete_forward_row(
                    bypass_interface_luid_,
                    destination_host_order,
                    32,
                    gateway_host_order);
            }
            bypass_route_applied_ = false;
            bypass_destination_.clear();
            bypass_gateway_.clear();
            bypass_interface_luid_ = 0;
        }
    } catch (...) {
        default_route_applied_ = false;
        bypass_route_applied_ = false;
    }
}

const std::string& WindowsNetConfigurator::last_error() const noexcept {
    return last_error_;
}

std::size_t WindowsNetConfigurator::selective_route_count() const noexcept {
    return selective_routes_.size();
}

}  // namespace tunnel
