#!/usr/bin/env bash
# 公共函数：加载配置、检测 WAN 网卡、权限检查

set -euo pipefail

ENV_FILE="${SECTUNNEL_ENV:-/etc/sectunnel/sectunnel.env}"

load_env() {
    if [[ ! -f "${ENV_FILE}" ]]; then
        echo "错误：未找到配置文件 ${ENV_FILE}" >&2
        echo "请复制 deploy/linux/sectunnel.env.example 并修改。" >&2
        exit 1
    fi
    # shellcheck disable=SC1090
    source "${ENV_FILE}"
}

require_root() {
    if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
        echo "错误：此脚本需要 root 权限。" >&2
        exit 1
    fi
}

detect_wan_if() {
    if [[ -n "${WAN_IF:-}" ]]; then
        echo "${WAN_IF}"
        return
    fi
    ip route show default 2>/dev/null | awk '{print $5; exit}'
}

validate_wan_if() {
    local wan="$1"
    if [[ -z "${wan}" ]]; then
        echo "错误：无法检测默认路由网卡，请在 sectunnel.env 中设置 WAN_IF。" >&2
        exit 1
    fi
    if ! ip link show "${wan}" &>/dev/null; then
        echo "错误：网卡 ${wan} 不存在。" >&2
        exit 1
    fi
}

enable_ip_forward() {
    sysctl -w net.ipv4.ip_forward=1 >/dev/null
    local conf="/etc/sysctl.d/99-sectunnel-forward.conf"
    if [[ ! -f "${conf}" ]]; then
        echo "net.ipv4.ip_forward=1" > "${conf}"
    fi
}

disable_ip_forward_if_unused() {
    # 仅当没有其他服务依赖时，部署文档建议手动决定是否关闭
    :
}
