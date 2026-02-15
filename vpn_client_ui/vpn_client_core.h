#ifndef VPN_CLIENT_CORE_H
#define VPN_CLIENT_CORE_H

#include <string>
#include <thread>
#include <atomic>
#include <functional>

// VPN客户端核心接口
class VPNClientCore {
public:
    VPNClientCore();
    ~VPNClientCore();

    // 启动VPN客户端（建立连接，但不添加路由）
    // config_path: 配置文件路径
    // log_callback: 日志回调函数 (line, is_error)
    bool StartConnection(const std::string& config_path, 
                        std::function<void(const std::string&, bool)> log_callback = nullptr);

    // 添加路由（不建立新连接）
    // route_ip: 路由IP
    bool AddRoute(const std::string& route_ip);
    
    // 删除路由（不断开连接）
    void RemoveRoute();
    
    // 启动VPN（如果未连接则建立连接，然后添加路由）
    // config_path: 配置文件路径
    // route_ip: 路由IP
    // log_callback: 日志回调函数 (line, is_error)
    bool Start(const std::string& config_path, const std::string& route_ip, 
               std::function<void(const std::string&, bool)> log_callback = nullptr);

    // 停止VPN（只删除路由，不断开连接）
    void Stop();
    
    // 真正断开连接（在析构函数中调用）
    void Disconnect();
    
    // 获取停止函数指针（用于调用vpn_base/client的停止函数）
    static void StopVPNClient();

    // 检查连接是否已建立
    bool IsConnected() const { return m_connected; }
    
    // 检查路由是否已添加
    bool IsRouteActive() const { return m_route_active; }

private:
    std::thread m_client_thread;
    std::atomic<bool> m_connected{false};      // 连接是否已建立
    std::atomic<bool> m_route_active{false}; // 路由是否已激活
    std::atomic<bool> m_should_stop{false};
    std::function<void(const std::string&, bool)> m_log_callback;
    std::string m_config_path;
    std::string m_current_route_ip;
    
    // 内部实现函数
    void RunClient(const std::string& config_path, const std::string& route_ip);
};

#endif // VPN_CLIENT_CORE_H

