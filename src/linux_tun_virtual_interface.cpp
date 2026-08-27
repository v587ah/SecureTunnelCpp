#include "tunnel/linux_tun_virtual_interface.hpp"

#ifndef _WIN32

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace tunnel {

LinuxTunVirtualInterface::LinuxTunVirtualInterface(std::string interface_name)
    : interface_name_(std::move(interface_name)) {}

const std::string& LinuxTunVirtualInterface::last_error() const noexcept {
    return last_error_;
}

bool LinuxTunVirtualInterface::open(const std::uint16_t mtu) {
    close();

    if (mtu < 1280 || mtu > 1420) {
        last_error_ = "MTU 必须在 1280 到 1420 之间";
        return false;
    }

    fd_ = ::open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        last_error_ = std::string("无法打开 /dev/net/tun: ") + std::strerror(errno);
        return false;
    }

    ifreq request{};
    request.ifr_flags = IFF_TUN | IFF_NO_PI;
    if (interface_name_.size() >= IFNAMSIZ) {
        last_error_ = "接口名过长";
        close();
        return false;
    }
    std::strncpy(request.ifr_name, interface_name_.c_str(), IFNAMSIZ - 1);

    if (::ioctl(fd_, TUNSETIFF, &request) < 0) {
        last_error_ = std::string("TUNSETIFF 失败: ") + std::strerror(errno);
        close();
        return false;
    }

    interface_name_ = request.ifr_name;
    mtu_ = mtu;
    last_error_.clear();
    return true;
}

Packet LinuxTunVirtualInterface::read_packet() {
    if (fd_ < 0) {
        return {};
    }

    Packet buffer(static_cast<std::size_t>(mtu_));
    const ssize_t bytes_read = ::read(fd_, buffer.data(), buffer.size());
    if (bytes_read <= 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            last_error_ = std::string("TUN read 失败: ") + std::strerror(errno);
        }
        return {};
    }

    buffer.resize(static_cast<std::size_t>(bytes_read));
    return buffer;
}

bool LinuxTunVirtualInterface::write_packet(const std::span<const std::byte> packet) {
    if (fd_ < 0 || packet.empty()) {
        return false;
    }

    const ssize_t bytes_written =
        ::write(fd_, packet.data(), packet.size());
    if (bytes_written != static_cast<ssize_t>(packet.size())) {
        last_error_ = std::string("TUN write 失败: ") + std::strerror(errno);
        return false;
    }

    return true;
}

void LinuxTunVirtualInterface::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

}  // namespace tunnel

#endif
