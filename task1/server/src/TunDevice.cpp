#include "../include/TunDevice.h"
#include <iostream>
// #include <string>
// #include <vector>
// #include <memory>
// #include <thread>
// #include <boost/asio.hpp>
// #include <boost/asio/ssl.hpp>
// #include <boost/bind/bind.hpp>
// #include <sys/types.h>
// #include <sys/stat.h>
// #include <fcntl.h>
// #include <sys/ioctl.h>
// #include <net/if.h>
#include <linux/if_tun.h>
// #include <unistd.h>
// #include <cstring>
// #include <array>
// #include <atomic>
// #include <csignal>
// #include <cstdlib>
// #include <arpa/inet.h>
// #include <boost/asio.hpp>
// #include <boost/asio/ssl.hpp>
// #include <sys/stat.h>
// #include <fcntl.h>
// #include <sys/ioctl.h>
// #include <net/if.h>
// #include <linux/if_tun.h>
// #include <unistd.h>
// #include <cstring>
// #include <fstream>
// #include <unordered_set>
// #include <mutex>
// #include <nlohmann/json.hpp>



TunDevice::TunDevice(const std::string& dev_name) : fd_(-1){
    if ((fd_ = open("/dev/net/tun", O_RDWR)) < 0) {
        throw std::runtime_error("Failed to open /dev/net/tun: " + std::string(strerror(errno)));
    }

    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    std::strncpy(ifr.ifr_name, dev_name.c_str(), IFNAMSIZ - 1);

    if(ioctl(fd_, TUNSETIFF, reinterpret_cast<void*>(&ifr)) < 0){
        close(fd_);
        throw std::runtime_error("Failed to ioctl TUNSETIFF: " + std::string(strerror(errno)));
    }
    std::cout << "TUN设备创建成功: " << ifr.ifr_name << " (fd: " << fd_ << ")" << std::endl;
}

TunDevice::~TunDevice(){
    if (fd_ != -1){
        close(fd_);
        std::cout << "TUN设备关闭" << std::endl;
    }
}

ssize_t TunDevice::write(const uint8_t* raw_data, size_t raw_size){
    ssize_t written = ::write(fd_, raw_data, raw_size);
    if (written < 0) {
        std::cerr << "TUN写入失败: " << strerror(errno) << std::endl;
    }
    return written;
}


