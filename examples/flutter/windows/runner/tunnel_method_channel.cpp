// Flutter Windows 宿主侧 MethodChannel 示例（需放入 flutter/windows/runner/ 并注册）。
//
// 作用：让 Flutter UI（普通权限）通过 C++ Runner 调用 C# 服务或直接启动
// 已提权的 tunnel_client.exe。

#include <flutter/flutter_engine.h>
#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>

#include <memory>
#include <string>

namespace {

class TunnelMethodChannel {
 public:
  explicit TunnelMethodChannel(flutter::BinaryMessenger* messenger)
      : channel_(std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
            messenger,
            "sectunnel/control",
            &flutter::StandardMethodCodec::GetInstance())) {
    channel_->SetMethodCallHandler(
        [this](const auto& call, auto result) { HandleMethodCall(call, std::move(result)); });
  }

 private:
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    if (call.method_name() == "getStatus") {
      flutter::EncodableMap map;
      map[flutter::EncodableValue("state")] = flutter::EncodableValue("disconnected");
      map[flutter::EncodableValue("connected")] = flutter::EncodableValue(false);
      result->Success(flutter::EncodableValue(map));
      return;
    }

    if (call.method_name() == "connect") {
      // 生产环境：在此调用 Named Pipe / Windows Service，而不是直接 CreateProcess。
      result->Error("NOT_IMPLEMENTED",
                    "请在 Runner 中对接 SecureTunnel Windows Service 或提权启动逻辑");
      return;
    }

    if (call.method_name() == "disconnect") {
      result->Success();
      return;
    }

    result->NotImplemented();
  }

  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel_;
};

}  // namespace

void RegisterTunnelMethodChannel(flutter::BinaryMessenger* messenger) {
  static TunnelMethodChannel channel(messenger);
}
