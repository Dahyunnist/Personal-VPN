#include "ui_main.h"
#include "personal_vpn/client_config.hpp"

#include <cstring>
#include <exception>

UIMain::UIMain() {
    m_vpnClient = std::make_unique<VPNClientCore>();
}

UIMain::~UIMain() {
    // 先停止VPN（删除路由并断开连接）
    StopVPN();
    // 注意：Shutdown() 会在 main.cpp 中调用，这里不重复调用
    // 但如果 main.cpp 没有调用，这里作为保险
    if (m_hwnd) {
        Shutdown();
    }
}

bool UIMain::Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context) {
    m_hwnd = hwnd;
    m_device = device;
    m_context = context;

    // 初始化ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    // 加载中文字体
    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false;
    
    char font_path[MAX_PATH];
    GetWindowsDirectoryA(font_path, MAX_PATH);
    std::string msyh_path = std::string(font_path) + "\\Fonts\\msyh.ttc";
    std::string simhei_path = std::string(font_path) + "\\Fonts\\simhei.ttf";
    
    static const ImWchar ranges[] = {
        0x0020, 0x00FF, 0x4E00, 0x9FFF, 0x3000, 0x303F, 0xFF00, 0xFFEF, 0,
    };
    
    ImFont* font = nullptr;
    if (GetFileAttributesA(msyh_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        font = io.Fonts->AddFontFromFileTTF(msyh_path.c_str(), 16.0f, &font_cfg, ranges);
    } else if (GetFileAttributesA(simhei_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        font = io.Fonts->AddFontFromFileTTF(simhei_path.c_str(), 16.0f, &font_cfg, ranges);
    }
    
    if (!font) {
        io.Fonts->AddFontDefault();
        ImFontConfig merge_cfg;
        merge_cfg.MergeMode = true;
        if (GetFileAttributesA(msyh_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            io.Fonts->AddFontFromFileTTF(msyh_path.c_str(), 16.0f, &merge_cfg, ranges);
        } else if (GetFileAttributesA(simhei_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            io.Fonts->AddFontFromFileTTF(simhei_path.c_str(), 16.0f, &merge_cfg, ranges);
        }
    }

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, context);
    m_imguiInitialized = true;

    return true;
}

void UIMain::Shutdown() {
    // 只关闭一次，避免重复关闭导致断言失败
    if (!m_imguiInitialized) {
        return;
    }
    
    if (m_hwnd && m_device && m_context) {
        // 确保在设备释放前关闭 ImGui
        // 先结束当前帧，避免在渲染过程中关闭
        ImGui::EndFrame();
        
        try {
            ImGui_ImplDX11_Shutdown();
        } catch (...) {
            // 忽略关闭时的异常
        }
        try {
            ImGui_ImplWin32_Shutdown();
        } catch (...) {
            // 忽略关闭时的异常
        }
        try {
            ImGui::DestroyContext();
        } catch (...) {
            // 忽略关闭时的异常
        }
    }
    
    // 标记为已关闭，避免重复关闭
    m_imguiInitialized = false;
    // 清除引用
    m_hwnd = nullptr;
    m_device = nullptr;
    m_context = nullptr;
}

LRESULT UIMain::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    return ImGui_ImplWin32_WndProcHandler(m_hwnd, msg, wParam, lParam);
}

std::string UIMain::OpenFileDialog(const char* filter) {
    OPENFILENAMEA ofn;
    char szFile[512] = {0};

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(szFile);
    }
    return "";
}

bool UIMain::ImportConfig(const std::string& configPath) {
    try {
        const auto config = personal_vpn::client::load_client_config(configPath);
        strncpy_s(m_serverHost, config.server_host.c_str(), _TRUNCATE);
        snprintf(m_serverPort, sizeof(m_serverPort), "%u",
                 static_cast<unsigned int>(config.server_port));
        std::string routes;
        for (const auto& route : config.routes) {
            if (!routes.empty()) {
                routes += ", ";
            }
            routes += route;
        }
        strncpy_s(m_routes, routes.c_str(), _TRUNCATE);
        strncpy_s(m_configPath, configPath.c_str(), _TRUNCATE);
        AddLog("配置文件校验成功；虚拟地址将由服务器分配");
        return true;
    } catch (const std::exception& error) {
        AddLog(std::string("配置文件无效：") + error.what(), true);
        return false;
    }
}

void UIMain::AddLog(const std::string& line, bool isError) {
    std::lock_guard<std::mutex> lock(m_logMutex);
    m_logLines.push_back(isError ? "[错误] " + line : line);
    if (m_logLines.size() > MAX_LOG_LINES) {
        m_logLines.erase(m_logLines.begin());
    }
}

void UIMain::StartVPN() {
    if (m_isConnected || m_isStarting) {
        return;
    }

    if (strlen(m_configPath) == 0) {
        AddLog("请先导入有效配置文件", true);
        return;
    }
    AddLog("正在建立 mTLS 隧道并等待服务器分配地址...");
    
    auto logCallback = [this](const std::string& line, bool isError) {
        AddLog(line, isError);
    };

    if (m_vpnClient->Start(m_configPath, logCallback)) {
        m_isStarting = true;
    } else {
        AddLog("启动请求失败", true);
    }
}

void UIMain::StopVPN() {
    if (!m_isConnected && !m_isStarting) {
        return;
    }

    AddLog("正在安全断开并回滚网络配置...");
    m_vpnClient->Stop();
    m_isConnected = false;
    m_isStarting = false;
    AddLog("VPN 已断开，地址、MTU 和路由已回滚");
}

void UIMain::Render() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    
    ImGui::Begin("VPN客户端", nullptr, 
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    if (ImGui::BeginTabBar("MainTabs")) {
        // 连接配置标签
        if (ImGui::BeginTabItem("连接配置")) {
            ImGui::Spacing();

            // 配置导入栏
            if (ImGui::CollapsingHeader("配置导入", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::InputText("配置文件路径", m_configPath, sizeof(m_configPath), 
                    ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::Button("浏览") && !m_isConnected && !m_isStarting) {
                    std::string path = OpenFileDialog("配置文件(*.json)\0*.json\0所有文件(*.*)\0*.*\0");
                    if (!path.empty()) {
                        ImportConfig(path);
                    }
                }
            }

            ImGui::Spacing();

            // 设备配置栏
            if (ImGui::CollapsingHeader("设备配置", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::InputText("服务器地址", m_serverHost, sizeof(m_serverHost),
                    ImGuiInputTextFlags_ReadOnly);
                ImGui::InputText("服务器端口", m_serverPort, sizeof(m_serverPort), 
                    ImGuiInputTextFlags_ReadOnly);
                ImGui::InputText("隧道路由", m_routes, sizeof(m_routes),
                    ImGuiInputTextFlags_ReadOnly);
                ImGui::TextUnformatted("客户端地址与网关由服务器在认证后分配");
            }

            ImGui::Spacing();

            // 连接/断开按钮
            if (ImGui::Button("连接VPN", ImVec2(150, 40)) && !m_isConnected && !m_isStarting) {
                StartVPN();
            }
            ImGui::SameLine();
            if (ImGui::Button("断开连接", ImVec2(150, 40)) && (m_isConnected || m_isStarting)) {
                StopVPN();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(m_isConnected ? "已连接" : (m_isStarting ? "连接中" : "未连接"));

            ImGui::EndTabItem();
        }

        // 日志输出标签
        if (ImGui::BeginTabItem("日志输出")) {
            ImGui::Spacing();
            
            ImGui::BeginChild("LogScroll", ImVec2(0, -30), false, ImGuiWindowFlags_HorizontalScrollbar);
            
            {
                std::lock_guard<std::mutex> lock(m_logMutex);
                for (const auto& line : m_logLines) {
                    ImGui::TextUnformatted(line.c_str());
                }
            }
            
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            
            ImGui::EndChild();
            
            if (ImGui::Button("清除日志", ImVec2(100, 0))) {
                std::lock_guard<std::mutex> lock(m_logMutex);
                m_logLines.clear();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void UIMain::Update() {
    m_isConnected = m_vpnClient->IsConnected();
    m_isStarting = m_vpnClient->IsRunning() && !m_isConnected;
}
