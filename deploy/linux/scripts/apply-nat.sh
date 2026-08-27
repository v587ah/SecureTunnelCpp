#!/usr/bin/env bash
# 启用 IPv4 转发并加载 nftables NAT/防火墙规则
# 用法：sudo ./apply-nat.sh

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_root
load_env

WAN="$(detect_wan_if)"
validate_wan_if "${WAN}"

enable_ip_forward

# 清理旧表（幂等）
nft list table ip sectunnel &>/dev/null && nft delete table ip sectunnel || true

QUIC_RULE="udp dport ${QUIC_PORT} accept"
if [[ -n "${QUIC_ALLOW_CIDR:-}" ]]; then
    QUIC_RULE="ip saddr ${QUIC_ALLOW_CIDR} udp dport ${QUIC_PORT} accept"
fi

nft -f - <<EOF
table ip sectunnel {
    chain input {
        type filter hook input priority filter; policy drop;

        iif "lo" accept
        ct state established,related accept
        tcp dport ${SSH_PORT} accept
        ${QUIC_RULE}
        icmp type echo-request accept
    }

    chain forward {
        type filter hook forward priority filter; policy drop;

        iifname "${TUN_IF}" oifname "${WAN}" accept
        iifname "${WAN}" oifname "${TUN_IF}" ct state established,related accept
    }

    chain postrouting {
        type nat hook postrouting priority srcnat; policy accept;

        ip saddr ${TUN_NET} oifname "${WAN}" masquerade
    }
}
EOF

echo "nftables 规则已加载（WAN=${WAN}, TUN=${TUN_IF}, NET=${TUN_NET}）"
echo "验证：nft list table ip sectunnel"
