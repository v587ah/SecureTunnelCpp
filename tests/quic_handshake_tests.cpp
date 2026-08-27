#include "tunnel/development_adapters.hpp"
#include "tunnel/quic_secure_transport.hpp"
#include "tunnel/tunnel_engine.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

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

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

#ifdef _WIN32
bool start_server_process(const std::string& server_exe, HANDLE& process_handle) {
    STARTUPINFOA startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};

    std::string command_line = "\"" + server_exe + "\" --quic --relay-seconds:30";
    if (!CreateProcessA(
            nullptr,
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup_info,
            &process_info)) {
        return false;
    }

    CloseHandle(process_info.hThread);
    process_handle = process_info.hProcess;
    return true;
}

void stop_server_process(HANDLE process_handle) {
    if (process_handle == nullptr || process_handle == INVALID_HANDLE_VALUE) {
        return;
    }
    TerminateProcess(process_handle, 0);
    WaitForSingleObject(process_handle, 5000);
    CloseHandle(process_handle);
}
#endif

}  // namespace

int main() {
    const std::string cert_hash = read_trimmed_file("certs/dev/thumbprint.txt");
    if (!expect(!cert_hash.empty(),
                "缺少 certs/dev/thumbprint.txt，请先运行 scripts/gen_dev_cert.ps1")) {
        return 1;
    }

#ifndef _WIN32
    std::cerr << "SKIP: quic_handshake_tests 目前仅支持 Windows\n";
    return 0;
#else
    const std::string server_exe = "build-agent/tunnel_server.exe";
    HANDLE server_process = nullptr;
    if (!expect(
            start_server_process(server_exe, server_process),
            "无法启动 QUIC 服务端子进程")) {
        return 1;
    }

    // 给服务端留出监听启动时间。
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    tunnel::TunnelConfig client_config;
    client_config.role = tunnel::Role::client;
    client_config.endpoint = "127.0.0.1";
    client_config.port = 44333;
    client_config.prefer_quic = true;
    client_config.quic.insecure = true;

    tunnel::TunnelEngine client{
        client_config,
        std::make_unique<tunnel::DevelopmentVirtualInterface>(),
        std::make_unique<tunnel::QuicSecureTransport>()};

    const bool client_started = client.start();
    client.stop();
    stop_server_process(server_process);

    if (!expect(client_started, "客户端 QUIC/TLS 握手失败")) {
        return 1;
    }

    std::cout << "quic_handshake_tests passed\n";
    return 0;
#endif
}
