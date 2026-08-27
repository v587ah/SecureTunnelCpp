#include "tunnel/console_utf8.hpp"
#include "tunnel/development_adapters.hpp"
#include "tunnel/wintun_loader.hpp"
#include "tunnel/wintun_virtual_interface.hpp"

#include <iostream>

int main() {
    tunnel::init_console_utf8();

    // 验证 M1：Wintun 会话 + 接口级 IPv4/DNS。
    // 仍不修改 0.0.0.0/0 默认路由。

    tunnel::WintunLoader loader;
    if (!loader.load()) {
        std::cerr << "加载失败: " << loader.last_error() << '\n';
        return 1;
    }

    const auto version = loader.running_driver_version();
    if (version == 0) {
        std::cout << "wintun.dll 已加载，驱动版本未知。\n";
    } else {
        std::cout << "Wintun 驱动版本: " << ((version >> 16) & 0xffff) << '.'
                  << (version & 0xffff) << '\n';
    }

    tunnel::Ipv4InterfaceConfig ipv4{
        .address = "10.66.66.2",
        .prefix_length = 24,
        .dns = "1.1.1.1",
        .apply_on_open = true,
    };

    tunnel::WintunVirtualInterface iface(L"SecureTunnel", L"SecureTunnel", 0x400000, ipv4);
    if (!iface.open(1280)) {
        std::cerr << "打开虚拟网卡失败: " << iface.last_error() << '\n';
        std::cerr << "提示: 请用“以管理员身份运行”再试一次。\n";
        return 2;
    }

    std::cout << "Wintun 会话已启动。\n";
    std::cout << "适配器 LUID: " << iface.adapter_luid_value() << '\n';
    std::cout << "已配置 IPv4: " << iface.ipv4_config().address << '/'
              << static_cast<int>(iface.ipv4_config().prefix_length) << '\n';
    if (!iface.ipv4_config().dns.empty()) {
        std::cout << "接口 DNS: " << iface.ipv4_config().dns << '\n';
    }
    std::cout << "未启用全局默认路由（安全默认值）。\n";

    const auto packet = iface.read_packet();
    std::cout << "试读返回字节数: " << packet.size() << '\n';

    iface.close();
    std::cout << "已关闭会话并清理接口 IP/DNS。\n";
    return 0;
}
