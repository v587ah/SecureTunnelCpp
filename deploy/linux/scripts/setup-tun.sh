#!/usr/bin/env bash
# 配置 TUN 接口 IPv4 地址（需 root）
# 用法：sudo ./setup-tun.sh [up|down]

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

ACTION="${1:-up}"
require_root
load_env

case "${ACTION}" in
    up)
        ip link show "${TUN_IF}" &>/dev/null || true
        ip addr flush dev "${TUN_IF}" 2>/dev/null || true
        ip addr add "${TUN_GW}/${TUN_PREFIX}" dev "${TUN_IF}" 2>/dev/null || \
            ip addr replace "${TUN_GW}/${TUN_PREFIX}" dev "${TUN_IF}"
        ip link set "${TUN_IF}" up
        echo "TUN ${TUN_IF} 已配置为 ${TUN_GW}/${TUN_PREFIX}"
        ;;
    down)
        ip link set "${TUN_IF}" down 2>/dev/null || true
        ip addr flush dev "${TUN_IF}" 2>/dev/null || true
        echo "TUN ${TUN_IF} 已关闭"
        ;;
    *)
        echo "用法: $0 [up|down]" >&2
        exit 1
        ;;
esac
