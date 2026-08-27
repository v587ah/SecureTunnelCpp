#include "tunnel/wintun_loader.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <filesystem>

namespace tunnel {
namespace {

template <typename T>
bool resolve(HMODULE module, const char* name, T*& out, std::string& error) {
    out = reinterpret_cast<T*>(GetProcAddress(module, name));
    if (out == nullptr) {
        error = std::string("缺少导出函数: ") + name;
        return false;
    }
    return true;
}

std::wstring exe_directory() {
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return L".";
    }
    return std::filesystem::path(buffer.data()).parent_path().wstring();
}

}  // namespace

std::string format_win32_error(const unsigned long error_code) {
    if (error_code == 0) {
        return "无错误";
    }

    LPSTR message = nullptr;
    const DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&message),
        0,
        nullptr);

    std::string text = "Win32 错误 " + std::to_string(error_code);
    if (size != 0 && message != nullptr) {
        text.append(": ");
        text.append(message);
        while (!text.empty() &&
               (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
            text.pop_back();
        }
    }
    if (message != nullptr) {
        LocalFree(message);
    }
    return text;
}

WintunLoader::~WintunLoader() {
    unload();
}

bool WintunLoader::load(const std::wstring& dll_path) {
    unload();

    std::array<std::wstring, 3> candidates{};
    if (!dll_path.empty()) {
        candidates[0] = dll_path;
    } else {
        candidates[0] = exe_directory() + L"\\wintun.dll";
        candidates[1] = exe_directory() + L"\\..\\..\\third_party\\wintun\\wintun.dll";
        candidates[2] = L"wintun.dll";
    }

    HMODULE module = nullptr;
    for (const auto& candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        module = LoadLibraryW(candidate.c_str());
        if (module != nullptr) {
            break;
        }
    }

    if (module == nullptr) {
        last_error_ = "无法加载 wintun.dll。" + format_win32_error(GetLastError()) +
                      " 请确认 DLL 与可执行文件同目录。";
        return false;
    }

    // 按官方示例解析全部必要导出，任一缺失都视为加载失败。
    if (!resolve(module, "WintunCreateAdapter", create_adapter, last_error_) ||
        !resolve(module, "WintunCloseAdapter", close_adapter, last_error_) ||
        !resolve(module, "WintunOpenAdapter", open_adapter, last_error_) ||
        !resolve(module, "WintunGetAdapterLUID", get_adapter_luid, last_error_) ||
        !resolve(
            module,
            "WintunGetRunningDriverVersion",
            get_running_driver_version,
            last_error_) ||
        !resolve(module, "WintunDeleteDriver", delete_driver, last_error_) ||
        !resolve(module, "WintunSetLogger", set_logger, last_error_) ||
        !resolve(module, "WintunStartSession", start_session, last_error_) ||
        !resolve(module, "WintunEndSession", end_session, last_error_) ||
        !resolve(module, "WintunGetReadWaitEvent", get_read_wait_event, last_error_) ||
        !resolve(module, "WintunReceivePacket", receive_packet, last_error_) ||
        !resolve(
            module, "WintunReleaseReceivePacket", release_receive_packet, last_error_) ||
        !resolve(module, "WintunAllocateSendPacket", allocate_send_packet, last_error_) ||
        !resolve(module, "WintunSendPacket", send_packet, last_error_)) {
        FreeLibrary(module);
        return false;
    }

    module_ = module;
    last_error_.clear();
    return true;
}

void WintunLoader::unload() noexcept {
    create_adapter = nullptr;
    close_adapter = nullptr;
    open_adapter = nullptr;
    get_adapter_luid = nullptr;
    get_running_driver_version = nullptr;
    delete_driver = nullptr;
    set_logger = nullptr;
    start_session = nullptr;
    end_session = nullptr;
    get_read_wait_event = nullptr;
    receive_packet = nullptr;
    release_receive_packet = nullptr;
    allocate_send_packet = nullptr;
    send_packet = nullptr;

    if (module_ != nullptr) {
        FreeLibrary(static_cast<HMODULE>(module_));
        module_ = nullptr;
    }
}

bool WintunLoader::is_loaded() const noexcept {
    return module_ != nullptr;
}

std::uint32_t WintunLoader::running_driver_version() const noexcept {
    if (!is_loaded() || get_running_driver_version == nullptr) {
        return 0;
    }
    return get_running_driver_version();
}

const std::string& WintunLoader::last_error() const noexcept {
    return last_error_;
}

}  // namespace tunnel
