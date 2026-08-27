#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace tunnel {

constexpr std::uint8_t kInnerFrameVersionDev = 0xFE;
constexpr std::uint8_t kInnerFrameVersionAead = 0x01;
constexpr std::size_t kInnerKeySize = 32;
constexpr std::size_t kInnerNonceSize = 12;
constexpr std::size_t kInnerTagSize = 16;
constexpr std::size_t kInnerMaxPayloadSize = 2048;

// 内层会话配置。生产环境应通过安全通道分发 PSK，不应明文写入配置文件。
struct InnerSessionConfig {
    // 64 位十六进制 = 32 字节 AES-256 密钥；为空时使用开发模式（无 AEAD）。
    std::string psk_hex;
};

// 内层安全会话抽象：在 QUIC/TLS 之上提供 AEAD 封装与单调序号防重放。
class InnerSession {
public:
    virtual ~InnerSession() = default;

    [[nodiscard]] virtual bool is_ready() const noexcept = 0;
    [[nodiscard]] virtual bool seal(
        std::span<const std::byte> plaintext,
        std::vector<std::byte>& ciphertext) = 0;
    [[nodiscard]] virtual bool open(
        std::span<const std::byte> ciphertext,
        std::vector<std::byte>& plaintext) = 0;
    [[nodiscard]] virtual const std::string& last_error() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<InnerSession> create_inner_session(
    const InnerSessionConfig& config);

}  // namespace tunnel
