// Launcher/main.cpp
// Hikali 启动器：D3D11 + ImGui 启动界面
//
// 流程：配置渲染器初始化参数 → 写 JSON 到临时目录 → CreateProcess 启动渲染器
// 注意：
//   - add_executable 带 WIN32（/SUBSYSTEM:WINDOWS），入口必须是 wWinMain
//   - 渲染器 exe（Hikali.exe）与启动器同目录输出，由顶层
//     CMAKE_RUNTIME_OUTPUT_DIRECTORY 保证（build/bin/<Config>/）

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cwchar>
#include <filesystem>
#include <string>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include "Config.h"

// ---------- D3D11 全局状态 ----------
static ID3D11Device*            g_pd3dDevice            = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext     = nullptr;
static IDXGISwapChain*          g_pSwapChain            = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView  = nullptr;
static bool                     g_SwapChainOccluded     = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;

// 前向声明
void        CreateRenderTarget();
void        CleanupRenderTarget();
bool        CreateDeviceD3D(HWND hWnd);
void        CleanupDeviceD3D();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// imgui_impl_win32.h 故意用 #if 0 隐藏了 WndProcHandler 声明（避免引入 windows.h 依赖），
// 需自己复制这行做前向声明，官方示例（examples/example_win32_directx11）同样如此
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------- D3D11 辅助 ----------
void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = 0;       // 跟随窗口大小
    sd.BufferDesc.Height                  = 0;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hWnd;
    sd.SampleDesc.Count                   = 1;
    sd.SampleDesc.Quality                 = 0;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
        &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (hr == DXGI_ERROR_UNSUPPORTED)   // 无硬件加速时退回 WARP 软件渲染
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
            &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (hr != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain)        { g_pSwapChain->Release();        g_pSwapChain        = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice)        { g_pd3dDevice->Release();        g_pd3dDevice        = nullptr; }
}

// ---------- 窗口过程 ----------
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth  = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)    // 禁用 Alt 键系统菜单
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------- 主入口 ----------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    // 高分屏适配，必须早于窗口创建
    ImGui_ImplWin32_EnableDpiAwareness();

    // 注册并创建窗口
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"HikaliLauncherClass";
    wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);   // IDC_ARROW 是 ANSI 资源宏，需转宽
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Hikali 启动器",
        WS_OVERLAPPEDWINDOW, 100, 100, 460, 520,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // ---------- ImGui 初始化 ----------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // 中文字体：默认字体不含 CJK 字形，加载微软雅黑；失败则回退默认（中文会显示为方块）
    ImFontConfig fontCfg{};
    fontCfg.OversampleH = fontCfg.OversampleV = 1;   // 减小字体内存
    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 20.0f, &fontCfg,
                                                io.Fonts->GetGlyphRangesChineseFull());
    if (!font)
        font = io.Fonts->AddFontDefault();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    ImGui::StyleColorsDark();

    // ---------- 初始化配置（读取上次保存的，不存在则用默认） ----------
    RendererConfig cfg;
    const std::filesystem::path cfgDir  = std::filesystem::temp_directory_path() / L"Hikali";
    const std::filesystem::path cfgPath = cfgDir / L"renderer_config.json";
    std::error_code ec;
    std::filesystem::create_directories(cfgDir, ec);
    std::string loadErr;
    if (!LoadRendererConfigFromFile(cfgPath, cfg, &loadErr))
        cfg = RendererConfig{};

    // 渲染器 exe：与启动器同一目录（顶层 CMAKE_RUNTIME_OUTPUT_DIRECTORY 保证）
    wchar_t selfPath[MAX_PATH];
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    const std::filesystem::path launcherDir  = std::filesystem::path(selfPath).parent_path();
    const std::filesystem::path rendererPath = launcherDir / L"Hikali.exe";

    std::wstring launchStatus;

    // ---------- 主循环 ----------
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // 窗口被遮挡时暂停渲染
        if (g_SwapChainOccluded && g_pSwapChain->Present(1, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // 处理 Resize
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // ---------- 绘制配置界面 ----------
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::Begin("Hikali 启动器", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

        ImGui::PushFont(font);

        ImGui::TextWrapped("配置渲染器初始化参数，然后点击“启动渲染器”。");
        ImGui::Separator();

        // 图形后端
        const char* backends[] = { "D3D12", "Vulkan" };
        int backendIdx = (int)cfg.backend;
        ImGui::Combo("图形后端", &backendIdx, backends, IM_ARRAYSIZE(backends));
        cfg.backend = (GraphicsBackend)backendIdx;

        // 窗口尺寸（结构体是 int16_t，需要中间 int 变量）
        int w = cfg.windowWidth, h = cfg.windowHeight;
        ImGui::SliderInt("窗口宽度", &w, 640, 3840);
        ImGui::SliderInt("窗口高度", &h, 480, 2160);
        cfg.windowWidth  = (int16_t)w;
        cfg.windowHeight = (int16_t)h;

        ImGui::Checkbox("垂直同步 (VSync)", &cfg.vsync);
        ImGui::Checkbox("全屏", &cfg.fullScreen);
        ImGui::Checkbox("调试层 / 验证层 (Debug Layer)", &cfg.debugLayers);

        ImGui::Separator();

        // 启动按钮：写 JSON → CreateProcess 启动渲染器
        if (ImGui::Button("启动渲染器", ImVec2(-1.0f, 44.0f)))
        {
            if (SaveRenderConfigToFile(cfgPath, cfg))
            {
                std::wstring cmdLine = L"\"" + rendererPath.wstring() +
                                       L"\" --config \"" + cfgPath.wstring() + L"\"";
                STARTUPINFOW si{};
                si.cb = sizeof(si);
                PROCESS_INFORMATION pi{};
                if (CreateProcessW(rendererPath.c_str(), cmdLine.data(),
                                   nullptr, nullptr, FALSE, 0, nullptr,
                                   launcherDir.c_str(), &si, &pi))
                {
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                    launchStatus = L"已启动渲染器：" + rendererPath.wstring();
                }
                else
                {
                    wchar_t buf[160];
                    swprintf_s(buf, L"启动失败（错误码 %lu）。请确认 Hikali.exe 与启动器在同一目录。",
                               (unsigned long)GetLastError());
                    launchStatus = buf;
                }
            }
            else
            {
                launchStatus = L"写入配置文件失败。";
            }
        }

        if (!launchStatus.empty())
            ImGui::TextWrapped("%ls", launchStatus.c_str());

        ImGui::PopFont();
        ImGui::End();

        // ---------- 提交渲染 ----------
        ImGui::Render();
        const float clearColor[4] = { 0.06f, 0.06f, 0.08f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    // ---------- 清理 ----------
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
