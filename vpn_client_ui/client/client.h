#ifndef CLIENT_H
#define CLIENT_H

#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

    int start_vpn_client(const char* config_path, const char* route_ip);
    
    // 路由管理函数
    bool add_route(const char* target_ip);
    bool remove_route(const char* target_ip);
    bool remove_current_route();

#ifdef __cplusplus
}
#endif

#endif