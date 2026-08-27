#include "tunnel/smart_route_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

namespace tunnel {
namespace {

std::string trim_copy(std::string text) {
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.erase(text.begin());
    }
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.pop_back();
    }
    return text;
}

bool ensure_winsock_once() {
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

std::vector<std::string> load_list_file(
    const std::string& path,
    std::string& error) {
    std::ifstream input(path);
    if (!input.is_open()) {
        error = "无法打开列表文件: " + path;
        return {};
    }

    std::vector<std::string> entries;
    std::string line;
    while (std::getline(input, line)) {
        line = trim_copy(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line = trim_copy(line.substr(0, comment));
        }
        if (!line.empty()) {
            entries.push_back(line);
        }
    }

    error.clear();
    return entries;
}

std::string host_order_to_dotted(const std::uint32_t host_order) {
    IN_ADDR addr{};
    addr.S_un.S_addr = htonl(host_order);
    char buffer[INET_ADDRSTRLEN]{};
    if (InetNtopA(AF_INET, &addr, buffer, sizeof(buffer)) == nullptr) {
        return {};
    }
    return buffer;
}

}  // namespace

std::vector<std::string> load_route_domain_list(
    const std::string& path,
    std::string& error) {
    return load_list_file(path, error);
}

bool parse_cidr_prefix(
    const std::string& text,
    std::string& network_ipv4,
    std::uint8_t& prefix_length,
    std::string& error) {
    const auto trimmed = trim_copy(text);
    const auto slash = trimmed.find('/');
    std::string host_part = trimmed;
    prefix_length = 32;

    if (slash != std::string::npos) {
        host_part = trim_copy(trimmed.substr(0, slash));
        try {
            const int parsed = std::stoi(trimmed.substr(slash + 1));
            if (parsed < 0 || parsed > 32) {
                error = "CIDR 前缀长度非法: " + trimmed;
                return false;
            }
            prefix_length = static_cast<std::uint8_t>(parsed);
        } catch (...) {
            error = "CIDR 前缀长度非法: " + trimmed;
            return false;
        }
    }

    std::uint32_t network_host_order = 0;
    if (!WindowsNetConfigurator::parse_ipv4(
            host_part, network_host_order, error)) {
        return false;
    }

    if (prefix_length < 32) {
        const std::uint32_t mask =
            prefix_length == 0 ? 0U : (~0U << (32U - prefix_length));
        network_host_order &= mask;
    }

    network_ipv4 = host_order_to_dotted(network_host_order);
    if (network_ipv4.empty()) {
        error = "CIDR 网络地址无效: " + trimmed;
        return false;
    }

    error.clear();
    return true;
}

std::vector<std::string> load_route_cidr_list(
    const std::string& path,
    std::string& error) {
    return load_list_file(path, error);
}

SmartRouteManager::SmartRouteManager(WindowsNetConfigurator& net_config)
    : net_config_(net_config) {}

SmartRouteManager::~SmartRouteManager() {
    stop();
}

bool SmartRouteManager::start(const Options& options, std::string& error) {
    stop();

    if (options.tunnel_luid == 0) {
        error = "隧道 LUID 无效";
        return false;
    }

    options_ = options;

    if (!options.domain_list_path.empty()) {
        domains_ = load_route_domain_list(options.domain_list_path, error);
        if (!error.empty()) {
            return false;
        }
    }

    if (!options.cidr_list_path.empty()) {
        cidrs_ = load_route_cidr_list(options.cidr_list_path, error);
        if (!error.empty()) {
            return false;
        }
    }

    if (domains_.empty() && cidrs_.empty()) {
        error = "智能分流列表为空";
        return false;
    }

    stop_requested_.store(false);
    apply_routes_once();

    worker_ = std::thread([this] { worker_main(); });
    error.clear();
    return true;
}

void SmartRouteManager::stop() noexcept {
    stop_requested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::thread{};
}

std::size_t SmartRouteManager::applied_route_count() const noexcept {
    return applied_route_count_.load();
}

void SmartRouteManager::worker_main() {
    while (!stop_requested_.load()) {
        const auto interval = options_.refresh_interval.count();
        for (int i = 0; i < interval && !stop_requested_.load(); ++i) {
            Sleep(1000);
        }
        if (stop_requested_.load()) {
            break;
        }
        apply_routes_once();
    }
}

void SmartRouteManager::apply_routes_once() {
    std::vector<std::string> cidrs;
    std::vector<std::string> domains;
    {
        std::lock_guard lock(mutex_);
        if (stop_requested_.load()) {
            return;
        }
        cidrs = cidrs_;
        domains = domains_;
    }

    for (const auto& cidr : cidrs) {
        if (stop_requested_.load()) {
            return;
        }
        apply_cidr_route(cidr);
    }

    for (const auto& domain : domains) {
        if (stop_requested_.load()) {
            return;
        }
        resolve_domain_and_apply(domain);
    }

    applied_route_count_.store(net_config_.selective_route_count());
}

void SmartRouteManager::resolve_domain_and_apply(const std::string& domain) {
    if (!ensure_winsock_once()) {
        return;
    }

    std::set<std::string> hosts;
    hosts.insert(domain);
    if (domain.rfind("*.", 0) == 0) {
        hosts.insert(domain.substr(2));
    } else {
        hosts.insert("www." + domain);
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    for (const auto& host : hosts) {
        addrinfo* result = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 ||
            result == nullptr) {
            continue;
        }

        for (addrinfo* entry = result; entry != nullptr; entry = entry->ai_next) {
            if (entry->ai_family != AF_INET || entry->ai_addr == nullptr) {
                continue;
            }

            const auto* addr =
                reinterpret_cast<const sockaddr_in*>(entry->ai_addr);
            const std::uint32_t host_order =
                ntohl(addr->sin_addr.S_un.S_addr);
            const std::string dotted = host_order_to_dotted(host_order);
            if (dotted.empty()) {
                continue;
            }

            std::string route_error;
            (void)net_config_.apply_prefix_route(
                options_.tunnel_luid,
                dotted,
                32,
                options_.gateway_ipv4,
                route_error);
        }

        freeaddrinfo(result);
    }
}

void SmartRouteManager::apply_cidr_route(const std::string& cidr_text) {
    std::string network;
    std::uint8_t prefix_length = 32;
    std::string parse_error;
    if (!parse_cidr_prefix(cidr_text, network, prefix_length, parse_error)) {
        return;
    }

    std::string route_error;
    (void)net_config_.apply_prefix_route(
        options_.tunnel_luid,
        network,
        prefix_length,
        options_.gateway_ipv4,
        route_error);
}

}  // namespace tunnel
