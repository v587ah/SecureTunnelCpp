#pragma once

namespace tunnel {

// MsQuic API 进程级单例加载器（引用计数）。
// 同一进程内允许多个 QuicSecureTransport 并存，最后一个关闭时才 MsQuicClose。
class MsQuicApi {
public:
    [[nodiscard]] static bool acquire(const void*& api_table);
    static void release() noexcept;

    MsQuicApi(const MsQuicApi&) = delete;
    MsQuicApi& operator=(const MsQuicApi&) = delete;

private:
    MsQuicApi() = default;
};

}  // namespace tunnel
