#include "../include/BasicFunc.h"

namespace ssl = boost::asio::ssl;

uint32_t ip_to_uint(const std::string& ip_str){
    in_addr addr;
    inet_pton(AF_INET, ip_str.c_str(), &addr);
    return ntohl(addr.s_addr);
}


std::string uint_to_ip(uint32_t ip){
    in_addr addr;
    addr.s_addr = htonl(ip);
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr, buf, INET_ADDRSTRLEN);
    return buf;
}


std::string get_vm_ip() {
    int fd;
    struct ifreq ifr;
    const char* iface = vpn_config::PHYSICAL_NIC.c_str(); // 使用配置中的物理网卡名称
    
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ-1);
    
    if (ioctl(fd, SIOCGIFADDR, &ifr) == -1) {
        close(fd);
        throw std::runtime_error("无法获取网卡 " + std::string(iface) + " 的IP地址");
    }
    
    close(fd);
    return inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
}

// === SSL上下文配置函数 ===
ssl::context create_ssl_context() {
    ssl::context ctx(ssl::context::tls_server);

    try {
        ctx.use_certificate_chain_file(vpn_config::CERTIFICATE_PATH);
        ctx.use_private_key_file(vpn_config::PRIVATE_KEY_PATH, ssl::context::pem);
        ctx.set_options(
            ssl::context::default_workarounds |
            ssl::context::no_sslv2 |
            ssl::context::no_sslv3
        );
        std::cout << "SSL上下文配置成功" << std::endl;
    } catch (std::exception& e) {
        std::cerr << "SSL证书加载失败: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    return ctx;
}