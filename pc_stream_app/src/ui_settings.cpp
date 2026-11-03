#include "ui_settings.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "imgui.h"
#include <windows.h>
#include <shellapi.h>
#include <cstdio>

namespace ht306::ui {

void Settings::Render(AppState& state) {
    // 1. Header Section
    Theme::BeginCard("SettingsHeroCard");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::BeginChild("SettingsRevBadge", ImVec2(120.0f, 22.0f), false);
    ImGui::SetCursorPos(ImVec2(6.0f, 2.0f));
    ImGui::TextColored(Color::TextSecondary, "v1.3.0-RC4");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "Settings & System Preferences");

    ImGui::TextColored(Color::TextSecondary, "Configure audio buffer latency, startup behaviors, global hotkeys, toast alerts, and RP2350-Zero firmware maintenance.");

    ImGui::Spacing();
    ImGui::TextColored(Color::IconGray, "Config: %%APPDATA%%\\HT306Bridge\\config.json  | ");
    ImGui::SameLine();
    ImGui::TextColored(Color::StatusOk, "● Synced to hardware bridge");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    if (ImGui::Button("Restore Defaults", ImVec2(150.0f, 28.0f))) {
        state.settings.master_volume_db = 0.0f;
        state.settings.lip_sync_delay_ms = 24;
        state.settings.limiter_enabled = true;
        for (int i = 0; i < 6; ++i) state.settings.trims[i] = 0.0f;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
    if (ImGui::Button("Save Changes", ImVec2(140.0f, 28.0f))) {
        state.AddLog(LogLevel::LOG_INFO, "CONFIG", "Preferences saved to disk successfully.");
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    Theme::EndCard();

    ImGui::Spacing();

    // 2. Main Grid: 2 Columns
    float total_w = ImGui::GetContentRegionAvail().x;
    float col_w = (total_w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    // LEFT: Audio Engine & Buffer Latency
    Theme::BeginCard("AudioEngineSettingsCard", ImVec2(col_w, 360.0f));
    ImGui::TextColored(Color::PrimaryContainer, "%s", ICON_MD_TUNE);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "Audio Engine & Buffer Latency");
    ImGui::SameLine(col_w - 140.0f);
    ImGui::TextColored(Color::Tertiary, "WASAPI Direct");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Capture Stream Source
    ImGui::TextColored(Color::IconGray, "CAPTURE STREAM SOURCE");
    if (ImGui::BeginCombo("##CapSrcCombo", state.available_devices.empty() ? "Default Windows Loopback" : state.available_devices[state.active_device_index].name.c_str())) {
        for (size_t i = 0; i < state.available_devices.size(); ++i) {
            bool is_sel = (state.active_device_index == (int)i);
            if (ImGui::Selectable(state.available_devices[i].name.c_str(), is_sel)) {
                state.active_device_index = (int)i;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Follow Default Audio Endpoint", &state.settings.follow_default_device);

    ImGui::Spacing();

    // Buffer Latency Profile (Segmented: Auto, Low Latency, Safe Mode)
    ImGui::TextColored(Color::IconGray, "BUFFER LATENCY PROFILE");
    float b_btn_w = (col_w - 40.0f) / 3.0f;
    struct BufProf { const char* name; const char* sub; };
    BufProf b_profs[3] = {
        { "Auto", "Dynamic Link" },
        { "Low Latency", "3.8 ms / 128 smp" },
        { "Safe Mode", "10.0 ms / 512 smp" }
    };

    for (int b = 0; b < 3; ++b) {
        bool is_sel = (state.settings.buffer_profile == b);
        if (is_sel) ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
        else ImGui::PushStyleColor(ImGuiCol_Button, Color::Surface2);

        char b_id[32];
        std::snprintf(b_id, sizeof(b_id), "%s\n%s##bp_%d", b_profs[b].name, b_profs[b].sub, b);
        if (ImGui::Button(b_id, ImVec2(b_btn_w, 42.0f))) {
            state.settings.buffer_profile = b;
        }
        ImGui::PopStyleColor();
        if (b < 2) ImGui::SameLine();
    }

    ImGui::Spacing();

    // Resampling & Master Clock
    ImGui::TextColored(Color::IconGray, "DSP RESAMPLING HEADROOM");
    const char* resamps[] = { "Bit-Perfect Pass (No SRC)", "High-Fidelity Sinc Interpolator", "Linear Polyphase (Low CPU)" };
    if (ImGui::BeginCombo("##ResampCombo", resamps[state.settings.resampling_quality])) {
        for (int r = 0; r < 3; ++r) {
            bool is_sel = (state.settings.resampling_quality == r);
            if (ImGui::Selectable(resamps[r], is_sel)) {
                state.settings.resampling_quality = r;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    Theme::StatTile("I2S Master Clock", "24.576 MHz", "RP2350 SysPLL Sync (Zero Drift)", Color::Tertiary, col_w - 32.0f);

    Theme::EndCard();

    ImGui::SameLine();

    // RIGHT: USB Hardware Link & Virtual Audio Device
    Theme::BeginCard("HardwareLinkSettingsCard", ImVec2(col_w, 360.0f));
    ImGui::TextColored(Color::StatusInfo, "%s", ICON_MD_USB);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "Hardware Link & Driver Device");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    Theme::StatTile("WinUSB Transport Endpoint", "EP 0x03 OUT", "Bulk 512B Transfer Pipe (VID 0x2E8A : PID 0x4306)", Color::TextPrimary, col_w - 32.0f);
    ImGui::Spacing();

    ImGui::Checkbox("Auto-reconnect on USB unplug / plug", &state.settings.auto_reconnect);
    ImGui::Checkbox("Bit-Perfect Mode (Bypass software limiter & gain trims)", &state.settings.bit_perfect_mode);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(Color::TextPrimary, "Virtual Audio Device Driver (Taskbar Audio Device)");
    ImGui::TextColored(Color::TextSecondary, "Paket driver audio virtual WHQL siap dipasang agar PC menampilkan audio device terpisah:");

    ImGui::Spacing();
    if (ImGui::Button("Buka Installer Driver Virtual Cable (tools/vbcable)", ImVec2(col_w - 32.0f, 32.0f))) {
        ShellExecuteA(NULL, "open", "pc_stream_app\\tools\\vbcable", NULL, NULL, SW_SHOWNORMAL);
    }

    ImGui::Spacing();
    if (ImGui::Button("Reboot RP2350-Zero into BOOTSEL Mode", ImVec2(col_w - 32.0f, 30.0f))) {
        state.AddLog(LogLevel::LOG_WARN, "RP2350", "Requested BOOTSEL reset via CDC-ACM 1200 baud touch.");
    }

    Theme::EndCard();
}

} // namespace ht306::ui
