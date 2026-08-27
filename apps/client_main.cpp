#include "tunnel/console_utf8.hpp"
#include "tunnel/development_adapters.hpp"
#include "tunnel/tunnel_engine.hpp"

#ifdef TUNNEL_HAS_MSQUIC
#include "tunnel/quic_secure_transport.hpp"
#endif

#ifdef _WIN32
#include "tunnel/wintun_virtual_interface.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

#ifdef _WIN32
tunnel::TunnelEngine* g_engine_for_signal = nullptr;

bool is_process_elevated() {
    BOOL elevated = FALSE;
    PSID administrators_group = nullptr;
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(
            &authority,
            2,
            SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS,
            0,
            0,
            0,
            0,
            0,
            0,
            &administrators_group)) {
        CheckTokenMembership(nullptr, administrators_group, &elevated);
        FreeSid(administrators_group);
    }
    return elevated == TRUE;
}

BOOL WINAPI console_ctrl_handler(const DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT ||
        ctrl_type == CTRL_CLOSE_EVENT) {
        if (g_engine_for_signal != nullptr) {
            g_engine_for_signal->request_data_plane_stop();
        }
        return TRUE;
    }
    return FALSE;
}
#endif

std::string read_trimmed_file(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return {};
    }

    std::string content;
    std::getline(input, content);
    while (!content.empty() &&
           (content.back() == '\r' || content.back() == '\n' ||
            content.back() == ' ')) {
        content.pop_back();
    }
    return content;
}

std::unique_ptr<tunnel::SecureTransport> make_transport(const bool use_quic) {
    if (use_quic) {
#ifdef TUNNEL_HAS_MSQUIC
        return std::make_unique<tunnel::QuicSecureTransport>();
#else
        std::cerr << "MsQuic is not enabled in this build; cannot use --quic\n";
        return nullptr;
#endif
    }
    return std::make_unique<tunnel::DevelopmentSecureTransport>();
}

}  // namespace

int main(int argc, char** argv) {
    tunnel::init_console_utf8();

    bool use_wintun = false;
    bool use_quic = false;
    bool insecure = false;
    bool relay = false;
    int relay_seconds = 0;
    bool global_route = false;
    bool smart_route = false;
    std::string smart_domain_list;
    std::string smart_cidr_list;
    std::string cert_hash;
    std::string endpoint = "127.0.0.1";
    std::uint16_t port = 44333;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--wintun") {
            use_wintun = true;
        } else if (arg == "--quic") {
            use_quic = true;
        } else if (arg == "--insecure") {
            insecure = true;
        } else if (arg == "--relay") {
            relay = true;
        } else if (arg.rfind("--relay-seconds:", 0) == 0) {
            relay = true;
            relay_seconds = std::stoi(arg.substr(std::string("--relay-seconds:").size()));
        } else if (arg == "--global-route") {
            global_route = true;
        } else if (arg == "--smart-route") {
            smart_route = true;
        } else if (arg.rfind("--smart-domains:", 0) == 0) {
            smart_domain_list = arg.substr(std::string("--smart-domains:").size());
        } else if (arg.rfind("--smart-cidrs:", 0) == 0) {
            smart_cidr_list = arg.substr(std::string("--smart-cidrs:").size());
        } else if (arg.rfind("--cert_hash:", 0) == 0) {
            cert_hash = arg.substr(std::string("--cert_hash:").size());
        } else if (arg.rfind("--endpoint:", 0) == 0) {
            endpoint = arg.substr(std::string("--endpoint:").size());
        } else if (arg.rfind("--port:", 0) == 0) {
            port = static_cast<std::uint16_t>(
                std::stoi(arg.substr(std::string("--port:").size())));
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            return 1;
        }
    }

    if (global_route && smart_route) {
        std::cerr << "--global-route and --smart-route are mutually exclusive\n";
        return 1;
    }
    if (global_route && !use_wintun) {
        std::cerr << "--global-route requires --wintun\n";
        return 1;
    }
    if (global_route && !use_quic) {
        std::cerr << "--global-route requires --quic\n";
        return 1;
    }
    if (smart_route && !use_wintun) {
        std::cerr << "--smart-route requires --wintun\n";
        return 1;
    }
    if (smart_route && !use_quic) {
        std::cerr << "--smart-route requires --quic\n";
        return 1;
    }

#ifdef _WIN32
    if (use_wintun && !is_process_elevated()) {
        std::cerr << "ERROR: --wintun requires Administrator privileges.\n"
                  << "       Right-click PowerShell -> Run as administrator, then retry.\n";
        return 1;
    }
#endif

    tunnel::TunnelConfig config;
    config.role = tunnel::Role::client;
    config.endpoint = endpoint;
    config.port = port;
    config.mtu = 1280;
    config.prefer_quic = use_quic;
    config.quic.insecure = insecure;
#ifdef _WIN32
    if (global_route) {
        config.enable_global_default_route = true;
        config.client_routing_mode = tunnel::ClientRoutingMode::global;
    } else if (smart_route) {
        config.client_routing_mode = tunnel::ClientRoutingMode::smart;
    }
    if (!smart_domain_list.empty()) {
        config.smart_route_domain_list = smart_domain_list;
    }
    if (!smart_cidr_list.empty()) {
        config.smart_route_cidr_list = smart_cidr_list;
    }
#endif

    if (use_quic && cert_hash.empty()) {
        cert_hash = read_trimmed_file("certs/dev/thumbprint.txt");
    }
    config.quic.cert_hash = cert_hash;

    std::unique_ptr<tunnel::VirtualInterface> iface;
    tunnel::DevelopmentVirtualInterface* dev_iface = nullptr;
    tunnel::WintunVirtualInterface* wintun_view = nullptr;
#ifdef _WIN32
    if (use_wintun) {
        auto wintun = std::make_unique<tunnel::WintunVirtualInterface>(
            L"SecureTunnel", L"SecureTunnel", 0x400000, config.ipv4);
        wintun_view = wintun.get();
        iface = std::move(wintun);
    } else {
        auto dev = std::make_unique<tunnel::DevelopmentVirtualInterface>();
        dev_iface = dev.get();
        iface = std::move(dev);
    }
#else
    {
        auto dev = std::make_unique<tunnel::DevelopmentVirtualInterface>();
        dev_iface = dev.get();
        iface = std::move(dev);
    }
#endif

    auto transport = make_transport(use_quic);
    if (!transport) {
        return 1;
    }

    tunnel::SecureTransport* transport_view = transport.get();

    tunnel::TunnelEngine engine{
        config,
        std::move(iface),
        std::move(transport)};

#ifdef _WIN32
    const bool vpn_routing = global_route || smart_route;
    if (vpn_routing) {
        g_engine_for_signal = &engine;
        SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
    }
#endif

    if (!engine.start()) {
        std::cerr << "Client start failed";
#ifdef _WIN32
        if (use_wintun && wintun_view != nullptr && !wintun_view->last_error().empty()) {
            std::cerr << " [Wintun]: " << wintun_view->last_error();
        }
#endif
#ifdef TUNNEL_HAS_MSQUIC
        if (use_quic && transport_view != nullptr) {
            if (const auto* quic =
                    dynamic_cast<const tunnel::QuicSecureTransport*>(transport_view)) {
                if (!quic->last_error().empty()) {
                    std::cerr << " [QUIC]: " << quic->last_error();
                } else if (!use_wintun) {
                    std::cerr << " (start server first; ensure msquic.dll is next to exe)";
                }
            }
        }
#endif
        if (use_quic) {
            std::cerr << " (ensure tunnel_server.exe --quic --relay is running first)";
        }
#ifdef _WIN32
        if (use_wintun && (wintun_view == nullptr || wintun_view->last_error().empty())) {
            std::cerr << " (for --wintun: Administrator + wintun.dll next to exe)";
        }
#endif
        std::cerr << '\n';
        return 1;
    }

    std::cout << "[OK] Client state: established"
              << ", epoch: " << engine.session().epoch()
              << ", interface: " << (use_wintun ? "Wintun" : "dev-adapter")
              << ", transport: " << (use_quic ? "QUIC/TLS1.3" : "dev-adapter");
#ifdef _WIN32
    if (global_route) {
        std::cout << ", route: global -> " << config.default_route_gateway;
    } else if (smart_route) {
        std::cout << ", route: smart -> " << config.default_route_gateway;
    }
#endif
    std::cout << '\n';

    const bool run_relay = relay || global_route || smart_route;
    if (run_relay) {
        if (dev_iface != nullptr) {
            dev_iface->inject_inbound_packet({
                std::byte{0x45},
                std::byte{0x00},
                std::byte{0x00},
                std::byte{0x2A},
            });
        }

        if (global_route || smart_route) {
            const char* mode = global_route ? "global VPN" : "smart VPN";
            std::cout << "Data-plane relay running (" << mode
                      << ", Ctrl+C to stop)...\n";
            engine.run_data_plane_for(std::chrono::hours(24));
            std::cout << "Stopping client...\n";
        } else if (relay_seconds > 0) {
            std::cout << "Data-plane relay running (" << relay_seconds << " seconds)...\n";
            engine.run_data_plane_for(std::chrono::seconds(relay_seconds));
        } else {
            std::cout << "Data-plane relay running (5 seconds)...\n";
            engine.run_data_plane_for(std::chrono::seconds(5));
        }
    }

#ifdef _WIN32
    if (global_route || smart_route) {
        SetConsoleCtrlHandler(console_ctrl_handler, FALSE);
        g_engine_for_signal = nullptr;
    }
#endif

    engine.stop();
    tunnel::restore_console();
    return 0;
}
