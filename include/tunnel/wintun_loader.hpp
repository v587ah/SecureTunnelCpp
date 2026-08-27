#pragma once

#include <cstdint>
#include <string>

#include <wintun.h>

namespace tunnel {

// Wintun API 动态加载器。
// 负责：找到 wintun.dll、解析导出函数、查询驱动版本。
// 不负责：创建网卡、改路由、加 IP（这些由 WintunVirtualInterface 完成）。
class WintunLoader {
public:
    WintunLoader() = default;
    ~WintunLoader();

    WintunLoader(const WintunLoader&) = delete;
    WintunLoader& operator=(const WintunLoader&) = delete;

    // dll_path 为空时，依次尝试：可执行文件同目录、third_party/wintun、系统 PATH。
    [[nodiscard]] bool load(const std::wstring& dll_path = L"");
    void unload() noexcept;
    [[nodiscard]] bool is_loaded() const noexcept;

    // 驱动已安装时返回版本号；未安装或调用失败返回 0。
    [[nodiscard]] std::uint32_t running_driver_version() const noexcept;

    [[nodiscard]] const std::string& last_error() const noexcept;

    WINTUN_CREATE_ADAPTER_FUNC* create_adapter{nullptr};
    WINTUN_CLOSE_ADAPTER_FUNC* close_adapter{nullptr};
    WINTUN_OPEN_ADAPTER_FUNC* open_adapter{nullptr};
    WINTUN_GET_ADAPTER_LUID_FUNC* get_adapter_luid{nullptr};
    WINTUN_GET_RUNNING_DRIVER_VERSION_FUNC* get_running_driver_version{nullptr};
    WINTUN_DELETE_DRIVER_FUNC* delete_driver{nullptr};
    WINTUN_SET_LOGGER_FUNC* set_logger{nullptr};
    WINTUN_START_SESSION_FUNC* start_session{nullptr};
    WINTUN_END_SESSION_FUNC* end_session{nullptr};
    WINTUN_GET_READ_WAIT_EVENT_FUNC* get_read_wait_event{nullptr};
    WINTUN_RECEIVE_PACKET_FUNC* receive_packet{nullptr};
    WINTUN_RELEASE_RECEIVE_PACKET_FUNC* release_receive_packet{nullptr};
    WINTUN_ALLOCATE_SEND_PACKET_FUNC* allocate_send_packet{nullptr};
    WINTUN_SEND_PACKET_FUNC* send_packet{nullptr};

private:
    void* module_{nullptr};  // HMODULE，头文件里用 void* 避免到处包含 windows.h
    std::string last_error_;
};

// 把 Windows 错误码转成可读说明，便于排障。
[[nodiscard]] std::string format_win32_error(unsigned long error_code);

}  // namespace tunnel
