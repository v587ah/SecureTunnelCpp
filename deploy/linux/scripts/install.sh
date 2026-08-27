#!/usr/bin/env bash
# 安装配置文件、systemd 单元与脚本到系统目录
# 用法：sudo ./install.sh /path/to/SecureTunnelCpp

set -euo pipefail

REPO_ROOT="${1:-}"
if [[ -z "${REPO_ROOT}" || ! -d "${REPO_ROOT}/deploy/linux" ]]; then
    echo "用法: sudo $0 /path/to/SecureTunnelCpp" >&2
    exit 1
fi

if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
    echo "错误：需要 root 权限。" >&2
    exit 1
fi

install -d -m 0755 /etc/sectunnel
install -d -m 0750 /var/lib/sectunnel
install -d -m 0755 /etc/sectunnel/certs
install -d -m 0755 /usr/local/lib/sectunnel

if [[ ! -f /etc/sectunnel/sectunnel.env ]]; then
    install -m 0640 "${REPO_ROOT}/deploy/linux/sectunnel.env.example" \
        /etc/sectunnel/sectunnel.env
    echo "已安装 /etc/sectunnel/sectunnel.env（请编辑 WAN_IF、证书路径等）"
else
    echo "保留已有 /etc/sectunnel/sectunnel.env"
fi

install -m 0755 "${REPO_ROOT}/deploy/linux/scripts/"*.sh /usr/local/lib/sectunnel/

install -m 0644 "${REPO_ROOT}/docs/LINUX_SERVER_DEPLOY.md" \
    /usr/share/doc/sectunnel/LINUX_SERVER_DEPLOY.md 2>/dev/null || \
    install -d -m 0755 /usr/share/doc/sectunnel && \
    install -m 0644 "${REPO_ROOT}/docs/LINUX_SERVER_DEPLOY.md" \
        /usr/share/doc/sectunnel/LINUX_SERVER_DEPLOY.md

install -m 0644 "${REPO_ROOT}/deploy/linux/systemd/sectunnel-server.service" \
    /etc/systemd/system/sectunnel-server.service

systemctl daemon-reload
echo "安装完成。下一步："
echo "  1. 编辑 /etc/sectunnel/sectunnel.env"
echo "  2. 放置 TLS 证书到 /etc/sectunnel/certs/"
echo "  3. 安装 tunnel_server 到 /usr/local/bin/"
echo "  4. sudo /usr/local/lib/sectunnel/apply-nat.sh"
echo "  5. sudo systemctl enable --now sectunnel-server"
