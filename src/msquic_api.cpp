#include "tunnel/msquic_api.hpp"

#include <msquic.h>

#include <mutex>

namespace tunnel {
namespace {

std::mutex g_msquic_mutex;
const QUIC_API_TABLE* g_msquic_api = nullptr;
int g_msquic_ref_count = 0;

}  // namespace

bool MsQuicApi::acquire(const void*& api_table) {
    std::lock_guard lock(g_msquic_mutex);
    if (g_msquic_api == nullptr) {
        const QUIC_API_TABLE* opened = nullptr;
        if (QUIC_FAILED(MsQuicOpen2(&opened)) || opened == nullptr) {
            api_table = nullptr;
            return false;
        }
        g_msquic_api = opened;
    }

    ++g_msquic_ref_count;
    api_table = g_msquic_api;
    return true;
}

void MsQuicApi::release() noexcept {
    std::lock_guard lock(g_msquic_mutex);
    if (g_msquic_ref_count <= 0) {
        return;
    }

    --g_msquic_ref_count;
    if (g_msquic_ref_count == 0 && g_msquic_api != nullptr) {
        MsQuicClose(g_msquic_api);
        g_msquic_api = nullptr;
    }
}

}  // namespace tunnel
