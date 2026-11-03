#include "ui_dashboard.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "imgui.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace ht306::ui {

void Dashboard::Render(AppState& state) {
    RenderHardwareCard(state);
    ImGui::Spacing();
    
    // Middle section: 2 columns
    float total_w = ImGui::GetContentRegionAvail().x;
    float left_w = total_w * 0.58f;
    float right_w = total_w - left_w - ImGui::GetStyle().ItemSpacing.x;

    ImGui::BeginChild("MidLeft", ImVec2(left_w, 315.0f), false);
    RenderTelemetryCard(state);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("MidRight", ImVec2(right_w, 315.0f), false);
    RenderControlsCard(state);
    ImGui::EndChild();

    ImGui::Spacing();
    RenderVuMetersCard(state);
}

void Dashboard::RenderHeader(AppState& state) {
    // Rendered in top navigation bar by main container
}

void Dashboard::RenderHardwareCard(AppState& state) {
    Theme::BeginCard("HwCard");

    // Row 1: Header + Badges
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 999.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 4.0f));
    ImGui::BeginChild("HwStatusPill", ImVec2(145.0f, 26.0f), false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 cp = ImGui::GetCursorScreenPos();
    dl->AddCircleFilled(ImVec2(cp.x + 8.0f, cp.y + 9.0f), 4.5f, Color::ToU32(Color::StatusOk));
    ImGui::SetCursorPosX(22.0f);
    ImGui::TextColored(Color::StatusOk, "● CONNECTED");
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::TextColored(Color::TextSecondary, "HARDWARE LINK");
    ImGui::SameLine();
    ImGui::TextColored(Color::IconGray, "/");
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "RP2350-Zero");
    ImGui::SameLine();
    ImGui::TextColored(Color::Tertiary, "FW %s", state.hw_info.firmware_version.c_str());

    // Right-aligned badges
    float avail_r = ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(avail_r - 380.0f);
    ImGui::TextColored(Color::Tertiary, "%s %s", ICON_MD_CABLE, state.hw_info.com_port.c_str());
    ImGui::SameLine();
    ImGui::TextColored(Color::StatusInfo, "%s EP 0x03 OUT", ICON_MD_USB);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextSecondary, "VID/PID: 0x%04X : 0x%04X", state.hw_info.vid, state.hw_info.pid);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Row 2: 5 Stat Tiles
    float tile_w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 4) / 5.0f;
    Theme::StatTile("Uptime", "02:14:09", "Active", Color::TextPrimary, tile_w);
    ImGui::SameLine();
    Theme::StatTile("Bridge Heartbeat", "♥ 1 Hz OK", "0 drop", Color::StatusOk, tile_w);
    ImGui::SameLine();
    Theme::StatTile("PIO I2S Engine", "READY", "@ 96 kHz", Color::Tertiary, tile_w);
    ImGui::SameLine();
    Theme::StatTile("GP8 Hardware MUX", "HIGH", "Direct Bypass", Color::StatusInfo, tile_w);
    ImGui::SameLine();
    Theme::StatTile("DSP Sync Lock", "LOCKED", "PS9850 ASRC", Color::TextPrimary, tile_w);

    Theme::EndCard();
}

void Dashboard::RenderTelemetryCard(AppState& state) {
    Theme::BeginCard("StreamEngineCard", ImVec2(0, 315.0f));

    ImGui::TextColored(Color::PrimaryContainer, "%s", ICON_MD_TUNE);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "STREAM & FORMAT ENGINE");

    // Mode Selector Pills
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 240.0f);
    const char* modes[] = { "Auto: Windows", "Best", "Adaptive", "Manual" };
    for (int i = 0; i < 4; ++i) {
        bool is_sel = (state.settings.format_mode == i);
        if (is_sel) ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
        else ImGui::PushStyleColor(ImGuiCol_Button, Color::Surface2);

        char mode_id[32];
        std::snprintf(mode_id, sizeof(mode_id), "%s##m%d", modes[i], i);
        if (ImGui::Button(mode_id)) {
            state.settings.format_mode = i;
        }
        ImGui::PopStyleColor();
        if (i < 3) ImGui::SameLine();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 2x2 Grid parameter tiles
    float sub_w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    // Tile 1: Sample Rate
    char lbl1[64]; std::snprintf(lbl1, sizeof(lbl1), "%s Sample Rate", ICON_MD_SPEED);
    Theme::StatTile(lbl1, "96.0 kHz", "Master Bit-Clock: 6.144 MHz", Color::TextPrimary, sub_w);
    ImGui::SameLine();

    // Tile 2: Bit Depth
    char lbl2[64]; std::snprintf(lbl2, sizeof(lbl2), "%s Bit Depth", ICON_MD_MEMORY);
    Theme::StatTile(lbl2, "32-bit Container", "24-bit Valid High Precision Audio", Color::TextPrimary, sub_w);

    // Tile 3: Channel Topology
    char lbl3[64]; std::snprintf(lbl3, sizeof(lbl3), "%s Channel Topology", ICON_MD_SURROUND_SOUND);
    Theme::StatTile(lbl3, "5.1 Surround", "6 Discrete I2S Channels (3x Data)", Color::TextPrimary, sub_w);
    ImGui::SameLine();

    // Tile 4: Lossless Transport
    char lbl4[64]; std::snprintf(lbl4, sizeof(lbl4), "%s Lossless Transport", ICON_MD_COMPRESS);
    Theme::StatTile(lbl4, "Delta-Rice FLAC Order-4", "~58% Bandwidth Reduction", Color::Tertiary, sub_w);

    ImGui::Spacing();
    // Capture device bar
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::BeginChild("CapDevBar", ImVec2(0, 32.0f), true);
    ImGui::TextColored(Color::StatusOk, "%s", ICON_MD_SPEAKER);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextSecondary, "Capture Device:");
    ImGui::SameLine();
    if (!state.available_devices.empty() && state.active_device_index < (int)state.available_devices.size()) {
        ImGui::TextColored(Color::TextPrimary, "%s", state.available_devices[state.active_device_index].name.c_str());
    } else {
        ImGui::TextColored(Color::TextPrimary, "WASAPI Loopback (Default Output Device)");
    }
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120.0f);
    ImGui::TextColored(Color::StatusOk, "SHARED 0dBFS");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    Theme::EndCard();
}

void Dashboard::RenderControlsCard(AppState& state) {
    Theme::BeginCard("LinkBudgetCard", ImVec2(0, 315.0f));

    ImGui::TextColored(Color::StatusInfo, "%s", ICON_MD_EQUALIZER);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "LINK BUDGET & BUFFER");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80.0f);
    ImGui::TextColored(Color::StatusOk, "HEALTHY");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Link Usage Envelope Progress Bar
    ImGui::TextColored(Color::TextSecondary, "USB Full-Speed Iso/Bulk Safe Envelope");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90.0f);
    ImGui::TextColored(Color::TextPrimary, "%d%% Utilized", state.telemetry.link_usage_pct);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pb_pos = ImGui::GetCursorScreenPos();
    float pb_w = ImGui::GetContentRegionAvail().x;
    float pb_h = 10.0f;

    dl->AddRectFilled(pb_pos, ImVec2(pb_pos.x + pb_w, pb_pos.y + pb_h), Color::ToU32(Color::SurfaceDim), 5.0f);
    float active_w = pb_w * (state.telemetry.link_usage_pct / 100.0f);
    dl->AddRectFilledMultiColor(
        pb_pos, ImVec2(pb_pos.x + active_w, pb_pos.y + pb_h),
        Color::ToU32(Color::Tertiary), Color::ToU32(Color::StatusInfo),
        Color::ToU32(Color::StatusInfo), Color::ToU32(Color::Tertiary)
    );
    ImGui::Dummy(ImVec2(pb_w, pb_h + 4.0f));

    ImGui::TextColored(Color::Tertiary, "%.0f KB/s Current Rate", state.telemetry.throughput_kbs);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 180.0f);
    ImGui::TextColored(Color::TextSecondary, "1,050 KB/s Hardware Ceiling");

    ImGui::Spacing();
    // 4 Micro Telemetry Tiles (2x2)
    float m_w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    Theme::StatTile("Target Latency", "3.8 ms", "MMCSS Pro Audio", Color::TextPrimary, m_w);
    ImGui::SameLine();
    Theme::StatTile("Ring Buffer Fill", "62% Fill", "48 frames / 0.5ms sub", Color::TextPrimary, m_w);

    Theme::StatTile("Clock Jitter", "< 0.2 ms", "Drift: +0.003 ppm", Color::StatusOk, m_w);
    ImGui::SameLine();
    Theme::StatTile("Buffer Underruns", "0 / hr", "Zero Loss / Glitch-Free", Color::StatusOk, m_w);

    Theme::EndCard();
}

void Dashboard::RenderVuMetersCard(AppState& state) {
    Theme::BeginCard("VuMetersCard");

    ImGui::TextColored(Color::PrimaryContainer, "%s", ICON_MD_GRAPHIC_EQ);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "6-CHANNEL LIVE VU METERS");
    ImGui::SameLine();
    ImGui::TextColored(Color::TextSecondary, "| True-Peak DBFS Telemetry with Instantaneous Hold Markers");

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 240.0f);
    ImGui::TextColored(Color::StatusOk, "● CLIP: NONE (Clean Headroom)");
    ImGui::SameLine();
    char reset_lbl[32];
    std::snprintf(reset_lbl, sizeof(reset_lbl), "%s Reset Peaks", ICON_MD_REFRESH);
    if (ImGui::Button(reset_lbl)) {
        for (int i = 0; i < 6; ++i) state.meters[i].peak_hold_db = -60.0f;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Scale header ticks: -60, -48, -36, -24, -18, -12, -6, -3, 0 dBFS
    ImGui::SetCursorPosX(160.0f);
    float meter_track_w = ImGui::GetContentRegionAvail().x - 110.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 tick_pos = ImGui::GetCursorScreenPos();
    const char* ticks[] = { "-60 dB", "-48", "-36", "-24", "-18", "-12", "-6", "-3", "0 dBFS" };
    float tick_dbs[] =    { -60.0f, -48.0f, -36.0f, -24.0f, -18.0f, -12.0f, -6.0f, -3.0f, 0.0f };

    for (int t = 0; t < 9; ++t) {
        float norm = (tick_dbs[t] + 60.0f) / 60.0f;
        float tx = tick_pos.x + (meter_track_w * norm);
        ImU32 col = (t >= 8) ? Color::ToU32(Color::StatusError) : (t >= 6) ? Color::ToU32(Color::StatusWarn) : Color::ToU32(Color::IconGray);
        dl->AddText(ImVec2(tx - 12.0f, tick_pos.y), col, ticks[t]);
    }
    ImGui::Dummy(ImVec2(0, 16.0f));

    // 6 Horizontal Channel Strips (FL, FR, C, LFE, SL, SR)
    struct ChDef {
        const char* tag;
        const char* name;
    };
    ChDef chs[6] = {
        { "FL",  "Front L" },
        { "FR",  "Front R" },
        { "C",   "Center" },
        { "LFE", "Sub 120Hz" },
        { "SL",  "Surr L" },
        { "SR",  "Surr R" }
    };

    for (int i = 0; i < 6; ++i) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));

        char strip_id[32];
        std::snprintf(strip_id, sizeof(strip_id), "Strip_%d", i);
        ImGui::BeginChild(strip_id, ImVec2(0, 36.0f), true, ImGuiWindowFlags_AlwaysUseWindowPadding);

        // Tag + Name
        ImGui::TextColored(Color::TextPrimary, "%s", chs[i].tag);
        ImGui::SameLine();
        ImGui::TextColored(Color::TextSecondary, "%s", chs[i].name);

        // Meter Bar
        ImGui::SameLine(160.0f);
        ImVec2 bar_p = ImGui::GetCursorScreenPos();
        float bar_w = ImGui::GetContentRegionAvail().x - 100.0f;
        float bar_h = 18.0f;

        // Background track
        dl->AddRectFilled(bar_p, ImVec2(bar_p.x + bar_w, bar_p.y + bar_h), Color::ToU32(Color::SurfaceDim), 3.0f);

        // Ticks visual backdrop lines
        for (int t = 0; t < 9; ++t) {
            float norm = (tick_dbs[t] + 60.0f) / 60.0f;
            float tx = bar_p.x + (bar_w * norm);
            ImU32 tick_col = (t >= 8) ? Color::ToU32(Color::WithAlpha(Color::StatusError, 0.4f)) : 
                             (t >= 6) ? Color::ToU32(Color::WithAlpha(Color::StatusWarn, 0.4f)) : 
                             Color::ToU32(Color::WithAlpha(Color::TextPrimary, 0.15f));
            dl->AddLine(ImVec2(tx, bar_p.y), ImVec2(tx, bar_p.y + bar_h), tick_col, 1.0f);
        }

        // Active fill
        float db = state.meters[i].peak_db;
        float norm = (std::clamp)((db + 60.0f) / 60.0f, 0.0f, 1.0f);
        float fill_w = bar_w * norm;

        if (fill_w > 1.0f) {
            ImU32 top_col = (db > -3.0f) ? Color::ToU32(Color::StatusError) : 
                            (db > -12.0f) ? Color::ToU32(Color::StatusWarn) : Color::ToU32(Color::StatusOk);
            dl->AddRectFilledMultiColor(
                bar_p, ImVec2(bar_p.x + fill_w, bar_p.y + bar_h),
                Color::ToU32(Color::StatusOk), top_col, top_col, Color::ToU32(Color::StatusOk)
            );
        }

        // Peak Hold vertical line
        float pk = state.meters[i].peak_hold_db;
        float pk_norm = (std::clamp)((pk + 60.0f) / 60.0f, 0.0f, 1.0f);
        float pk_x = bar_p.x + (bar_w * pk_norm);
        ImU32 pk_col = (pk > -3.0f) ? Color::ToU32(Color::StatusError) : 
                       (pk > -12.0f) ? Color::ToU32(Color::StatusWarn) : Color::ToU32(Color::StatusOk);
        dl->AddLine(ImVec2(pk_x, bar_p.y), ImVec2(pk_x, bar_p.y + bar_h), pk_col, 2.0f);

        // Numeric Readout on right
        ImGui::SameLine(160.0f + bar_w + 12.0f);
        if (Fonts::Mono) ImGui::PushFont(Fonts::Mono);
        if (db <= -59.5f) {
            ImGui::TextColored(Color::TextSecondary, "-inf dB");
        } else {
            ImGui::TextColored(Color::TextPrimary, "%.1f dB", db);
        }
        if (Fonts::Mono) ImGui::PopFont();

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // Bottom Action Controls (Exact match to Stitch code.html bottom action bar)
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    bool is_streaming = state.is_streaming.load();
    float btn_h = 38.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);

    if (is_streaming) {
        ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color::LgRedHover);
        char stop_lbl[64];
        std::snprintf(stop_lbl, sizeof(stop_lbl), "%s  Stop Stream", ICON_MD_STOP_CIRCLE);
        if (ImGui::Button(stop_lbl, ImVec2(160.0f, btn_h))) {
            state.is_streaming.store(false);
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        ImGui::TextColored(Color::StatusOk, "● Active Transmission");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, Color::StatusOk);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color::Tertiary);
        char start_lbl[64];
        std::snprintf(start_lbl, sizeof(start_lbl), "%s  Start Stream", ICON_MD_PLAY_CIRCLE);
        if (ImGui::Button(start_lbl, ImVec2(160.0f, btn_h))) {
            state.is_streaming.store(true);
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        ImGui::TextColored(Color::TextSecondary, "○ Stream Idle");
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 390.0f);

    // Quick Pill 1: 5.1 Speaker Sweep
    char swp_lbl[64];
    std::snprintf(swp_lbl, sizeof(swp_lbl), "%s 5.1 Speaker Sweep", ICON_MD_VOLUME_UP);
    if (ImGui::Button(swp_lbl, ImVec2(180.0f, btn_h))) {
        state.current_tab.store(NavTab::TEST);
    }

    // Quick Pill 2: Mute Master
    ImGui::SameLine();
    char mute_lbl[64];
    if (state.settings.mute_master) {
        ImGui::PushStyleColor(ImGuiCol_Button, Color::StatusError);
        std::snprintf(mute_lbl, sizeof(mute_lbl), "%s Muted", ICON_MD_VOLUME_OFF);
        if (ImGui::Button(mute_lbl, ImVec2(100.0f, btn_h))) {
            state.settings.mute_master = false;
        }
        ImGui::PopStyleColor();
    } else {
        std::snprintf(mute_lbl, sizeof(mute_lbl), "%s Mute", ICON_MD_VOLUME_OFF);
        if (ImGui::Button(mute_lbl, ImVec2(100.0f, btn_h))) {
            state.settings.mute_master = true;
        }
    }

    // Quick Pill 3: Settings
    ImGui::SameLine();
    char set_lbl[64];
    std::snprintf(set_lbl, sizeof(set_lbl), "%s", ICON_MD_SETTINGS);
    if (ImGui::Button(set_lbl, ImVec2(40.0f, btn_h))) {
        state.current_tab.store(NavTab::SETTINGS);
    }

    ImGui::PopStyleVar();

    // Hardware Synchronized Footer Subtext
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(Color::Tertiary, "%s", ICON_MD_VERIFIED);
    ImGui::SameLine();
    ImGui::TextColored(Color::IconGray, "Bit-Perfect stream synchronized to IC602 Pulsus PS9850 ASRC. Zero resample artifacts.");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 320.0f);
    ImGui::TextColored(Color::IconGray, "FIFO DEPTH: 1024 BYTES │ LATENCY OFFSET: 0.00ms");

    Theme::EndCard();
}

} // namespace ht306::ui
