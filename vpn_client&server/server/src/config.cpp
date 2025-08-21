#include "../include/config.h"
#include "../include/IpPoolManager.h"

namespace vpn_global
{
unsigned short port = 0;
std::atomic<bool> running(true);
std::mutex cmd_mutex;
IpPoolManager ip_pool(vpn_config::IP_POOL_START, vpn_config::IP_POOL_END);
}    // namespace vpn_global
