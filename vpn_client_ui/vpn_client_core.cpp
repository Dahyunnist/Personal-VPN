#include "vpn_client_core.h"
#include <iostream>
#include <sstream>
#include <streambuf>
#include <mutex>
#include <thread>
#include <chrono>
#include <future>

// 包含client的头文件
#include "client/client.h"

// 前向声明停止函数（在client_core_impl.cpp中定义）
extern void stop_vpn_client_impl();

// 日志重定向器
class LogRedirector : public std::streambuf {
public:
    LogRedirector(std::function<void(const std::string&, bool)> callback) 
        : m_callback(callback) {
        m_old_cout = std::cout.rdbuf();
        m_old_cerr = std::cerr.rdbuf();
        std::cout.rdbuf(this);
        std::cerr.rdbuf(this);
    }

    ~LogRedirector() {
        flush();
        std::cout.rdbuf(m_old_cout);
        std::cerr.rdbuf(m_old_cerr);
    }

protected:
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_buffer.append(s, n);
        if (m_buffer.size() > 512) {
            flush();
        }
        return n;
    }

    int_type overflow(int_type c) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (c != traits_type::eof()) {
            m_buffer += static_cast<char>(c);
            if (c == '\n') {
                flush();
            }
        }
        return c;
    }

private:
    std::function<void(const std::string&, bool)> m_callback;
    std::streambuf* m_old_cout;
    std::streambuf* m_old_cerr;
    std::string m_buffer;
    std::mutex m_mutex;
    bool m_is_error = false;

    void flush() {
        if (!m_buffer.empty() && m_callback) {
            // 检查是否是错误输出（cerr）
            bool is_error = (std::cerr.rdbuf() == this);
            
            // 按行分割
            std::istringstream iss(m_buffer);
            std::string line;
            while (std::getline(iss, line)) {
                if (!line.empty()) {
                    m_callback(line, is_error);
                }
            }
            m_buffer.clear();
        }
    }
};

VPNClientCore::VPNClientCore() {
}

VPNClientCore::~VPNClientCore() {
    Disconnect();  // 真正断开连接
}

void VPNClientCore::RunClient(const std::string& config_path, const std::string& route_ip) {
    // 设置日志重定向
    LogRedirector redirector(m_log_callback);
    
    // 在新线程中调用vpn_base/client的start_vpn_client函数
    // 注意：start_vpn_client是阻塞的，所以我们在独立线程中运行
    // 如果 route_ip 为空，则只建立连接不添加路由
    start_vpn_client(config_path.c_str(), route_ip.empty() ? nullptr : route_ip.c_str());
    
    // 函数返回时，客户端已停止
    m_connected = false;
}

bool VPNClientCore::StartConnection(const std::string& config_path,
                                    std::function<void(const std::string&, bool)> log_callback) {
    if (m_connected) {
        return true; // 已经连接
    }

    m_log_callback = log_callback;
    m_config_path = config_path;
    m_should_stop = false;
    m_connected = true;

    // 在新线程中运行客户端（不添加路由）
    m_client_thread = std::thread(&VPNClientCore::RunClient, this, config_path, "");

    // 等待一下确保连接建立
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    return true;
}

bool VPNClientCore::AddRoute(const std::string& route_ip) {
    if (m_route_active) {
        // 如果已有路由，先删除
        RemoveRoute();
    }
    
    if (!m_connected) {
        return false; // 未连接，无法添加路由
    }
    
    if (add_route(route_ip.c_str())) {
        m_current_route_ip = route_ip;
        m_route_active = true;
        return true;
    }
    return false;
}

void VPNClientCore::RemoveRoute() {
    if (!m_route_active) {
        return;
    }
    
    remove_current_route();
    m_route_active = false;
    m_current_route_ip.clear();
}

bool VPNClientCore::Start(const std::string& config_path, const std::string& route_ip,
                          std::function<void(const std::string&, bool)> log_callback) {
    // 如果未连接，先建立连接
    if (!m_connected) {
        if (!StartConnection(config_path, log_callback)) {
            return false;
        }
    }
    
    // 添加路由
    return AddRoute(route_ip);
}

void VPNClientCore::Stop() {
    // 只删除路由，不断开连接
    RemoveRoute();
}

void VPNClientCore::Disconnect() {
    // 先删除路由
    RemoveRoute();
    
    // 如果已连接，断开连接
    if (m_connected) {
        m_should_stop = true;
        stop_vpn_client_impl();
        
        if (m_client_thread.joinable()) {
            // 等待线程结束
            auto start_time = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::seconds(5);
            
            while (m_connected && m_client_thread.joinable()) {
                if (std::chrono::steady_clock::now() - start_time > timeout) {
                    std::cerr << "警告：VPN客户端线程停止超时" << std::endl;
                    if (m_client_thread.joinable()) {
                        m_client_thread.detach();
                    }
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            if (m_client_thread.joinable()) {
                try {
                    m_client_thread.join();
                } catch (...) {
                    // 忽略join时的异常
                }
            }
        }
        
        m_connected = false;
    }
}

void VPNClientCore::StopVPNClient() {
    stop_vpn_client_impl();
}

