#ifndef TUNDEVICE_H
#define TUNDEVICE_H

// #include <iostream>
// #include <string>
// #include <vector>
// #include <memory>
// #include <thread>
#include <boost/asio.hpp>
// #include <boost/asio/ssl.hpp>
// #include <boost/bind/bind.hpp>
// #include <sys/types.h>
// #include <sys/stat.h>
// #include <fcntl.h>
// #include <sys/ioctl.h>
// #include <net/if.h>
// #include <linux/if_tun.h>
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

class TunDevice
{
   public:
    TunDevice(const std::string& dev_name);

    ~TunDevice();

    ssize_t read(uint8_t* buffer, size_t size) { return ::read(fd_, buffer, size); }

    ssize_t write(const uint8_t* raw_data, size_t raw_size);

    int fd() const { return fd_; }

    TunDevice(const TunDevice&) = delete;
    TunDevice& operator=(const TunDevice&) = delete;

   private:
    int fd_;
};

#endif