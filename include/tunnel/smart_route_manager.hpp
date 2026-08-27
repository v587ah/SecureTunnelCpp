#pragma once

#include "tunnel/windows_net_config.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tunnel {

// 解析域名/CIDR 列表（可单独单元测试）。
[[nodiscard]] std::vector<std::string> load_route_domain_list(
    const std::string& path,
    std::string& error);

[[nodiscard]] bool parse_cidr_prefix(
    const std::string& text,
    std::string& network_ipv4,
    std::uint8_t& prefix_length,
    std::string& error);

[[nodiscard]] std::vector<std::string> load_route_cidr_list(
    const std::string& path,
    std::string& error);

// 智能分流：解析名单中的域名/CIDR，经隧道网关安装前缀路由并定期刷新。
class SmartRouteManager {
public:
    struct Options {
        std::uint64_t tunnel_luid{0};
        std::string gateway_ipv4{"10.66.66.1"};
        std::string domain_list_path;
        std::string cidr_list_path;
        std::chrono::seconds refresh_interval{300};
    };

    explicit SmartRouteManager(WindowsNetConfigurator& net_config);
    ~SmartRouteManager();

    SmartRouteManager(const SmartRouteManager&) = delete;
    SmartRouteManager& operator=(const SmartRouteManager&) = delete;

    [[nodiscard]] bool start(const Options& options, std::string& error);
    void stop() noexcept;

    [[nodiscard]] std::size_t applied_route_count() const noexcept;

private:
    void worker_main();
    void apply_routes_once();
    void resolve_domain_and_apply(const std::string& domain);
    void apply_cidr_route(const std::string& cidr_text);

    WindowsNetConfigurator& net_config_;
    Options options_{};
    std::vector<std::string> domains_;
    std::vector<std::string> cidrs_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<std::size_t> applied_route_count_{0};
    mutable std::mutex mutex_;
    std::thread worker_;
};

}  // namespace tunnel
