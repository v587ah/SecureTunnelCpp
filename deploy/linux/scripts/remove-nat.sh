#!/usr/bin/env bash
# 移除 SecureTunnel nftables 规则
# 用法：sudo ./remove-nat.sh

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_root

if nft list table ip sectunnel &>/dev/null; then
    nft delete table ip sectunnel
    echo "已删除 nftables 表 ip sectunnel"
else
    echo "nftables 表 ip sectunnel 不存在，跳过"
fi
