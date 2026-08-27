#include "tunnel/console_utf8.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <iostream>
#include <memory>
#include <streambuf>
#include <string>
#include <string_view>

namespace tunnel {
namespace {

#ifdef _WIN32
void write_utf8_to_handle(const HANDLE handle, const std::string_view text) {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE || text.empty()) {
        return;
    }

    DWORD console_mode = 0;
    if (GetConsoleMode(handle, &console_mode) != 0) {
        const int wide_length = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            nullptr,
            0);
        if (wide_length <= 0) {
            return;
        }

        std::wstring wide(static_cast<std::size_t>(wide_length), L'\0');
        MultiByteToWideChar(
            CP_UTF8,
            0,
            text.data(),
            static_cast<int>(text.size()),
            wide.data(),
            wide_length);

        DWORD written = 0;
        (void)WriteConsoleW(
            handle,
            wide.data(),
            static_cast<DWORD>(wide_length),
            &written,
            nullptr);
        return;
    }

    DWORD written = 0;
    (void)WriteFile(
        handle,
        text.data(),
        static_cast<DWORD>(text.size()),
        &written,
        nullptr);
}

class Utf8ConsoleStreambuf final : public std::streambuf {
public:
    explicit Utf8ConsoleStreambuf(const HANDLE handle) : handle_(handle) {}

protected:
    int overflow(const int character) override {
        if (character == EOF) {
            return EOF;
        }

        buffer_.push_back(static_cast<char>(character));
        if (character == '\n') {
            sync();
        }
        return character;
    }

    int sync() override {
        flush_buffer();
        return 0;
    }

private:
    void flush_buffer() {
        if (buffer_.empty()) {
            return;
        }
        write_utf8_to_handle(handle_, buffer_);
        buffer_.clear();
    }

    HANDLE handle_;
    std::string buffer_;
};

struct ConsoleState {
    std::streambuf* original_stdout{nullptr};
    std::streambuf* original_stderr{nullptr};
    std::unique_ptr<Utf8ConsoleStreambuf> stdout_buf;
    std::unique_ptr<Utf8ConsoleStreambuf> stderr_buf;
    bool installed{false};
};

ConsoleState& console_state() {
    static ConsoleState state;
    return state;
}
#endif

}  // namespace

void init_console_utf8() noexcept {
#ifdef _WIN32
    try {
        auto& state = console_state();
        if (state.installed) {
            return;
        }

        (void)SetConsoleOutputCP(CP_UTF8);
        (void)SetConsoleCP(CP_UTF8);

        const HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
        const HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);

        state.original_stdout = std::cout.rdbuf();
        state.original_stderr = std::cerr.rdbuf();

        state.stdout_buf =
            std::make_unique<Utf8ConsoleStreambuf>(stdout_handle);
        state.stderr_buf =
            std::make_unique<Utf8ConsoleStreambuf>(stderr_handle);

        std::cout.rdbuf(state.stdout_buf.get());
        std::cerr.rdbuf(state.stderr_buf.get());
        std::clog.rdbuf(state.stderr_buf.get());

        state.installed = true;
    } catch (...) {
        // 控制台初始化失败时不影响主流程。
    }
#else
    std::ios_base::sync_with_stdio(true);
#endif
}

void restore_console() noexcept {
#ifdef _WIN32
    try {
        std::cout.flush();
        std::cerr.flush();
        std::clog.flush();

        auto& state = console_state();
        if (state.original_stdout != nullptr) {
            std::cout.rdbuf(state.original_stdout);
        }
        if (state.original_stderr != nullptr) {
            std::cerr.rdbuf(state.original_stderr);
            std::clog.rdbuf(state.original_stderr);
        }

        state.stdout_buf.reset();
        state.stderr_buf.reset();
        state.original_stdout = nullptr;
        state.original_stderr = nullptr;
        state.installed = false;
    } catch (...) {
    }
#endif
}

}  // namespace tunnel
