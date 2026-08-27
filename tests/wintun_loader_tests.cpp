#include "tunnel/wintun_loader.hpp"

#include <cassert>
#include <iostream>

int main() {
    // 不创建真实网卡，只验证 DLL 能否被找到并解析导出符号。
    tunnel::WintunLoader loader;
    const bool ok = loader.load();
    if (!ok) {
        std::cerr << "加载失败: " << loader.last_error() << '\n';
        return 1;
    }

    assert(loader.is_loaded());
    assert(loader.create_adapter != nullptr);
    assert(loader.receive_packet != nullptr);
    assert(loader.send_packet != nullptr);

    std::cout << "Wintun 导出符号解析成功\n";
    return 0;
}
