#include "ui_sweep_test.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "imgui.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace ht306::ui {

static void DrawRoomCanvas(AppState& state, float canvas_w, float canvas_h) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();

    // 1. Room Background
    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_w, canvas_pos.y + canvas_h),
                             Color::ToU32(Color::Surface2), 12.0f);
    draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_w, canvas_pos.y + canvas_h),
                       Color::ToU32(Color::Border), 12.0f);

    // Center coordinates
    float cx = canvas_pos.x + canvas_w * 0.5f;
    float cy = canvas_pos.y + canvas_h * 0.52f;

    // Room boundaries (dashed or subtle grid)
    draw_list->AddCircle(ImVec2(cx, cy), 140.0f, Color::ToU32(Color::WithAlpha(Color::Border, 0.4f)), 48, 1.0f);
    draw_list->AddCircle(ImVec2(cx, cy), 220.0f, Color::ToU32(Color::WithAlpha(Color::Border, 0.25f)), 64, 1.0f);

    // 2. Sweet-Spot Listener
    draw_list->AddCircleFilled(ImVec2(cx, cy + 25.0f), 18.0f, Color::ToU32(Color::SurfaceContainerLowest));
    draw_list->AddCircle(ImVec2(cx, cy + 25.0f), 18.0f, Color::ToU32(Color::PrimaryContainer), 0, 2.0f);
    draw_list->AddText(ImVec2(cx - 26.0f, cy + 50.0f), Color::ToU32(Color::TextSecondary), "Listener (Sweet Spot)");

    // 3. 6 Speaker Placements
    struct SpeakerCoord {
        int ch_idx;
        const char* name;
        const char* desc;
        float x;
        float y;
    };

    SpeakerCoord speakers[6] = {
        { 0, "FL",  "Front Left",   cx - 220.0f, cy - 110.0f },
        { 2, "C",   "Center Voice", cx,          cy - 145.0f },
        { 1, "FR",  "Front Right",  cx + 220.0f, cy - 110.0f },
        { 5, "SR",  "Surround R",   cx + 240.0f, cy + 90.0f  },
        { 4, "SL",  "Surround L",   cx - 240.0f, cy + 90.0f  },
        { 3, "LFE", "Sub 120Hz",    cx - 85.0f,  cy - 60.0f  }
    };

    int active_ch = state.sweep_active_channel.load();
    float time_sec = (float)ImGui::GetTime();

    // Auto-sweep sequence step handler
    if (state.sweep_running.load() && active_ch >= 0) {
        // Advance sequence every 1.5 seconds if running
        static float s_last_step = 0.0f;
        if (time_sec - s_last_step > 1.5f) {
            s_last_step = time_sec;
            int order[6] = { 0, 2, 1, 5, 4, 3 }; // FL -> C -> FR -> SR -> SL -> LFE
            int cur_idx = 0;
            for (int k = 0; k < 6; ++k) {
                if (order[k] == active_ch) { cur_idx = k; break; }
            }
            state.sweep_active_channel.store(order[(cur_idx + 1) % 6]);
        }
    }

    for (int i = 0; i < 6; ++i) {
        bool is_active = (active_ch == speakers[i].ch_idx);
        ImVec2 spk_pos = ImVec2(speakers[i].x, speakers[i].y);
        float radius = (speakers[i].ch_idx == 3) ? 24.0f : 20.0f; // Subwoofer slightly larger

        // Check mouse click inside speaker node to manually test
        ImVec2 m = ImGui::GetIO().MousePos;
        float dist = std::sqrt((m.x - spk_pos.x) * (m.x - spk_pos.x) + (m.y - spk_pos.y) * (m.y - spk_pos.y));
        if (dist <= radius && ImGui::IsMouseClicked(0)) {
            if (active_ch == speakers[i].ch_idx) state.sweep_active_channel.store(-1);
            else state.sweep_active_channel.store(speakers[i].ch_idx);
        }

        if (is_active) {
            // Animated acoustic pressure ping rings
            for (int r = 0; r < 3; ++r) {
                float pulse = std::fmod(time_sec * 2.2f + (r * 0.33f), 1.0f);
                float ring_radius = radius + pulse * 45.0f;
                float ring_alpha = (1.0f - pulse) * 0.85f;
                draw_list->AddCircle(spk_pos, ring_radius,
                                     Color::ToU32(Color::WithAlpha(Color::PrimaryContainer, ring_alpha)), 32, 2.0f);
            }

            draw_list->AddCircleFilled(spk_pos, radius, Color::ToU32(Color::PrimaryContainer));
            draw_list->AddCircle(spk_pos, radius, Color::ToU32(Color::TextPrimary), 0, 2.5f);
        } else {
            draw_list->AddCircleFilled(spk_pos, radius, Color::ToU32(Color::SurfaceContainerLowest));
            draw_list->AddCircle(spk_pos, radius, Color::ToU32(Color::Border), 0, 1.5f);
        }

        // Channel Tag text
        ImVec2 lbl_sz = ImGui::CalcTextSize(speakers[i].name);
        ImU32 txt_col = is_active ? Color::ToU32(Color::TextPrimary) : Color::ToU32(Color::TextSecondary);
        draw_list->AddText(ImVec2(spk_pos.x - lbl_sz.x * 0.5f, spk_pos.y - lbl_sz.y * 0.5f), txt_col, speakers[i].name);

        // Subtitle below speaker
        ImVec2 sub_sz = ImGui::CalcTextSize(speakers[i].desc);
        draw_list->AddText(ImVec2(spk_pos.x - sub_sz.x * 0.5f, spk_pos.y + radius + 4.0f), Color::ToU32(Color::IconGray), speakers[i].desc);
    }

    ImGui::Dummy(ImVec2(canvas_w, canvas_h));
}

void SweepTest::RenderPage(AppState& state) {
    // 1. Top Hero Header Bar
    Theme::BeginCard("SweepHeroCard");

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 999.0f);
    ImGui::BeginChild("SeqEnginePill", ImVec2(280.0f, 24.0f), false);
    ImGui::SetCursorPos(ImVec2(10.0f, 3.0f));
    ImGui::TextColored(Color::TextPrimary, "ACOUSTIC VERIFICATION / SEQUENCE ENGINE");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 999.0f);
    ImGui::BeginChild("JitterPill", ImVec2(160.0f, 24.0f), false);
    ImGui::SetCursorPos(ImVec2(10.0f, 3.0f));
    ImGui::TextColored(Color::StatusOk, "● DA-I2S LOW-JITTER");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::TextColored(Color::TextPrimary, "5.1 Speaker Sweep & Acoustic Alignment");
    ImGui::TextColored(Color::TextSecondary, "Automated sequence sweep (FL -> C -> FR -> SR -> SL -> LFE), pink noise calibration, and polarity verification to confirm physical speaker wiring and DAC line integrity.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Quick Action Controls
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    bool is_sweeping = state.sweep_running.load();
    if (is_sweeping) {
        ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
        char btn_lbl[64];
        std::snprintf(btn_lbl, sizeof(btn_lbl), "%s  Pause Sweep", ICON_MD_PAUSE);
        if (ImGui::Button(btn_lbl, ImVec2(180.0f, 32.0f))) {
            state.sweep_running.store(false);
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
        char btn_lbl[64];
        std::snprintf(btn_lbl, sizeof(btn_lbl), "%s  Start 5.1 Auto-Sweep", ICON_MD_PLAY_ARROW);
        if (ImGui::Button(btn_lbl, ImVec2(210.0f, 32.0f))) {
            state.sweep_running.store(true);
            if (state.sweep_active_channel.load() < 0) state.sweep_active_channel.store(0);
        }
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    if (ImGui::Button("Abort Test", ImVec2(120.0f, 32.0f))) {
        state.sweep_running.store(false);
        state.sweep_active_channel.store(-1);
    }

    ImGui::SameLine();
    if (ImGui::Button("Mute All", ImVec2(110.0f, 32.0f))) {
        state.settings.mute_master = true;
    }
    ImGui::PopStyleVar();

    Theme::EndCard();

    ImGui::Spacing();

    // 2. Sweep & Signal Generator Controller Bar
    Theme::BeginCard("SignalGenCard");
    ImGui::TextColored(Color::IconGray, "TEST SIGNAL EXCITATION");
    ImGui::Spacing();

    const char* sigs[] = {
        "Chirp Sweep (20Hz - 20kHz)",
        "Pink Noise (Uncorrelated)",
        "Sine Tone (1 kHz Ref)",
        "Dual-Tone IMD (60Hz + 7kHz)",
        "Polarity Pulse (Phase)"
    };

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    for (int s = 0; s < 5; ++s) {
        bool is_sel = (state.test_signal_type == s);
        if (is_sel) ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
        else ImGui::PushStyleColor(ImGuiCol_Button, Color::Surface2);

        char sig_id[64];
        std::snprintf(sig_id, sizeof(sig_id), "%s##sig_%d", sigs[s], s);
        if (ImGui::Button(sig_id)) {
            state.test_signal_type = s;
        }
        ImGui::PopStyleColor();
        if (s < 4) ImGui::SameLine();
    }
    ImGui::PopStyleVar();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Calibrated Level Stepper
    ImGui::TextColored(Color::IconGray, "CALIBRATED TEST OUTPUT:");
    ImGui::SameLine();
    if (ImGui::Button("-1.0##dbfs", ImVec2(35.0f, 24.0f))) state.test_level_dbfs = (std::max)(-60.0f, state.test_level_dbfs - 1.0f);
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::BeginChild("DbfsPill", ImVec2(100.0f, 24.0f), false);
    ImGui::SetCursorPos(ImVec2(12.0f, 3.0f));
    ImGui::TextColored(Color::Tertiary, "%.1f dBFS", state.test_level_dbfs);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SameLine();
    if (ImGui::Button("+1.0##dbfs", ImVec2(35.0f, 24.0f))) state.test_level_dbfs = (std::min)(-12.0f, state.test_level_dbfs + 1.0f);

    ImGui::SameLine();
    ImGui::TextColored(Color::StatusWarn, "Safety Limiter @ -12 dBFS (EBU R128 Reference)");

    Theme::EndCard();

    ImGui::Spacing();

    // 3. Interactive Room Canvas
    Theme::BeginCard("RoomCanvasCard");
    ImGui::TextColored(Color::PrimaryContainer, "%s", ICON_MD_VOLUME_UP);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "Interactive 5.1 Surround Room Alignment Canvas");
    ImGui::SameLine();
    ImGui::TextColored(Color::TextSecondary, "- Click any speaker node to isolate and emit test signal.");

    ImGui::Spacing();
    float canvas_w = ImGui::GetContentRegionAvail().x;
    float canvas_h = 360.0f;
    DrawRoomCanvas(state, canvas_w, canvas_h);
    Theme::EndCard();
}

void SweepTest::RenderModal(AppState& state) {
    if (!state.sweep_running.load() && state.sweep_active_channel.load() < 0) {
        return;
    }
    // If not in TEST tab, modal dialog can be opened
    if (state.current_tab.load() == NavTab::TEST) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(720.0f, 540.0f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Color::Surface);
    ImGui::PushStyleColor(ImGuiCol_Border, Color::Border);

    if (ImGui::Begin("5.1 Acoustic Sweep Speaker Test", nullptr, ImGuiWindowFlags_NoCollapse)) {
        float w = ImGui::GetContentRegionAvail().x;
        float h = 380.0f;
        DrawRoomCanvas(state, w, h);

        ImGui::Spacing();
        if (ImGui::Button("Close Test Dialog", ImVec2(160.0f, 32.0f))) {
            state.sweep_running.store(false);
            state.sweep_active_channel.store(-1);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
}

} // namespace ht306::ui
