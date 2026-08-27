#pragma once

namespace tunnel {

// 客户端路由策略：none=仅网卡，smart=按名单分流，global=全局默认路由。
enum class ClientRoutingMode {
    none,
    smart,
    global,
};

}  // namespace tunnel
