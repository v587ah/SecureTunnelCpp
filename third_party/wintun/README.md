# Wintun（第三方）

本目录存放 WireGuard LLC 官方发布的 Wintun 0.14.1。

## 文件

- `wintun.dll`：运行时动态加载的用户态库（当前为 amd64）
- `wintun.h`：官方导出函数声明
- `extracted/`：完整官方包（含 LICENSE 与多架构二进制）

## 许可

Wintun 以 GPL-2.0 OR MIT 双许可发布。分发时请保留官方 `LICENSE.txt`。

## 在本项目中的作用

它只提供 **Windows 虚拟网卡**，不负责加密隧道协议。  
加密与握手仍由后续 MsQuic / Noise / WireGuard 等经审计组件完成。

## 使用注意

1. 创建适配器通常需要 **管理员权限**。
2. 首次创建时，Wintun 可能安装内核驱动。
3. 当前工程不会自动改默认路由；避免半成品把整机流量导走。
