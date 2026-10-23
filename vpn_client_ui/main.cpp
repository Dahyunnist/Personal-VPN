#include "ui_main.h"
#include "vpn_client_core.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cwchar>
#include <iostream>
#include <shellapi.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sddl.h>

#if defined(_MSC_VER)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

// 检查是否以管理员权限运行
bool IsRunAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

// DirectX11全局变量
struct D3D11Data {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* renderTargetView = nullptr;
    HWND hwnd = nullptr;
    UINT width = 800;
    UINT height = 700;
} g_d3d;

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
bool CreateRenderTarget();
void CleanupRenderTarget();

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static UIMain* uiMain = nullptr;

    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        uiMain = (UIMain*)cs->lpCreateParams;
        g_d3d.hwnd = hWnd;
    }

    if (uiMain) {
        if (uiMain->HandleMessage(msg, wParam, lParam)) {
            return true;
        }
    }

    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g_d3d.width = (UINT)LOWORD(lParam);
            g_d3d.height = (UINT)HIWORD(lParam);
            CleanupRenderTarget();
            if (g_d3d.swapChain) {
                const auto resizeResult = g_d3d.swapChain->ResizeBuffers(
                    0, g_d3d.width, g_d3d.height, DXGI_FORMAT_UNKNOWN, 0);
                if (SUCCEEDED(resizeResult)) {
                    static_cast<void>(CreateRenderTarget());
                }
            }
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { 
        D3D_FEATURE_LEVEL_11_0, 
        D3D_FEATURE_LEVEL_10_0 
    };
    
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, 
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, 
        &g_d3d.swapChain, &g_d3d.device, &featureLevel, &g_d3d.context
    );

    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, 
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd, 
            &g_d3d.swapChain, &g_d3d.device, &featureLevel, &g_d3d.context
        );
    }

    if (res != S_OK) {
        return false;
    }

    return CreateRenderTarget();
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_d3d.swapChain) { g_d3d.swapChain->Release(); g_d3d.swapChain = nullptr; }
    if (g_d3d.context) { g_d3d.context->Release(); g_d3d.context = nullptr; }
    if (g_d3d.device) { g_d3d.device->Release(); g_d3d.device = nullptr; }
}

bool CreateRenderTarget() {
    if (!g_d3d.swapChain || !g_d3d.device) {
        return false;
    }

    ID3D11Texture2D* pBackBuffer = nullptr;
    const auto bufferResult = g_d3d.swapChain->GetBuffer(
        0, IID_PPV_ARGS(&pBackBuffer));
    if (FAILED(bufferResult) || !pBackBuffer) {
        return false;
    }

    ID3D11RenderTargetView* renderTargetView = nullptr;
    const auto targetResult = g_d3d.device->CreateRenderTargetView(
        pBackBuffer, nullptr, &renderTargetView);
    pBackBuffer->Release();
    if (FAILED(targetResult) || !renderTargetView) {
        if (renderTargetView) {
            renderTargetView->Release();
        }
        return false;
    }

    g_d3d.renderTargetView = renderTargetView;
    return true;
}

void CleanupRenderTarget() {
    if (g_d3d.renderTargetView) { 
        g_d3d.renderTargetView->Release(); 
        g_d3d.renderTargetView = nullptr; 
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    const bool disable_kill_switch = arguments != nullptr && argument_count == 2 &&
        std::wcscmp(arguments[1], L"--disable-kill-switch") == 0;
    if (arguments != nullptr) {
        LocalFree(arguments);
    }
    if (disable_kill_switch) {
        if (!IsRunAsAdmin()) {
            wchar_t exePath[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
                nullptr, L"runas", exePath, L"--disable-kill-switch", nullptr, SW_SHOWNORMAL));
            return result > 32 ? 0 : 1;
        }
        try {
            VPNClientCore::DisableKillSwitch();
            MessageBoxW(nullptr, L"PersonalVPN 全局断线保护已关闭。",
                        L"PersonalVPN", MB_OK | MB_ICONINFORMATION);
            return 0;
        } catch (const std::exception& error) {
            const auto message = std::string("关闭断线保护失败：") + error.what();
            MessageBoxA(nullptr, message.c_str(), "PersonalVPN", MB_OK | MB_ICONERROR);
            return 1;
        }
    }

    WNDCLASSEX wc = { 
        sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, 
        instance, nullptr, nullptr, nullptr, nullptr,
        TEXT("VPNClientUI"), nullptr 
    };
    RegisterClassEx(&wc);

    UIMain uiMain;

    HWND hwnd = CreateWindow(
        wc.lpszClassName, TEXT("VPN客户端"), WS_OVERLAPPEDWINDOW,
        100, 100, 800, 700, nullptr, nullptr, wc.hInstance, &uiMain
    );

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        std::cerr << "Failed to initialize DirectX11" << std::endl;
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    if (!uiMain.Initialize(hwnd, g_d3d.device, g_d3d.context)) {
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        std::cerr << "Failed to initialize UI" << std::endl;
        return 1;
    }

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        if (!g_d3d.renderTargetView && !CreateRenderTarget()) {
            Sleep(16);
            continue;
        }

        const float clear_color[4] = { 0.45f, 0.55f, 0.60f, 1.00f };
        g_d3d.context->OMSetRenderTargets(1, &g_d3d.renderTargetView, nullptr);
        g_d3d.context->ClearRenderTargetView(g_d3d.renderTargetView, clear_color);

        uiMain.Update();
        uiMain.Render();

        g_d3d.swapChain->Present(1, 0);
    }

    uiMain.Shutdown();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    return 0;
}
