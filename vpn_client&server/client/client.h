#ifndef CLIENT_H
#define CLIENT_H

#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

    int start_vpn_client(const char* config_path, const char* route_ip);

#ifdef __cplusplus
}
#endif

#endif