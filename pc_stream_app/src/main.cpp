#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <tchar.h>
#include <cstdio>
#include <cmath>
#include <string>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include "app_state.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "ui_sidebar.h"
#include "ui_dashboard.h"
#include "ui_mixer.h"
#include "ui_routing.h"
#include "ui_profiles.h"
#include "ui_sweep_test.h"
#include "ui_log.h"
#include "ui_settings.h"
#include "usb_transport.h"
#include "audio_capture.h"
#include "audio_endpoint_helper.h"

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;
static float                    g_DpiScale = 1.0f;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static std::string FindAssetPath(const std::string& subpath) {
    const char* candidates[] = {
        subpath.c_str(),
        ("..\\" + subpath).c_str(),
        ("pc_stream_app\\" + subpath).c_str(),
        ("..\\..\\pc_stream_app\\" + subpath).c_str(),
        ("E:\\Project\\HomeTheather\\pc_stream_app\\" + subpath).c_str()
    };
    for (const char* p : candidates) {
        if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) {
            return std::string(p);
        }
    }
    return subpath;
}

static void LoadApplicationFonts(ImGuiIO& io, float scale) {
    std::string font_sans = FindAssetPath("assets\\fonts\\PlusJakartaSans.ttf");
    std::string font_mono = FindAssetPath("assets\\fonts\\JetBrainsMono.ttf");
    std::string font_icon = FindAssetPath("assets\\fonts\\MaterialIcons.ttf");

    ImFontConfig font_cfg;
    font_cfg.OversampleH = 2;
    font_cfg.OversampleV = 2;
    font_cfg.RasterizerMultiply = 1.15f; // Crisp subpixel contrast for dark themes

    float base_ui_size = std::round(14.0f * scale);
    float title_size   = std::round(17.0f * scale);
    float mono_size    = std::round(12.0f * scale);
    float mono_lg_size = std::round(15.0f * scale);
    float icon_size    = std::round(16.0f * scale);

    // 1. Regular UI Font (Plus Jakarta Sans)
    if (GetFileAttributesA(font_sans.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ht306::ui::Fonts::UiRegular = io.Fonts->AddFontFromFileTTF(font_sans.c_str(), base_ui_size, &font_cfg);

        // Merge Material Icons into the regular UI font so any icon can be printed inline!
        if (GetFileAttributesA(font_icon.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ImFontConfig icon_cfg = font_cfg;
            icon_cfg.MergeMode = true;
            icon_cfg.PixelSnapH = true;
            icon_cfg.GlyphMinAdvanceX = icon_size;
            static const ImWchar icon_ranges[] = { 0xE000, 0xF8FF, 0 };
            io.Fonts->AddFontFromFileTTF(font_icon.c_str(), icon_size, &icon_cfg, icon_ranges);
        }
    } else {
        ht306::ui::Fonts::UiRegular = io.Fonts->AddFontDefault();
    }

    // 2. Title Font (Plus Jakarta Sans Bold)
    if (GetFileAttributesA(font_sans.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ht306::ui::Fonts::UiTitle = io.Fonts->AddFontFromFileTTF(font_sans.c_str(), title_size, &font_cfg);
    }

    // 3. Monospace Fonts (JetBrains Mono)
    if (GetFileAttributesA(font_mono.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ht306::ui::Fonts::Mono = io.Fonts->AddFontFromFileTTF(font_mono.c_str(), mono_size, &font_cfg);
        ht306::ui::Fonts::MonoLarge = io.Fonts->AddFontFromFileTTF(font_mono.c_str(), mono_lg_size, &font_cfg);
    }

    io.Fonts->Build();
}

static void RenderTopHeader(ht306::AppState& state, float width, float scale) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ht306::ui::Color::SurfaceContainerLowest);
    ImGui::PushStyleColor(ImGuiCol_Border, ht306::ui::Color::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * scale, 5.0f * scale));

    float header_h = 38.0f * scale;
    ImGuiWindowFlags h_flags = ImGuiWindowFlags_AlwaysUseWindowPadding | 
                               ImGuiWindowFlags_NoScrollbar | 
                               ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("TopHeader", ImVec2(width, header_h), true, h_flags);

    // Left Title + Logo
    ImGui::TextColored(ht306::ui::Color::PrimaryContainer, "%s", ICON_MD_GRAPHIC_EQ);
    ImGui::SameLine();
    ImGui::TextColored(ht306::ui::Color::TextPrimary, "HT306 Audio Bridge");
    ImGui::SameLine();
    ImGui::TextColored(ht306::ui::Color::IconGray, "|");
    ImGui::SameLine();
    ImGui::TextColored(ht306::ui::Color::TextSecondary, "RP2350-Zero Direct-I2S Precision Audio");

    // Right Controls
    float r_avail = ImGui::GetContentRegionAvail().x;
    float right_block_w = 460.0f * scale;
    ImGui::SameLine(r_avail - right_block_w);

    // Status Pill (Rounded full style matching HTML)
    bool connected = (state.state.load() != ht306::ConnectionState::DISCONNECTED);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ht306::ui::Color::Surface2);
    ImGui::PushStyleColor(ImGuiCol_Border, ht306::ui::Color::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 999.0f);
    ImGui::BeginChild("HeadConnPill", ImVec2(115.0f * scale, 24.0f * scale), true, h_flags);
    ImGui::SetCursorPos(ImVec2(8.0f * scale, 3.0f * scale));
    if (connected) {
        ImGui::TextColored(ht306::ui::Color::StatusOk, "● Connected");
    } else {
        ImGui::TextColored(ht306::ui::Color::StatusError, "● Disconnected");
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    // Profile Pill (Clickable)
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ht306::ui::Color::Surface2);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ht306::ui::Color::SurfaceVariant);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    char prof_btn[64];
    std::snprintf(prof_btn, sizeof(prof_btn), "Profile: %s %s", state.settings.active_profile.c_str(), ICON_MD_ARROW_DROP_DOWN);
    if (ImGui::Button(prof_btn, ImVec2(125.0f * scale, 24.0f * scale))) {
        state.current_tab.store(ht306::NavTab::PROFILES);
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    // Person Avatar Icon Circle
    ImGui::SameLine();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p_pos = ImGui::GetCursorScreenPos();
    float av_r = 11.0f * scale;
    dl->AddCircleFilled(ImVec2(p_pos.x + av_r, p_pos.y + av_r), av_r, ht306::ui::Color::ToU32(ht306::ui::Color::PrimaryContainer));
    dl->AddText(ImVec2(p_pos.x + 4.0f * scale, p_pos.y + 2.0f * scale), ht306::ui::Color::ToU32(ht306::ui::Color::TextPrimary), ICON_MD_PERSON);
    ImGui::Dummy(ImVec2(av_r * 2.0f, av_r * 2.0f));

    // Stream Toggle Button
    ImGui::SameLine();
    bool is_streaming = state.is_streaming.load();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    if (is_streaming) {
        ImGui::PushStyleColor(ImGuiCol_Button, ht306::ui::Color::StatusWarn);
        if (ImGui::Button("⏹ STOP STREAM", ImVec2(120.0f * scale, 24.0f * scale))) {
            state.is_streaming.store(false);
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ht306::ui::Color::PrimaryContainer);
        if (ImGui::Button("▶ START STREAM", ImVec2(120.0f * scale, 24.0f * scale))) {
            state.is_streaming.store(true);
        }
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 1. Enable Windows Per-Monitor DPI Awareness V2 for razor-sharp physical pixel rendering
    typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        auto set_dpi_ctx = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (set_dpi_ctx) {
            set_dpi_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"HT306StitchStreamer", nullptr };
    ::RegisterClassExW(&wc);

    // Initial default size scaled for high-dpi
    int init_w = 1280;
    int init_h = 840;

    HWND hwnd = ::CreateWindowW(
        wc.lpszClassName, 
        L"LG HT306 Audio Bridge — RP2350-Zero Direct-I2S (96kHz Lossless)", 
        WS_OVERLAPPEDWINDOW, 
        50, 50, init_w, init_h, 
        nullptr, nullptr, wc.hInstance, nullptr
    );

    // Query exact DPI scaling factor for this window
    typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
    if (hUser32) {
        auto get_dpi = (PFN_GetDpiForWindow)GetProcAddress(hUser32, "GetDpiForWindow");
        if (get_dpi) {
            UINT dpi = get_dpi(hwnd);
            if (dpi > 0) g_DpiScale = (float)dpi / 96.0f;
        }
    }

    // Adjust window size proportionally
    if (g_DpiScale > 1.01f) {
        int scaled_w = (int)(init_w * g_DpiScale);
        int scaled_h = (int)(init_h * g_DpiScale);
        SetWindowPos(hwnd, NULL, 50, 50, scaled_w, scaled_h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        CoUninitialize();
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    // Load rasterized sharp fonts matching physical display DPI
    LoadApplicationFonts(io, g_DpiScale);

    ht306::ui::Theme::Apply(g_DpiScale);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ht306::AppState app_state;
    ht306::audio::AudioEndpointHelper::EnumerateDevices(app_state);

    ht306::usb::UsbTransport transport(app_state);
    transport.StartWorker();

    ht306::audio::AudioCapture audio_capture(app_state, transport);

    bool was_streaming = false;
    bool done = false;

    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Audio stream sync
        bool is_stream_req = app_state.is_streaming.load();
        if (is_stream_req && !was_streaming) {
            audio_capture.Start();
            was_streaming = true;
            app_state.state.store(ht306::ConnectionState::STREAMING);
        } else if (!is_stream_req && was_streaming) {
            audio_capture.Stop();
            was_streaming = false;
            app_state.state.store(transport.IsOpen() ? ht306::ConnectionState::READY : ht306::ConnectionState::DISCONNECTED);
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Outer full-window layout container
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGuiWindowFlags root_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | 
                                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | 
                                      ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin("RootContainer", nullptr, root_flags);

        // 1. Top Header
        RenderTopHeader(app_state, io.DisplaySize.x, g_DpiScale);

        // 2. Left Sidebar Navigation
        float sidebar_w = 210.0f * g_DpiScale;
        ht306::ui::Sidebar::Render(app_state, sidebar_w);

        ImGui::SameLine();

        // 3. Right Content Area (View per active NavTab)
        float content_w = io.DisplaySize.x - sidebar_w - (16.0f * g_DpiScale);
        float content_h = io.DisplaySize.y - (42.0f * g_DpiScale);
        ImGui::BeginChild("MainContentArea", ImVec2(content_w, content_h), false, 0);

        ht306::NavTab cur_tab = app_state.current_tab.load();
        switch (cur_tab) {
            case ht306::NavTab::DASHBOARD:
                ht306::ui::Dashboard::Render(app_state);
                break;
            case ht306::NavTab::MIXER:
                ht306::ui::Mixer::Render(app_state);
                break;
            case ht306::NavTab::ROUTING:
                ht306::ui::Routing::Render(app_state);
                break;
            case ht306::NavTab::PROFILES:
                ht306::ui::Profiles::Render(app_state);
                break;
            case ht306::NavTab::TEST:
                ht306::ui::SweepTest::RenderPage(app_state);
                break;
            case ht306::NavTab::LOG:
                ht306::ui::Log::Render(app_state);
                break;
            case ht306::NavTab::SETTINGS:
                ht306::ui::Settings::Render(app_state);
                break;
        }

        ImGui::EndChild();
        ImGui::End();

        // Modal for 5.1 Sweep test if opened
        ht306::ui::SweepTest::RenderModal(app_state);

        ImGui::Render();
        const float clear_color[4] = { 0.075f, 0.075f, 0.075f, 1.00f }; // #131313
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    audio_capture.Stop();
    transport.StopWorker();
    transport.Close();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();

    return 0;
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
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, 
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, 
        &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
    );

    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, 
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, 
            &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
        );
    }
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    switch (msg) {
        case WM_DPICHANGED: {
            RECT* const prcNewWindow = (RECT*)lParam;
            SetWindowPos(hWnd, NULL,
                prcNewWindow->left, prcNewWindow->top,
                prcNewWindow->right - prcNewWindow->left,
                prcNewWindow->bottom - prcNewWindow->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            g_ResizeWidth = (UINT)LOWORD(lParam);
            g_ResizeHeight = (UINT)HIWORD(lParam);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
