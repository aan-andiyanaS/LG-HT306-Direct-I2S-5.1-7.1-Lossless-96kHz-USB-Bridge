#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <tchar.h>
#include <cstdio>
#include <string>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include "app_state.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "ui_sidebar.h"
#include "ui_dashboard.h"
#include "ui_sweep_test.h"
#include "usb_transport.h"
#include "audio_capture.h"
#include "audio_endpoint_helper.h"

static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

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

static void LoadApplicationFonts(ImGuiIO& io) {
    std::string font_sans = FindAssetPath("assets\\fonts\\PlusJakartaSans.ttf");
    std::string font_mono = FindAssetPath("assets\\fonts\\JetBrainsMono.ttf");
    std::string font_icon = FindAssetPath("assets\\fonts\\MaterialIcons.ttf");

    // 1. Regular UI Font (Plus Jakarta Sans 15px)
    if (GetFileAttributesA(font_sans.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ht306::ui::Fonts::UiRegular = io.Fonts->AddFontFromFileTTF(font_sans.c_str(), 15.0f);

        // Merge Material Icons into the regular UI font so any icon can be printed inline!
        if (GetFileAttributesA(font_icon.c_str()) != INVALID_FILE_ATTRIBUTES) {
            ImFontConfig icon_cfg;
            icon_cfg.MergeMode = true;
            icon_cfg.PixelSnapH = true;
            icon_cfg.GlyphMinAdvanceX = 16.0f;
            static const ImWchar icon_ranges[] = { 0xE000, 0xF8FF, 0 };
            io.Fonts->AddFontFromFileTTF(font_icon.c_str(), 16.0f, &icon_cfg, icon_ranges);
        }
    } else {
        ht306::ui::Fonts::UiRegular = io.Fonts->AddFontDefault();
    }

    // 2. Title Font (Plus Jakarta Sans 18px Bold)
    if (GetFileAttributesA(font_sans.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ht306::ui::Fonts::UiTitle = io.Fonts->AddFontFromFileTTF(font_sans.c_str(), 18.0f);
    }

    // 3. Monospace Fonts (JetBrains Mono)
    if (GetFileAttributesA(font_mono.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ht306::ui::Fonts::Mono = io.Fonts->AddFontFromFileTTF(font_mono.c_str(), 13.0f);
        ht306::ui::Fonts::MonoLarge = io.Fonts->AddFontFromFileTTF(font_mono.c_str(), 18.0f);
    }

    io.Fonts->Build();
}

static void RenderTopHeader(ht306::AppState& state, float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ht306::ui::Color::SurfaceContainerLowest);
    ImGui::PushStyleColor(ImGuiCol_Border, ht306::ui::Color::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 8.0f));

    ImGui::BeginChild("TopHeader", ImVec2(width, 44.0f), true, ImGuiWindowFlags_AlwaysUseWindowPadding);

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
    ImGui::SameLine(r_avail - 440.0f);

    // Status Pill
    bool connected = (state.state.load() != ht306::ConnectionState::DISCONNECTED);
    if (connected) {
        ImGui::TextColored(ht306::ui::Color::StatusOk, "● CONNECTED");
    } else {
        ImGui::TextColored(ht306::ui::Color::StatusError, "● DISCONNECTED");
    }

    ImGui::SameLine();
    ImGui::TextColored(ht306::ui::Color::IconGray, "|");
    ImGui::SameLine();
    ImGui::TextColored(ht306::ui::Color::TextSecondary, "Profile: %s %s", state.settings.active_profile.c_str(), ICON_MD_ARROW_DROP_DOWN);

    ImGui::SameLine();
    bool is_streaming = state.is_streaming.load();
    if (is_streaming) {
        ImGui::PushStyleColor(ImGuiCol_Button, ht306::ui::Color::StatusWarn);
        if (ImGui::Button("⏹ STOP STREAM", ImVec2(120.0f, 26.0f))) {
            state.is_streaming.store(false);
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ht306::ui::Color::PrimaryContainer);
        if (ImGui::Button("▶ START STREAM", ImVec2(120.0f, 26.0f))) {
            state.is_streaming.store(true);
        }
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"HT306StitchStreamer", nullptr };
    ::RegisterClassExW(&wc);

    HWND hwnd = ::CreateWindowW(
        wc.lpszClassName, 
        L"LG HT306 Audio Bridge — RP2350-Zero Direct-I2S (96kHz Lossless)", 
        WS_OVERLAPPEDWINDOW, 
        50, 50, 1280, 840, 
        nullptr, nullptr, wc.hInstance, nullptr
    );

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
    io.IniFilename = nullptr; // Stateless portable UI

    // Load Stitch Google Fonts & Material Icons
    LoadApplicationFonts(io);

    ht306::ui::Theme::Apply();

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
        RenderTopHeader(app_state, io.DisplaySize.x);

        // 2. Left Sidebar Navigation
        float sidebar_w = 210.0f;
        ht306::ui::Sidebar::Render(app_state, sidebar_w);

        ImGui::SameLine();

        // 3. Right Content Area (View per active NavTab)
        float content_w = io.DisplaySize.x - sidebar_w - 18.0f;
        float content_h = io.DisplaySize.y - 48.0f;
        ImGui::BeginChild("MainContentArea", ImVec2(content_w, content_h), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        ht306::NavTab cur_tab = app_state.current_tab.load();
        switch (cur_tab) {
            case ht306::NavTab::DASHBOARD:
                ht306::ui::Dashboard::Render(app_state);
                break;
            case ht306::NavTab::MIXER: {
                ht306::ui::Theme::BeginCard("MixerCard");
                ImGui::TextColored(ht306::ui::Color::PrimaryContainer, "%s", ICON_MD_TUNE);
                ImGui::SameLine();
                ImGui::TextColored(ht306::ui::Color::TextPrimary, "6-CHANNEL HARDWARE MIXER & GAIN TRIMS");
                ImGui::Separator();
                ImGui::Spacing();
                const char* ch_names[6] = { "FL (Front L)", "FR (Front R)", "C (Center)", "LFE (Subwoofer)", "SL (Surround L)", "SR (Surround R)" };
                for (int c = 0; c < 6; ++c) {
                    char slider_id[32];
                    std::snprintf(slider_id, sizeof(slider_id), "##trim_%d", c);
                    ImGui::TextColored(ht306::ui::Color::TextPrimary, "%s", ch_names[c]);
                    ImGui::SameLine(180.0f);
                    ImGui::SetNextItemWidth(300.0f);
                    ImGui::SliderFloat(slider_id, &app_state.settings.trims[c], -12.0f, +12.0f, "%.1f dB");
                    ImGui::SameLine();
                    char mute_id[32];
                    std::snprintf(mute_id, sizeof(mute_id), "MUTE##m_%d", c);
                    ImGui::Checkbox(mute_id, &app_state.settings.mutes[c]);
                }
                ht306::ui::Theme::EndCard();
                break;
            }
            case ht306::NavTab::ROUTING: {
                ht306::ui::Theme::BeginCard("RoutingCard");
                ImGui::TextColored(ht306::ui::Color::PrimaryContainer, "%s", ICON_MD_ALT_ROUTE);
                ImGui::SameLine();
                ImGui::TextColored(ht306::ui::Color::TextPrimary, "6x6 CHANNEL MAPPING MATRIX");
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextColored(ht306::ui::Color::TextSecondary, "Konfigurasi rute input software ke output fisik I2S PS9850:");
                ImGui::Spacing();
                const char* spk_lbls[6] = { "FL", "FR", "C", "LFE", "SL", "SR" };
                for (int out = 0; out < 6; ++out) {
                    ImGui::TextColored(ht306::ui::Color::TextPrimary, "Output %s ->", spk_lbls[out]);
                    ImGui::SameLine(120.0f);
                    for (int in = 0; in < 6; ++in) {
                        bool sel = (app_state.settings.routing_map[out] == in);
                        char r_id[32];
                        std::snprintf(r_id, sizeof(r_id), "%s##r_%d_%d", spk_lbls[in], out, in);
                        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ht306::ui::Color::StatusOk);
                        else ImGui::PushStyleColor(ImGuiCol_Button, ht306::ui::Color::Surface2);
                        if (ImGui::Button(r_id, ImVec2(50.0f, 28.0f))) {
                            app_state.settings.routing_map[out] = in;
                        }
                        ImGui::PopStyleColor();
                        if (in < 5) ImGui::SameLine();
                    }
                }
                ImGui::Spacing();
                if (ImGui::Button("Reset to Default 1:1 Mapping")) {
                    for (int i = 0; i < 6; ++i) app_state.settings.routing_map[i] = i;
                }
                ht306::ui::Theme::EndCard();
                break;
            }
            case ht306::NavTab::TEST: {
                app_state.sweep_running.store(true);
                ht306::ui::Dashboard::Render(app_state);
                break;
            }
            case ht306::NavTab::PROFILES:
            case ht306::NavTab::LOG:
            case ht306::NavTab::SETTINGS: {
                ht306::ui::Theme::BeginCard("SettingsCard");
                ImGui::TextColored(ht306::ui::Color::PrimaryContainer, "%s", ICON_MD_SETTINGS);
                ImGui::SameLine();
                ImGui::TextColored(ht306::ui::Color::TextPrimary, "PREFERENCES & AUDIO DEVICE SINK");
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::Checkbox("Auto-reconnect on USB unplug", &app_state.settings.auto_reconnect);
                ImGui::Checkbox("Bit-Perfect Mode (Bypass limiter and volume)", &app_state.settings.bit_perfect_mode);
                ImGui::Spacing();
                ImGui::TextColored(ht306::ui::Color::TextSecondary, "Virtual Audio Device Status:");
                ImGui::TextColored(ht306::ui::Color::StatusOk, "Driver package available in tools/vbcable");
                if (ImGui::Button("Buka Folder Driver Virtual Cable", ImVec2(240.0f, 36.0f))) {
                    ShellExecuteA(NULL, "open", "pc_stream_app\\tools\\vbcable", NULL, NULL, SW_SHOWNORMAL);
                }
                ht306::ui::Theme::EndCard();
                break;
            }
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
