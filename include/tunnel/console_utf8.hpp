#pragma once

namespace tunnel {

// 在 main() 第一行调用。
// Windows 下将 std::cout / std::cerr 重定向为 WriteConsoleW 输出 UTF-8，避免 cmd/VS 乱码。
void init_console_utf8() noexcept;

// 进程退出前调用，避免自定义 streambuf 在析构时崩溃。
void restore_console() noexcept;

}  // namespace tunnel
