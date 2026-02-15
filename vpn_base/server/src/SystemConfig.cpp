#include "../include/SystemConfig.h"
#include <iostream>

void SystemConfig::run_command(const std::string& cmd)
{
    std::cout << "执行命令: " << cmd << std::endl;
    int ret = std::system(cmd.c_str());
    if (ret != 0)
    {
        std::cerr << "命令执行失败: " << cmd << " (返回值: " << ret << ")" << std::endl;
    }
    else
    {
        std::cout << "命令执行成功" << std::endl;
    }
}

void SystemConfig::configure_tun(const std::string& dev, const std::string& ip, const std::string& mask)
{
    run_command("ip link set dev " + dev + " down");
    run_command("ip addr flush dev " + dev);
    run_command("ip addr add " + ip + "/" + mask + " dev " + dev);
    // 设置MTU为1400，为SSL/TLS封装留出空间
    run_command("ip link set dev " + dev + " mtu 1400");
    run_command("ip link set dev " + dev + " up");
    run_command("ip route add 10.8.0.0/24 dev " + dev + " proto kernel scope link src " + ip);
    std::cout << "TUN设备配置完成: " << dev << " " << ip << "/" << mask << " MTU=1400" << std::endl;
}

void SystemConfig::enable_ip_forward()
{
    run_command("sysctl -w net.ipv4.ip_forward=1");
    std::cout << "IP转发已启用" << std::endl;
}

void SystemConfig::setup_iptables_nat(const std::string& network, const std::string& nic)
{
    run_command("iptables -t nat -F");
    run_command("iptables -t nat -A POSTROUTING -s " + network + " -o " + nic + " -j MASQUERADE");
    run_command("iptables -A FORWARD -i " + vpn_config::TUN_DEV + " -o " + nic + " -j ACCEPT");
    run_command("iptables -A FORWARD -i " + nic + " -o " + vpn_config::TUN_DEV + " -m state --state RELATED,ESTABLISHED -j ACCEPT");
    // 设置TCP MSS，确保TCP数据包大小合适（MTU 1400 - IP头20 - TCP头20 = 1360）
    run_command("iptables -t mangle -A FORWARD -o " + vpn_config::TUN_DEV + " -p tcp --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 1360");
    run_command("iptables -t mangle -A FORWARD -i " + vpn_config::TUN_DEV + " -p tcp --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 1360");
    std::cout << "iptables NAT规则配置完成（包含TCP MSS调整）" << std::endl;
}
