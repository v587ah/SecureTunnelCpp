#include "tunnel/quic_secure_transport.hpp"

#include "tunnel/msquic_api.hpp"

#include <msquic.h>

#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <utility>
#include <vector>

namespace tunnel {
namespace {

constexpr char kRegistrationName[] = "SecureTunnel";
constexpr std::chrono::milliseconds kHandshakeTimeout{15000};
constexpr std::chrono::milliseconds kStreamReadyTimeout{10000};

struct HandshakeState {
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> connected{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> shutdown_complete{false};
    std::atomic<unsigned> status{0};
};

struct StreamChannel {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::vector<std::uint8_t>> frames;
    std::vector<std::uint8_t> reassembly;
    std::atomic<bool> ready{false};
    std::atomic<bool> send_complete{true};
    HQUIC stream{nullptr};
    const QUIC_API_TABLE* msquic{nullptr};
};

struct ConnectionContext {
    HandshakeState* handshake{nullptr};
    StreamChannel* stream{nullptr};
    Role role{Role::client};
    const QUIC_API_TABLE* msquic{nullptr};
    HQUIC* connection_out{nullptr};
};

struct ServerContext {
    HandshakeState* handshake{nullptr};
    StreamChannel* stream{nullptr};
    const QUIC_API_TABLE* msquic{nullptr};
    HQUIC configuration{nullptr};
    HQUIC* connection_out{nullptr};
    ConnectionContext* connection_context{nullptr};
};

struct StoredCredentials {
    QUIC_CREDENTIAL_CONFIG config{};
    QUIC_CERTIFICATE_HASH cert_hash{};
    QUIC_CERTIFICATE_HASH_STORE cert_hash_store{};
    QUIC_CERTIFICATE_FILE cert_file{};
};

std::uint8_t decode_hex_nibble(const char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<std::uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<std::uint8_t>(10 + c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<std::uint8_t>(10 + c - 'A');
    }
    return 0;
}

bool decode_hex_buffer(
    const std::string& hex,
    std::span<std::uint8_t> out,
    std::size_t& written) {
    if (hex.size() % 2 != 0 || hex.size() / 2 > out.size()) {
        return false;
    }

    written = hex.size() / 2;
    for (std::size_t i = 0; i < written; ++i) {
        out[i] = static_cast<std::uint8_t>(
            (decode_hex_nibble(hex[i * 2]) << 4) |
            decode_hex_nibble(hex[i * 2 + 1]));
    }
    return true;
}

const QUIC_API_TABLE* api(const void* msquic) {
    return static_cast<const QUIC_API_TABLE*>(msquic);
}

HQUIC handle(void* value) {
    return static_cast<HQUIC>(value);
}

void append_received_bytes(
    StreamChannel* channel,
    const QUIC_BUFFER* buffers,
    uint32_t buffer_count) {
    for (uint32_t i = 0; i < buffer_count; ++i) {
        channel->reassembly.insert(
            channel->reassembly.end(),
            buffers[i].Buffer,
            buffers[i].Buffer + buffers[i].Length);
    }

    while (channel->reassembly.size() >= 4) {
        const std::uint32_t frame_size =
            (static_cast<std::uint32_t>(channel->reassembly[0]) << 24) |
            (static_cast<std::uint32_t>(channel->reassembly[1]) << 16) |
            (static_cast<std::uint32_t>(channel->reassembly[2]) << 8) |
            static_cast<std::uint32_t>(channel->reassembly[3]);
        if (frame_size == 0 || frame_size > 65536) {
            channel->reassembly.clear();
            break;
        }
        if (channel->reassembly.size() < 4 + frame_size) {
            break;
        }

        std::vector<std::uint8_t> frame(
            channel->reassembly.begin() + 4,
            channel->reassembly.begin() + 4 + frame_size);
        channel->frames.push_back(std::move(frame));
        channel->reassembly.erase(
            channel->reassembly.begin(),
            channel->reassembly.begin() + 4 + frame_size);
    }
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS QUIC_API stream_callback(
    HQUIC /*stream*/,
    void* context,
    QUIC_STREAM_EVENT* event);

bool open_client_stream(HQUIC connection, ConnectionContext* context) {
    if (context == nullptr || context->stream == nullptr || context->msquic == nullptr) {
        return false;
    }

    HQUIC stream = nullptr;
    if (QUIC_FAILED(context->msquic->StreamOpen(
            connection,
            QUIC_STREAM_OPEN_FLAG_NONE,
            stream_callback,
            context->stream,
            &stream))) {
        return false;
    }

    context->stream->stream = stream;
    context->stream->msquic = context->msquic;
    if (QUIC_FAILED(context->msquic->StreamStart(stream, QUIC_STREAM_START_FLAG_NONE))) {
        context->msquic->StreamClose(stream);
        context->stream->stream = nullptr;
        return false;
    }

    context->stream->ready.store(true, std::memory_order_release);
    context->stream->cv.notify_all();
    return true;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_STREAM_CALLBACK)
QUIC_STATUS QUIC_API stream_callback(
    HQUIC /*stream*/,
    void* context,
    QUIC_STREAM_EVENT* event) {
    auto* channel = static_cast<StreamChannel*>(context);
    if (channel == nullptr || event == nullptr) {
        return QUIC_STATUS_SUCCESS;
    }

    switch (event->Type) {
        case QUIC_STREAM_EVENT_RECEIVE:
            {
                std::lock_guard lock(channel->mutex);
                append_received_bytes(
                    channel,
                    event->RECEIVE.Buffers,
                    event->RECEIVE.BufferCount);
                channel->cv.notify_all();
            }
            break;
        case QUIC_STREAM_EVENT_SEND_COMPLETE:
            channel->send_complete.store(true, std::memory_order_release);
            channel->cv.notify_all();
            break;
        default:
            break;
    }

    return QUIC_STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_Function_class_(QUIC_CONNECTION_CALLBACK)
QUIC_STATUS QUIC_API connection_callback(
    HQUIC connection,
    void* context,
    QUIC_CONNECTION_EVENT* event) {
    auto* ctx = static_cast<ConnectionContext*>(context);
    if (ctx == nullptr || event == nullptr || ctx->handshake == nullptr) {
        return QUIC_STATUS_SUCCESS;
    }

    switch (event->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED:
            ctx->handshake->connected.store(true, std::memory_order_release);
            ctx->handshake->cv.notify_all();
            if (ctx->role == Role::client) {
                open_client_stream(connection, ctx);
            }
            break;
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
            if (ctx->stream != nullptr && ctx->msquic != nullptr) {
                ctx->msquic->SetCallbackHandler(
                    event->PEER_STREAM_STARTED.Stream,
                    reinterpret_cast<void*>(stream_callback),
                    ctx->stream);
                ctx->stream->stream = event->PEER_STREAM_STARTED.Stream;
                ctx->stream->msquic = ctx->msquic;
                ctx->stream->ready.store(true, std::memory_order_release);
                ctx->stream->cv.notify_all();
            }
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
            if (!ctx->handshake->connected.load(std::memory_order_acquire)) {
                ctx->handshake->status.store(
                    static_cast<unsigned>(event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status),
                    std::memory_order_release);
                ctx->handshake->failed.store(true, std::memory_order_release);
                ctx->handshake->cv.notify_all();
            }
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            if (ctx->handshake != nullptr) {
                ctx->handshake->shutdown_complete.store(true, std::memory_order_release);
                ctx->handshake->cv.notify_all();
            }
            break;
        default:
            break;
    }

    return QUIC_STATUS_SUCCESS;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(QUIC_LISTENER_CALLBACK)
QUIC_STATUS QUIC_API listener_callback(
    HQUIC /*listener*/,
    void* context,
    QUIC_LISTENER_EVENT* event) {
    auto* server = static_cast<ServerContext*>(context);
    if (server == nullptr || event == nullptr) {
        return QUIC_STATUS_NOT_SUPPORTED;
    }

    if (event->Type != QUIC_LISTENER_EVENT_NEW_CONNECTION) {
        return QUIC_STATUS_SUCCESS;
    }

    if (server->connection_out != nullptr) {
        *server->connection_out = event->NEW_CONNECTION.Connection;
    }

    server->msquic->SetCallbackHandler(
        event->NEW_CONNECTION.Connection,
        reinterpret_cast<void*>(connection_callback),
        server->connection_context);

    return server->msquic->ConnectionSetConfiguration(
        event->NEW_CONNECTION.Connection,
        server->configuration);
}

}  // namespace

QuicSecureTransport::~QuicSecureTransport() {
    close();
}

const std::string& QuicSecureTransport::last_error() const noexcept {
    return last_error_;
}

void QuicSecureTransport::set_error(std::string message) {
    last_error_ = std::move(message);
}

bool QuicSecureTransport::ensure_api_open() {
    if (api_open_) {
        return true;
    }

    const void* table = nullptr;
    if (!MsQuicApi::acquire(table) || table == nullptr) {
        set_error("MsQuicOpen2 失败，请确认 msquic.dll 位于可执行文件目录");
        return false;
    }

    msquic_ = table;
    api_open_ = true;
    return true;
}

bool QuicSecureTransport::open_registration() {
    if (!ensure_api_open()) {
        return false;
    }

    const QUIC_REGISTRATION_CONFIG reg_config{
        kRegistrationName,
        QUIC_EXECUTION_PROFILE_LOW_LATENCY,
    };

    HQUIC registration = nullptr;
    if (QUIC_FAILED(api(msquic_)->RegistrationOpen(
            &reg_config,
            &registration))) {
        set_error("RegistrationOpen 失败");
        return false;
    }

    registration_ = registration;
    return true;
}

bool QuicSecureTransport::load_configuration() {
    if (registration_ == nullptr) {
        set_error("MsQuic 注册对象尚未创建");
        return false;
    }

    QUIC_BUFFER alpn_buffer{
        static_cast<uint32_t>(config_.quic.alpn.size()),
        reinterpret_cast<uint8_t*>(config_.quic.alpn.data()),
    };

    QUIC_SETTINGS settings{};
    settings.IdleTimeoutMs = config_.quic.idle_timeout_ms;
    settings.IsSet.IdleTimeoutMs = TRUE;
    settings.PeerBidiStreamCount = 1;
    settings.IsSet.PeerBidiStreamCount = TRUE;

    if (config_.role == Role::server) {
        settings.ServerResumptionLevel = QUIC_SERVER_NO_RESUME;
        settings.IsSet.ServerResumptionLevel = TRUE;
    }

    HQUIC configuration = nullptr;
    if (QUIC_FAILED(api(msquic_)->ConfigurationOpen(
            handle(registration_),
            &alpn_buffer,
            1,
            &settings,
            sizeof(settings),
            nullptr,
            &configuration))) {
        set_error("ConfigurationOpen 失败");
        return false;
    }

    delete static_cast<StoredCredentials*>(stored_credentials_);
    stored_credentials_ = new StoredCredentials{};
    auto* creds = static_cast<StoredCredentials*>(stored_credentials_);

    if (config_.role == Role::client) {
        creds->config.Type = QUIC_CREDENTIAL_TYPE_NONE;
        creds->config.Flags = QUIC_CREDENTIAL_FLAG_CLIENT;
        if (config_.quic.insecure) {
            creds->config.Flags |= QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
        }
    } else if (!config_.quic.cert_hash.empty()) {
        std::array<std::uint8_t, 20> hash_bytes{};
        std::size_t written = 0;
        if (!decode_hex_buffer(config_.quic.cert_hash, hash_bytes, written) ||
            written != hash_bytes.size()) {
            set_error("证书指纹格式无效，应为 40 位十六进制 SHA1");
            return false;
        }
        std::memcpy(creds->cert_hash_store.ShaHash, hash_bytes.data(), hash_bytes.size());
        creds->cert_hash_store.Flags = QUIC_CERTIFICATE_HASH_STORE_FLAG_NONE;
        std::memcpy(
            creds->cert_hash_store.StoreName,
            "My",
            sizeof(creds->cert_hash_store.StoreName));
        creds->cert_hash_store.StoreName[sizeof(creds->cert_hash_store.StoreName) - 1] = '\0';
        creds->config.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_HASH_STORE;
        creds->config.CertificateHashStore = &creds->cert_hash_store;
    } else {
        creds->cert_file.CertificateFile =
            const_cast<char*>(config_.quic.cert_file.c_str());
        creds->cert_file.PrivateKeyFile =
            const_cast<char*>(config_.quic.key_file.c_str());
        creds->config.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
        creds->config.CertificateFile = &creds->cert_file;
    }

    if (QUIC_FAILED(api(msquic_)->ConfigurationLoadCredential(
            configuration,
            &creds->config))) {
        set_error("ConfigurationLoadCredential 失败，请检查证书配置");
        return false;
    }

    configuration_ = configuration;
    return true;
}

bool QuicSecureTransport::start_client() {
    auto* handshake = new HandshakeState();
    handshake_state_ = handshake;
    stream_channel_ = new StreamChannel();
    connection_context_ = new ConnectionContext{
        .handshake = handshake,
        .stream = static_cast<StreamChannel*>(stream_channel_),
        .role = Role::client,
        .msquic = api(msquic_),
        .connection_out = reinterpret_cast<HQUIC*>(&connection_),
    };

    HQUIC connection = nullptr;
    if (QUIC_FAILED(api(msquic_)->ConnectionOpen(
            handle(registration_),
            connection_callback,
            connection_context_,
            &connection))) {
        set_error("ConnectionOpen 失败");
        return false;
    }

    connection_ = connection;
    return true;
}

bool QuicSecureTransport::start_server_listener() {
    auto* handshake = new HandshakeState();
    handshake_state_ = handshake;
    stream_channel_ = new StreamChannel();
    connection_context_ = new ConnectionContext{
        .handshake = handshake,
        .stream = static_cast<StreamChannel*>(stream_channel_),
        .role = Role::server,
        .msquic = api(msquic_),
        .connection_out = reinterpret_cast<HQUIC*>(&connection_),
    };
    server_context_ = new ServerContext{
        .handshake = handshake,
        .stream = static_cast<StreamChannel*>(stream_channel_),
        .msquic = api(msquic_),
        .configuration = handle(configuration_),
        .connection_out = reinterpret_cast<HQUIC*>(&connection_),
        .connection_context = static_cast<ConnectionContext*>(connection_context_),
    };

    HQUIC listener = nullptr;
    if (QUIC_FAILED(api(msquic_)->ListenerOpen(
            handle(registration_),
            listener_callback,
            server_context_,
            &listener))) {
        set_error("ListenerOpen 失败");
        return false;
    }

    QUIC_ADDR address{};
    QuicAddrSetFamily(&address, QUIC_ADDRESS_FAMILY_UNSPEC);
    QuicAddrSetPort(&address, config_.port);

    QUIC_BUFFER alpn_buffer{
        static_cast<uint32_t>(config_.quic.alpn.size()),
        reinterpret_cast<uint8_t*>(config_.quic.alpn.data()),
    };

    if (QUIC_FAILED(api(msquic_)->ListenerStart(
            listener,
            &alpn_buffer,
            1,
            &address))) {
        api(msquic_)->ListenerClose(listener);
        set_error("ListenerStart 失败，端口可能被占用");
        return false;
    }

    listener_ = listener;
    listener_started_ = true;
    return true;
}

bool QuicSecureTransport::wait_for_handshake(
    const std::chrono::milliseconds timeout) {
    auto* state = static_cast<HandshakeState*>(handshake_state_);
    if (state == nullptr) {
        set_error("握手状态未初始化");
        return false;
    }

    std::unique_lock lock(state->mutex);
    const bool done = state->cv.wait_for(lock, timeout, [&] {
        return state->connected.load(std::memory_order_acquire) ||
               state->failed.load(std::memory_order_acquire);
    });

    if (!done) {
        set_error("QUIC/TLS 握手超时");
        return false;
    }

    if (state->failed.load(std::memory_order_acquire)) {
        set_error("QUIC/TLS 握手失败，状态码 0x" +
                  std::to_string(state->status.load()));
        return false;
    }

    return state->connected.load(std::memory_order_acquire);
}

bool QuicSecureTransport::connect(const TunnelConfig& config) {
    close();

    config_ = config;
    if (!config_.prefer_quic) {
        set_error("QuicSecureTransport 需要 prefer_quic=true");
        return false;
    }

    if (!open_registration() || !load_configuration()) {
        close();
        return false;
    }

    if (config_.role == Role::client) {
        if (!start_client()) {
            close();
            return false;
        }
    } else if (!start_server_listener()) {
        close();
        return false;
    }

    return true;
}

bool QuicSecureTransport::perform_handshake() {
    if (registration_ == nullptr || configuration_ == nullptr) {
        set_error("传输层尚未 connect");
        return false;
    }

    if (config_.role == Role::client) {
        if (connection_ == nullptr) {
            set_error("客户端连接对象未创建");
            return false;
        }

        if (QUIC_FAILED(api(msquic_)->ConnectionStart(
                handle(connection_),
                handle(configuration_),
                QUIC_ADDRESS_FAMILY_UNSPEC,
                config_.endpoint.c_str(),
                config_.port))) {
            set_error("ConnectionStart 失败，请确认服务端地址与端口");
            return false;
        }

        return wait_for_handshake(kHandshakeTimeout);
    }

    // 服务端：Listener 已启动即可，等待客户端在 open_data_channel() 中完成。
    if (!listener_started_) {
        set_error("Server listener is not started");
        return false;
    }

    return true;
}

bool QuicSecureTransport::wait_for_stream_ready(
    const std::chrono::milliseconds timeout) {
    auto* channel = static_cast<StreamChannel*>(stream_channel_);
    if (channel == nullptr) {
        set_error("QUIC 流尚未初始化");
        return false;
    }

    std::unique_lock lock(channel->mutex);
    const bool ready = channel->cv.wait_for(lock, timeout, [&] {
        return channel->ready.load(std::memory_order_acquire);
    });

    if (!ready) {
        set_error("QUIC 数据通道就绪超时");
        return false;
    }

    return true;
}

bool QuicSecureTransport::open_data_channel() {
    if (data_channel_open_) {
        return true;
    }

    if (config_.role == Role::server) {
        if (!wait_for_handshake(kHandshakeTimeout)) {
            last_error_.clear();
            return false;
        }
    }

    const auto stream_timeout =
        config_.role == Role::server ? kStreamReadyTimeout : kStreamReadyTimeout;

    if (!wait_for_stream_ready(stream_timeout)) {
        if (config_.role == Role::server) {
            last_error_.clear();
        }
        return false;
    }

    data_channel_open_ = true;
    return true;
}

bool QuicSecureTransport::send_payload(const std::span<const std::byte> payload) {
    if (!data_channel_open_) {
        set_error("QUIC 数据通道尚未打开");
        return false;
    }

    auto* channel = static_cast<StreamChannel*>(stream_channel_);
    if (channel == nullptr || channel->stream == nullptr || channel->msquic == nullptr) {
        set_error("QUIC 流不可用");
        return false;
    }
    if (payload.empty() || payload.size() > 65536) {
        set_error("载荷长度非法");
        return false;
    }

    std::vector<std::uint8_t> buffer(4 + payload.size());
    const auto size = static_cast<std::uint32_t>(payload.size());
    buffer[0] = static_cast<std::uint8_t>((size >> 24) & 0xFF);
    buffer[1] = static_cast<std::uint8_t>((size >> 16) & 0xFF);
    buffer[2] = static_cast<std::uint8_t>((size >> 8) & 0xFF);
    buffer[3] = static_cast<std::uint8_t>(size & 0xFF);
    std::memcpy(buffer.data() + 4, payload.data(), payload.size());

    std::unique_lock lock(channel->mutex);
    if (!channel->cv.wait_for(lock, kStreamReadyTimeout, [&] {
            return channel->send_complete.load(std::memory_order_acquire);
        })) {
        set_error("QUIC 发送队列阻塞超时");
        return false;
    }

    QUIC_BUFFER quic_buffer{
        static_cast<uint32_t>(buffer.size()),
        buffer.data(),
    };
    channel->send_complete.store(false, std::memory_order_release);

    if (QUIC_FAILED(channel->msquic->StreamSend(
            channel->stream,
            &quic_buffer,
            1,
            QUIC_SEND_FLAG_NONE,
            nullptr))) {
        channel->send_complete.store(true, std::memory_order_release);
        set_error("StreamSend 失败");
        return false;
    }

    if (!channel->cv.wait_for(lock, kStreamReadyTimeout, [&] {
            return channel->send_complete.load(std::memory_order_acquire);
        })) {
        set_error("QUIC 发送完成超时");
        return false;
    }

    return true;
}

std::optional<Packet> QuicSecureTransport::try_receive_payload() {
    if (!data_channel_open_) {
        return std::nullopt;
    }

    auto* channel = static_cast<StreamChannel*>(stream_channel_);
    if (channel == nullptr) {
        return std::nullopt;
    }

    std::lock_guard lock(channel->mutex);
    if (channel->frames.empty()) {
        return Packet{};
    }

    const std::vector<std::uint8_t> frame = std::move(channel->frames.front());
    channel->frames.pop_front();
    Packet packet(frame.size());
    std::memcpy(packet.data(), frame.data(), frame.size());
    return packet;
}

void QuicSecureTransport::reset_for_next_client() noexcept {
    if (closed_ || config_.role != Role::server || !listener_started_) {
        return;
    }

    try {
        data_channel_open_ = false;

        if (stream_channel_ != nullptr && api(msquic_) != nullptr) {
            auto* channel = static_cast<StreamChannel*>(stream_channel_);
            if (channel->stream != nullptr) {
                api(msquic_)->SetCallbackHandler(channel->stream, nullptr, nullptr);
                api(msquic_)->StreamClose(channel->stream);
                channel->stream = nullptr;
                channel->msquic = nullptr;
            }
            {
                std::lock_guard lock(channel->mutex);
                channel->frames.clear();
            }
            channel->ready.store(false, std::memory_order_release);
            channel->send_complete.store(true, std::memory_order_release);
        }

        if (connection_ != nullptr && api(msquic_) != nullptr) {
            api(msquic_)->SetCallbackHandler(handle(connection_), nullptr, nullptr);
            api(msquic_)->ConnectionClose(handle(connection_));
            connection_ = nullptr;
        }

        if (handshake_state_ != nullptr) {
            auto* state = static_cast<HandshakeState*>(handshake_state_);
            state->connected.store(false, std::memory_order_release);
            state->failed.store(false, std::memory_order_release);
            state->shutdown_complete.store(false, std::memory_order_release);
            state->status.store(0, std::memory_order_release);
        }

        if (connection_context_ != nullptr) {
            auto* ctx = static_cast<ConnectionContext*>(connection_context_);
            ctx->msquic = api(msquic_);
            ctx->stream = static_cast<StreamChannel*>(stream_channel_);
        }
        if (server_context_ != nullptr) {
            auto* server = static_cast<ServerContext*>(server_context_);
            server->msquic = api(msquic_);
            server->stream = static_cast<StreamChannel*>(stream_channel_);
            server->connection_context =
                static_cast<ConnectionContext*>(connection_context_);
        }
    } catch (...) {
    }
}

void QuicSecureTransport::close() noexcept {
    if (closed_) {
        return;
    }
    closed_ = true;
    data_channel_open_ = false;

    if (connection_context_ != nullptr) {
        auto* ctx = static_cast<ConnectionContext*>(connection_context_);
        ctx->handshake = nullptr;
        ctx->stream = nullptr;
        ctx->msquic = nullptr;
    }
    if (server_context_ != nullptr) {
        auto* server = static_cast<ServerContext*>(server_context_);
        server->handshake = nullptr;
        server->stream = nullptr;
        server->msquic = nullptr;
        server->connection_context = nullptr;
    }

    if (stream_channel_ != nullptr && api(msquic_) != nullptr) {
        auto* channel = static_cast<StreamChannel*>(stream_channel_);
        if (channel->stream != nullptr) {
            api(msquic_)->SetCallbackHandler(channel->stream, nullptr, nullptr);
            api(msquic_)->StreamClose(channel->stream);
            channel->stream = nullptr;
            channel->msquic = nullptr;
        }
    }

    if (connection_ != nullptr && api(msquic_) != nullptr) {
        api(msquic_)->SetCallbackHandler(handle(connection_), nullptr, nullptr);

        auto* state = static_cast<HandshakeState*>(handshake_state_);
        if (state != nullptr) {
            (void)api(msquic_)->ConnectionShutdown(
                handle(connection_),
                QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                0);
            std::unique_lock lock(state->mutex);
            (void)state->cv.wait_for(
                lock,
                std::chrono::milliseconds(2000),
                [&] {
                    return state->shutdown_complete.load(std::memory_order_acquire);
                });
        }
        api(msquic_)->ConnectionClose(handle(connection_));
        connection_ = nullptr;
    }

    if (listener_ != nullptr && api(msquic_) != nullptr) {
        if (listener_started_) {
            api(msquic_)->ListenerClose(handle(listener_));
        }
        listener_ = nullptr;
        listener_started_ = false;
    }

    if (configuration_ != nullptr && api(msquic_) != nullptr) {
        api(msquic_)->ConfigurationClose(handle(configuration_));
        configuration_ = nullptr;
    }

    if (registration_ != nullptr && api(msquic_) != nullptr) {
        api(msquic_)->RegistrationClose(handle(registration_));
        registration_ = nullptr;
    }

    delete static_cast<HandshakeState*>(handshake_state_);
    handshake_state_ = nullptr;
    delete static_cast<ServerContext*>(server_context_);
    server_context_ = nullptr;
    delete static_cast<ConnectionContext*>(connection_context_);
    connection_context_ = nullptr;
    delete static_cast<StreamChannel*>(stream_channel_);
    stream_channel_ = nullptr;
    delete static_cast<StoredCredentials*>(stored_credentials_);
    stored_credentials_ = nullptr;

    if (api_open_) {
        MsQuicApi::release();
        msquic_ = nullptr;
        api_open_ = false;
    }

    last_error_.clear();
}

}  // namespace tunnel
