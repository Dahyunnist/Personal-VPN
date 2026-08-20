#ifndef UI_MAIN_H
#define UI_MAIN_H

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <functional>

// 必须在windows.h之前包含winsock2.h
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <commdlg.h>
#include <d3d11.h>

// ImGui
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "vpn_client_core.h"

// 前向声明
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

class UIMain {
public:
    UIMain();
    ~UIMain();

    bool Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();
    void Render();
    void Update();

    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

private:
    // DirectX设备
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    HWND m_hwnd = nullptr;

    // UI状态
    char m_configPath[512] = "";
    char m_serverHost[256] = "";
    char m_serverPort[16] = "";
    char m_routes[512] = "";

    bool m_isConnected = false;
    bool m_isStarting = false;
    bool m_imguiInitialized = false;  // 标记 ImGui 是否已初始化

    // VPN客户端
    std::unique_ptr<VPNClientCore> m_vpnClient;

    // 日志
    std::vector<std::string> m_logLines;
    std::mutex m_logMutex;
    static constexpr size_t MAX_LOG_LINES = 1000;

    // 辅助函数
    std::string OpenFileDialog(const char* filter);
    bool ImportConfig(const std::string& configPath);
    void StartVPN();
    void StopVPN();
    void AddLog(const std::string& line, bool isError = false);
};

#endif // UI_MAIN_H
