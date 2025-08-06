#ifndef CLIENT_H
#define CLIENT_H

#include <stdatomic.h>
#include <atomic>

#ifdef __cplusplus
extern "C" {
#endif



    extern std::atomic_bool have_quit;

    int start_vpn_client(const char* config_path, const char* route_ip);

    void stop_vpn_client();

#ifdef __cplusplus
}
#endif

#endif