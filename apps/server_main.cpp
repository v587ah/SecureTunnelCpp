#include "tunnel/console_utf8.hpp"
#include "tunnel/development_adapters.hpp"
#include "tunnel/tunnel_engine.hpp"

#ifdef TUNNEL_HAS_MSQUIC
#include "tunnel/quic_secure_transport.hpp"
#endif

#ifndef _WIN32
#include "tunnel/linux_tun_virtual_interface.hpp"
#endif

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

tunnel::TunnelEngine* g_engine = nullptr;

void on_stop_signal(int /*signum*/) {
    if (g_engine != nullptr) {
        g_engine->stop();
    }
}

std::string read_trimmed_file(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return {};
    }

    std::string content;
    std::getline(input, content);
    while (!content.empty() &&
           (content.back() == '\r' || content.back() == '\n' || content.back() == ' ')) {
        content.pop_back();
    }
    return content;
}

std::string env_or_empty(const char* name) {
#ifdef _WIN32
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) {
        free(buffer);
        return {};
    }
    std::string value(buffer);
    free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
#endif
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

std::unique_ptr<tunnel::VirtualInterface> make_interface(const bool use_tun) {
#ifndef _WIN32
    if (use_tun) {
        return std::make_unique<tunnel::LinuxTunVirtualInterface>("sectun0");
    }
#endif
    (void)use_tun;
    return std::make_unique<tunnel::DevelopmentVirtualInterface>();
}

}  // namespace

int main(int argc, char** argv) {
    tunnel::init_console_utf8();

    bool use_quic = false;
    bool use_tun = false;
    bool relay = false;
    int relay_seconds = 0;
    std::string cert_hash;
    std::string cert_file;
    std::string key_file;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--quic") {
            use_quic = true;
        } else if (arg == "--tun") {
            use_tun = true;
        } else if (arg == "--relay") {
            relay = true;
        } else if (arg.rfind("--relay-seconds:", 0) == 0) {
            relay = true;
            relay_seconds = std::stoi(arg.substr(std::string("--relay-seconds:").size()));
        } else if (arg.rfind("--cert_hash:", 0) == 0) {
            cert_hash = arg.substr(std::string("--cert_hash:").size());
        } else if (arg.rfind("--cert_file:", 0) == 0) {
            cert_file = arg.substr(std::string("--cert_file:").size());
        } else if (arg.rfind("--key_file:", 0) == 0) {
            key_file = arg.substr(std::string("--key_file:").size());
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            return 1;
        }
    }

    tunnel::TunnelConfig config;
    config.role = tunnel::Role::server;
    config.endpoint = "0.0.0.0";
    config.port = 44333;
    config.prefer_quic = use_quic;

    if (use_quic) {
        if (cert_file.empty()) {
            cert_file = env_or_empty("TLS_CERT_FILE");
        }
        if (key_file.empty()) {
            key_file = env_or_empty("TLS_KEY_FILE");
        }
        if (cert_hash.empty()) {
            cert_hash = read_trimmed_file("certs/dev/thumbprint.txt");
        }

#ifndef _WIN32
        if (!cert_file.empty() && !key_file.empty()) {
            config.quic.cert_file = cert_file;
            config.quic.key_file = key_file;
        } else
#endif
        if (!cert_hash.empty()) {
            config.quic.cert_hash = cert_hash;
        } else {
        std::cerr << "QUIC server requires --cert_file/--key_file (Linux) "
                  << "or --cert_hash (Windows)\n";
            return 1;
        }
    }

    auto transport = make_transport(use_quic);
    if (!transport) {
        return 1;
    }

    tunnel::SecureTransport* transport_view = transport.get();

    tunnel::TunnelEngine engine{
        config,
        make_interface(use_tun),
        std::move(transport)};

    g_engine = &engine;
    std::signal(SIGINT, on_stop_signal);
    std::signal(SIGTERM, on_stop_signal);

    if (use_quic) {
        std::cout << "QUIC server listening on UDP " << config.port
                  << ", waiting for client...\n";
    }

    if (!engine.start()) {
        std::cerr << "Server start failed";
#ifdef TUNNEL_HAS_MSQUIC
        if (use_quic && transport_view != nullptr) {
            if (const auto* quic =
                    dynamic_cast<const tunnel::QuicSecureTransport*>(transport_view)) {
                std::cerr << ": " << quic->last_error();
            } else {
                std::cerr << " (check msquic.dll and certificate)";
            }
        }
#endif
        std::cerr << '\n';
        g_engine = nullptr;
        return 1;
    }

    if (engine.session().state() == tunnel::SessionState::handshaking) {
        std::cout << "[OK] Server listening, start client in another window\n";
    } else {
        std::cout << "[OK] Server state: established"
                  << ", transport: " << (use_quic ? "QUIC/TLS1.3" : "dev-adapter")
                  << ", interface: " << (use_tun ? "Linux-TUN" : "dev-adapter")
                  << '\n';
    }

    if (relay) {
        if (relay_seconds > 0) {
            std::cout << "Data-plane relay running for " << relay_seconds
                      << " seconds...\n";
            engine.run_data_plane_for(std::chrono::seconds(relay_seconds));
        } else {
            std::cout << "Data-plane relay running (Ctrl+C / SIGTERM to stop)...\n";
            engine.run_data_plane_for(std::chrono::hours(24));
        }
        const auto& stats = engine.relay_stats();
        std::cout << "Relay stats: outbound " << stats.outbound_packets
                  << " packets, inbound " << stats.inbound_packets << " packets\n";
    }

    engine.stop();
    g_engine = nullptr;
    return 0;
}
