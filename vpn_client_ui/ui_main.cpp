#include "ui_main.h"
#include <nlohmann/json.hpp>
#include <regex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <process.h>
#include <shlobj.h>
#include <fstream>

using json = nlohmann::json;

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
    std::ifstream file(configPath);
    if (!file.is_open()) {
        AddLog("无法打开配置文件: " + configPath, true);
        return false;
    }

    try {
        json config = json::parse(file);
        
        if (config.contains("server") && config["server"].is_object()) {
            auto server = config["server"];
            if (server.contains("ip") && server["ip"].is_string()) {
                strncpy_s(m_serverIp, server["ip"].get<std::string>().c_str(), sizeof(m_serverIp) - 1);
            }
            if (server.contains("port") && server["port"].is_number()) {
                snprintf(m_serverPort, sizeof(m_serverPort), "%d", server["port"].get<int>());
            }
        }
        
        if (config.contains("tun") && config["tun"].is_object()) {
            auto tun = config["tun"];
            if (tun.contains("ip") && tun["ip"].is_string()) {
                strncpy_s(m_tunIp, tun["ip"].get<std::string>().c_str(), sizeof(m_tunIp) - 1);
            }
        }
        
        strncpy_s(m_configPath, configPath.c_str(), sizeof(m_configPath) - 1);
        AddLog("配置文件导入成功: " + configPath);
        return true;
    } catch (...) {
        AddLog("配置文件格式错误", true);
        return false;
    }
}

void UIMain::AddLog(const std::string& line, bool isError) {
    std::lock_guard<std::mutex> lock(m_logMutex);
    m_logLines.push_back(line);
    if (m_logLines.size() > MAX_LOG_LINES) {
        m_logLines.erase(m_logLines.begin());
    }
}

void UIMain::StartVPN() {
    if (m_isConnected) {
        return; // 路由已激活
    }

    if (strlen(m_configPath) == 0 || strlen(m_routeIp) == 0) {
        AddLog("请先导入配置文件并输入路由IP", true);
        return;
    }

    if (strlen(m_serverIp) == 0 || strlen(m_serverPort) == 0 || strlen(m_tunIp) == 0) {
        AddLog("配置文件信息不完整", true);
        return;
    }

    AddLog("正在启动VPN路由...");
    
    auto logCallback = [this](const std::string& line, bool isError) {
        AddLog(line, isError);
    };

    // 如果未连接，Start 会自动建立连接；如果已连接，只添加路由
    if (m_vpnClient->Start(m_configPath, m_routeIp, logCallback)) {
        m_isConnected = true;
        AddLog("VPN路由已激活");
    } else {
        AddLog("启动VPN路由失败", true);
    }
}

void UIMain::StopVPN() {
    if (!m_isConnected) {
        return;
    }

    AddLog("正在停止VPN路由...");
    m_vpnClient->Stop();  // 只删除路由，不断开连接
    m_isConnected = false;
    AddLog("VPN路由已停止（连接保持）");
}

void UIMain::StartTest() {
    if (!m_isConnected) {
        AddLog("请先连接VPN", true);
        return;
    }

    std::string target = strlen(m_routeIp) > 0 ? m_routeIp : "1.1.1.1";
    m_testInProgress = true;
    m_testStatus = "测试中...";
    m_testOutput = "=== 开始测试 ===\n目标: " + target + "\n\n";

    std::thread([this, target]() {
        std::ostringstream cmd;
        cmd << "cmd.exe /c \"(ping -n 4 -w 2000 " << target 
            << " && curl -v -m 5 http://" << target 
            << ") || echo 测试失败; exit 0\"";

        FILE* pipe = _popen(cmd.str().c_str(), "r");
        if (pipe) {
            char buffer[128];
            std::string result;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                result += buffer;
            }
            _pclose(pipe);

            m_testOutput += result;
            m_testOutput += "\n=== 测试结束 ===\n";

            std::regex pingRegex("TTL=|往返行程的估计时间|Average =", std::regex_constants::icase);
            std::regex curlRegex("HTTP/\\d+\\.\\d+ (200|301|302)", std::regex_constants::icase);
            
            bool pingSuccess = std::regex_search(result, pingRegex);
            bool curlSuccess = std::regex_search(result, curlRegex);

            if (pingSuccess && curlSuccess) {
                m_testStatus = "测试通过✓";
            } else {
                m_testStatus = "测试失败✗";
            }

            m_testInProgress = false;
        } else {
            m_testOutput += "无法启动测试进程\n";
            m_testStatus = "测试失败✗";
            m_testInProgress = false;
        }
    }).detach();
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
                if (ImGui::Button("浏览") && !m_isConnected) {
                    std::string path = OpenFileDialog("配置文件(*.json)\0*.json\0所有文件(*.*)\0*.*\0");
                    if (!path.empty()) {
                        ImportConfig(path);
                    }
                }
            }

            ImGui::Spacing();

            // 设备配置栏
            if (ImGui::CollapsingHeader("设备配置", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::InputText("服务器IP", m_serverIp, sizeof(m_serverIp), 
                    ImGuiInputTextFlags_ReadOnly);
                ImGui::InputText("服务器端口", m_serverPort, sizeof(m_serverPort), 
                    ImGuiInputTextFlags_ReadOnly);
                ImGui::InputText("客户端TUN设备IP", m_tunIp, sizeof(m_tunIp), 
                    ImGuiInputTextFlags_ReadOnly);
                ImGui::InputText("路由IP", m_routeIp, sizeof(m_routeIp), 
                    m_isConnected ? ImGuiInputTextFlags_ReadOnly : 0);
            }

            ImGui::Spacing();

            // 连接/断开按钮
            if (ImGui::Button("连接VPN", ImVec2(150, 40)) && !m_isConnected) {
                StartVPN();
            }
            ImGui::SameLine();
            if (ImGui::Button("断开连接", ImVec2(150, 40)) && m_isConnected) {
                StopVPN();
            }
            ImGui::SameLine();
            if (ImGui::Button("测试连接", ImVec2(150, 40)) && m_isConnected && !m_testInProgress) {
                StartTest();
            }

            if (m_testInProgress) {
                ImGui::SameLine();
                ImGui::Text("测试中...");
            }

            ImGui::Spacing();
            ImGui::Text("测试状态: %s", m_testStatus.c_str());

            if (!m_testOutput.empty()) {
                ImGui::Spacing();
                ImGui::BeginChild("TestOutput", ImVec2(0, 150), true);
                ImGui::TextUnformatted(m_testOutput.c_str());
                ImGui::EndChild();
            }

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
    // 更新逻辑
}

