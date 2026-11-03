#include "ui_sweep_test.h"
#include "ui_theme.h"
#include "imgui.h"
#include <cmath>
#include <cstdio>

namespace ht306::ui {

void SweepTest::RenderModal(AppState& state) {
    if (!state.sweep_running.load()) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(680.0f, 520.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 340.0f, 
                                   ImGui::GetIO().DisplaySize.y * 0.5f - 260.0f), ImGuiCond_Appearing);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, Color::Surface);
    ImGui::PushStyleColor(ImGuiCol_Border, Color::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));

    if (ImGui::Begin("5.1 Acoustic Sweep Speaker Test", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(Color::TextPrimary, "SURROUND 5.1 SPEAKER POSITIONING & WIRING TEST");
        ImGui::TextColored(Color::TextSecondary, "Urutan Sweep: FL -> Center -> FR -> Surround Right -> Surround Left -> Subwoofer");
        ImGui::Separator();
        ImGui::Spacing();

        // Canvas for Speaker Placement
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        float canvas_w = ImGui::GetContentRegionAvail().x;
        float canvas_h = 290.0f;

        // Room background
        draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_w, canvas_pos.y + canvas_h),
                                 Color::ToU32(Color::Surface2), 10.0f);
        draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_w, canvas_pos.y + canvas_h),
                           Color::ToU32(Color::Border), 10.0f);

        // Center room point
        float cx = canvas_pos.x + canvas_w * 0.5f;
        float cy = canvas_pos.y + canvas_h * 0.52f;

        // Listener position in center
        draw_list->AddCircleFilled(ImVec2(cx, cy + 30.0f), 14.0f, Color::ToU32(Color::SurfaceContainerLowest));
        draw_list->AddCircle(ImVec2(cx, cy + 30.0f), 14.0f, Color::ToU32(Color::Border), 0, 1.5f);
        draw_list->AddText(ImVec2(cx - 22.0f, cy + 50.0f), Color::ToU32(Color::TextSecondary), "Listener");

        // Speaker positions
        struct SpeakerCoord {
            int ch_idx;
            const char* name;
            const char* desc;
            float x;
            float y;
        };

        SpeakerCoord speakers[6] = {
            { 0, "FL",  "Front L",   cx - 180.0f, cy - 85.0f },
            { 2, "C",   "Center",    cx,          cy - 105.0f },
            { 1, "FR",  "Front R",   cx + 180.0f, cy - 85.0f },
            { 5, "SR",  "Surround R",cx + 200.0f, cy + 65.0f },
            { 4, "SL",  "Surround L",cx - 200.0f, cy + 65.0f },
            { 3, "LFE", "Subwoofer", cx - 60.0f,  cy - 45.0f }
        };

        int active_ch = state.sweep_active_channel.load();
        float time_sec = (float)ImGui::GetTime();

        for (int i = 0; i < 6; ++i) {
            bool is_active = (active_ch == speakers[i].ch_idx);
            ImVec2 spk_pos = ImVec2(speakers[i].x, speakers[i].y);

            if (is_active) {
                // Animated pulse ring
                float pulse = std::fmod(time_sec * 3.0f, 1.0f);
                float ring_radius = 18.0f + pulse * 28.0f;
                float ring_alpha = 1.0f - pulse;
                draw_list->AddCircle(spk_pos, ring_radius, 
                                     Color::ToU32(Color::WithAlpha(Color::StatusOk, ring_alpha)), 24, 2.5f);

                draw_list->AddCircleFilled(spk_pos, 18.0f, Color::ToU32(Color::StatusOk));
                draw_list->AddCircle(spk_pos, 18.0f, Color::ToU32(Color::TextPrimary), 0, 2.0f);
            } else {
                draw_list->AddCircleFilled(spk_pos, 16.0f, Color::ToU32(Color::SurfaceContainerLowest));
                draw_list->AddCircle(spk_pos, 16.0f, Color::ToU32(Color::Border), 0, 1.5f);
            }

            ImVec2 lbl_sz = ImGui::CalcTextSize(speakers[i].name);
            ImU32 txt_col = is_active ? Color::ToU32(Color::TextPrimary) : Color::ToU32(Color::TextSecondary);
            draw_list->AddText(ImVec2(spk_pos.x - lbl_sz.x * 0.5f, spk_pos.y - lbl_sz.y * 0.5f), txt_col, speakers[i].name);

            // Sub-label
            ImVec2 sub_sz = ImGui::CalcTextSize(speakers[i].desc);
            draw_list->AddText(ImVec2(spk_pos.x - sub_sz.x * 0.5f, spk_pos.y + 18.0f), Color::ToU32(Color::IconGray), speakers[i].desc);
        }

        ImGui::Dummy(ImVec2(canvas_w, canvas_h + 10.0f));
        ImGui::Spacing();

        // Control buttons
        float btn_w = 140.0f;
        if (active_ch >= 0) {
            ImGui::PushStyleColor(ImGuiCol_Button, Color::StatusWarn);
            if (ImGui::Button("Stop Test", ImVec2(btn_w, 36.0f))) {
                state.sweep_active_channel.store(-1);
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
            if (ImGui::Button("Start Auto Sweep", ImVec2(btn_w, 36.0f))) {
                state.sweep_active_channel.store(0); // Start FL
            }
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        if (ImGui::Button("Tutup Dialog", ImVec2(btn_w, 36.0f))) {
            state.sweep_running.store(false);
            state.sweep_active_channel.store(-1);
        }

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 220.0f);
        ImGui::TextColored(Color::StatusWarn, "Test tone: 1.0 kHz @ -18 dBFS");
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

} // namespace ht306::ui
