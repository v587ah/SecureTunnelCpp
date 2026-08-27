#include "tunnel/inner_session.hpp"

#include <array>
#include <cstring>
#include <limits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace tunnel {
namespace {

std::uint8_t hex_nibble(const char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<std::uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<std::uint8_t>(10 + c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<std::uint8_t>(10 + c - 'A');
    }
    return 0xFF;
}

bool decode_hex_key(const std::string& hex, std::array<std::uint8_t, kInnerKeySize>& key) {
    if (hex.size() != kInnerKeySize * 2) {
        return false;
    }

    for (std::size_t i = 0; i < key.size(); ++i) {
        const auto high = hex_nibble(hex[i * 2]);
        const auto low = hex_nibble(hex[i * 2 + 1]);
        if (high == 0xFF || low == 0xFF) {
            return false;
        }
        key[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

void write_u64_be(std::vector<std::byte>& out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFF));
    }
}

void write_u16_be(std::vector<std::byte>& out, std::uint16_t value) {
    out.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::byte>(value & 0xFF));
}

std::uint64_t read_u64_be(std::span<const std::byte> data, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<std::uint64_t>(data[offset + i]);
    }
    return value;
}

std::uint16_t read_u16_be(std::span<const std::byte> data, std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[offset]) << 8) |
        static_cast<std::uint16_t>(data[offset + 1]));
}

class DevelopmentInnerSession final : public InnerSession {
public:
    [[nodiscard]] bool is_ready() const noexcept override {
        return true;
    }

    bool seal(
        std::span<const std::byte> plaintext,
        std::vector<std::byte>& ciphertext) override {
        if (plaintext.size() > kInnerMaxPayloadSize) {
            last_error_ = "载荷超过内层帧上限";
            return false;
        }

        ciphertext.clear();
        ciphertext.reserve(1 + 8 + 2 + plaintext.size());
        ciphertext.push_back(static_cast<std::byte>(kInnerFrameVersionDev));
        write_u64_be(ciphertext, ++send_sequence_);
        write_u16_be(ciphertext, static_cast<std::uint16_t>(plaintext.size()));
        ciphertext.insert(ciphertext.end(), plaintext.begin(), plaintext.end());
        return true;
    }

    bool open(
        std::span<const std::byte> ciphertext,
        std::vector<std::byte>& plaintext) override {
        if (ciphertext.size() < 1 + 8 + 2) {
            last_error_ = "开发内层帧过短";
            return false;
        }

        if (static_cast<std::uint8_t>(ciphertext[0]) != kInnerFrameVersionDev) {
            last_error_ = "内层帧版本不匹配";
            return false;
        }

        const std::uint64_t sequence = read_u64_be(ciphertext, 1);
        if (sequence <= last_received_sequence_) {
            last_error_ = "检测到重放或乱序帧";
            return false;
        }

        const std::uint16_t payload_size = read_u16_be(ciphertext, 9);
        if (payload_size > kInnerMaxPayloadSize ||
            ciphertext.size() < 1 + 8 + 2 + payload_size) {
            last_error_ = "内层帧长度非法";
            return false;
        }

        last_received_sequence_ = sequence;
        plaintext.assign(
            ciphertext.begin() + 11,
            ciphertext.begin() + 11 + payload_size);
        return true;
    }

    [[nodiscard]] const std::string& last_error() const noexcept override {
        return last_error_;
    }

private:
    std::uint64_t send_sequence_{0};
    std::uint64_t last_received_sequence_{0};
    std::string last_error_;
};

#ifdef _WIN32
class BcryptInnerSession final : public InnerSession {
public:
    explicit BcryptInnerSession(std::array<std::uint8_t, kInnerKeySize> key)
        : key_(key) {}

    ~BcryptInnerSession() override {
        close_key();
    }

    bool initialize() {
        if (BCryptOpenAlgorithmProvider(
                &aes_alg_,
                BCRYPT_AES_ALGORITHM,
                nullptr,
                0) != 0) {
            last_error_ = "BCryptOpenAlgorithmProvider 失败";
            return false;
        }

        if (BCryptSetProperty(
                aes_alg_,
                BCRYPT_CHAINING_MODE,
                reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                sizeof(BCRYPT_CHAIN_MODE_GCM),
                0) != 0) {
            last_error_ = "无法设置 AES-GCM 模式";
            return false;
        }

        if (BCryptGenerateSymmetricKey(
                aes_alg_,
                &key_handle_,
                nullptr,
                0,
                key_.data(),
                static_cast<ULONG>(key_.size()),
                0) != 0) {
            last_error_ = "BCryptGenerateSymmetricKey 失败";
            return false;
        }

        return true;
    }

    [[nodiscard]] bool is_ready() const noexcept override {
        return key_handle_ != nullptr;
    }

    bool seal(
        std::span<const std::byte> plaintext,
        std::vector<std::byte>& ciphertext) override {
        if (!is_ready()) {
            last_error_ = "内层 AEAD 尚未初始化";
            return false;
        }
        if (plaintext.size() > kInnerMaxPayloadSize) {
            last_error_ = "载荷超过内层帧上限";
            return false;
        }

        std::vector<std::byte> plain_frame;
        plain_frame.reserve(2 + plaintext.size());
        write_u16_be(plain_frame, static_cast<std::uint16_t>(plaintext.size()));
        plain_frame.insert(plain_frame.end(), plaintext.begin(), plaintext.end());

        const std::uint64_t sequence = ++send_sequence_;
        std::array<std::uint8_t, kInnerNonceSize> nonce{};
        std::memcpy(nonce.data(), &sequence, sizeof(sequence));

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth_info;
        BCRYPT_INIT_AUTH_MODE_INFO(auth_info);
        auth_info.pbNonce = nonce.data();
        auth_info.cbNonce = static_cast<ULONG>(nonce.size());
        auth_info.pbTag = tag_.data();
        auth_info.cbTag = static_cast<ULONG>(tag_.size());

        ULONG cipher_len = 0;
        if (BCryptEncrypt(
                key_handle_,
                reinterpret_cast<PUCHAR>(plain_frame.data()),
                static_cast<ULONG>(plain_frame.size()),
                &auth_info,
                nullptr,
                0,
                reinterpret_cast<PUCHAR>(cipher_buffer_.data()),
                static_cast<ULONG>(cipher_buffer_.size()),
                &cipher_len,
                0) != 0) {
            last_error_ = "BCryptEncrypt 失败";
            return false;
        }

        ciphertext.clear();
        ciphertext.reserve(
            1 + 8 + kInnerNonceSize + cipher_len + kInnerTagSize);
        ciphertext.push_back(static_cast<std::byte>(kInnerFrameVersionAead));
        write_u64_be(ciphertext, sequence);
        for (const auto byte : nonce) {
            ciphertext.push_back(static_cast<std::byte>(byte));
        }
        for (std::size_t i = 0; i < cipher_len; ++i) {
            ciphertext.push_back(static_cast<std::byte>(cipher_buffer_[i]));
        }
        for (const auto byte : tag_) {
            ciphertext.push_back(static_cast<std::byte>(byte));
        }
        return true;
    }

    bool open(
        std::span<const std::byte> ciphertext,
        std::vector<std::byte>& plaintext) override {
        if (!is_ready()) {
            last_error_ = "内层 AEAD 尚未初始化";
            return false;
        }

        constexpr std::size_t header_size =
            1 + 8 + kInnerNonceSize + kInnerTagSize;
        if (ciphertext.size() < header_size + 2) {
            last_error_ = "AEAD 内层帧过短";
            return false;
        }

        if (static_cast<std::uint8_t>(ciphertext[0]) != kInnerFrameVersionAead) {
            last_error_ = "内层帧版本不匹配";
            return false;
        }

        const std::uint64_t sequence = read_u64_be(ciphertext, 1);
        if (sequence <= last_received_sequence_) {
            last_error_ = "检测到重放或乱序帧";
            return false;
        }

        const auto* nonce_ptr =
            reinterpret_cast<const std::uint8_t*>(ciphertext.data() + 9);
        const std::size_t cipher_size =
            ciphertext.size() - header_size;
        const auto* cipher_ptr =
            reinterpret_cast<const std::uint8_t*>(ciphertext.data() + 9 + kInnerNonceSize);
        const auto* tag_ptr = cipher_ptr + cipher_size;

        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth_info;
        BCRYPT_INIT_AUTH_MODE_INFO(auth_info);
        auth_info.pbNonce = const_cast<PUCHAR>(nonce_ptr);
        auth_info.cbNonce = static_cast<ULONG>(kInnerNonceSize);
        auth_info.pbTag = const_cast<PUCHAR>(tag_ptr);
        auth_info.cbTag = static_cast<ULONG>(kInnerTagSize);

        ULONG plain_len = 0;
        if (BCryptDecrypt(
                key_handle_,
                const_cast<PUCHAR>(cipher_ptr),
                static_cast<ULONG>(cipher_size),
                &auth_info,
                nullptr,
                0,
                reinterpret_cast<PUCHAR>(plain_buffer_.data()),
                static_cast<ULONG>(plain_buffer_.size()),
                &plain_len,
                0) != 0) {
            last_error_ = "BCryptDecrypt 失败（认证标签无效）";
            return false;
        }

        if (plain_len < 2) {
            last_error_ = "解密后帧过短";
            return false;
        }

        const std::span<const std::byte> plain_span(
            reinterpret_cast<const std::byte*>(plain_buffer_.data()),
            plain_len);
        const std::uint16_t payload_size = read_u16_be(plain_span, 0);
        if (payload_size > kInnerMaxPayloadSize ||
            static_cast<ULONG>(plain_len) < 2 + payload_size) {
            last_error_ = "解密后载荷长度非法";
            return false;
        }

        last_received_sequence_ = sequence;
        plaintext.assign(
            plain_span.begin() + 2,
            plain_span.begin() + 2 + payload_size);
        return true;
    }

    [[nodiscard]] const std::string& last_error() const noexcept override {
        return last_error_;
    }

private:
    void close_key() noexcept {
        if (key_handle_ != nullptr) {
            BCryptDestroyKey(key_handle_);
            key_handle_ = nullptr;
        }
        if (aes_alg_ != nullptr) {
            BCryptCloseAlgorithmProvider(aes_alg_, 0);
            aes_alg_ = nullptr;
        }
    }

    std::array<std::uint8_t, kInnerKeySize> key_{};
    BCRYPT_ALG_HANDLE aes_alg_{nullptr};
    BCRYPT_KEY_HANDLE key_handle_{nullptr};
    std::array<std::uint8_t, kInnerMaxPayloadSize + 2> plain_buffer_{};
    std::array<std::uint8_t, kInnerMaxPayloadSize + 2> cipher_buffer_{};
    std::array<std::uint8_t, kInnerTagSize> tag_{};
    std::uint64_t send_sequence_{0};
    std::uint64_t last_received_sequence_{0};
    std::string last_error_;
};
#endif

}  // namespace

std::unique_ptr<InnerSession> create_inner_session(const InnerSessionConfig& config) {
    if (config.psk_hex.empty()) {
        return std::make_unique<DevelopmentInnerSession>();
    }

    std::array<std::uint8_t, kInnerKeySize> key{};
    if (!decode_hex_key(config.psk_hex, key)) {
        return nullptr;
    }

#ifdef _WIN32
    auto session = std::make_unique<BcryptInnerSession>(key);
    if (!session->initialize()) {
        return nullptr;
    }
    return session;
#else
    (void)key;
    return std::make_unique<DevelopmentInnerSession>();
#endif
}

}  // namespace tunnel
