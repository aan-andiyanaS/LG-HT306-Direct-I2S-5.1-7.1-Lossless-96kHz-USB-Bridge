#include "ui_routing.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "imgui.h"
#include <cstdio>
#include <cmath>

namespace ht306::ui {

void Routing::Render(AppState& state) {
    // 1. Hero Header Banner
    Theme::BeginCard("RoutingHeroCard");
    ImGui::TextColored(Color::TextPrimary, "I2S Crosspoint Matrix & Channel Mapping");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::BeginChild("I2sBusPill", ImVec2(140.0f, 22.0f), false);
    ImGui::SetCursorPos(ImVec2(6.0f, 2.0f));
    ImGui::TextColored(Color::Tertiary, "RP2350 PIO I2S Bus");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::BeginChild("DacChPill", ImVec2(200.0f, 22.0f), false);
    ImGui::SetCursorPos(ImVec2(6.0f, 2.0f));
    ImGui::TextColored(Color::TextSecondary, "6x Discrete Physical DAC Channels");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::TextColored(Color::TextSecondary, "Direct routing matrix between Windows WASAPI capture streams and LG HT306 physical DAC I2S lines (TAS5508 / PS9850). Correct swapped physical wiring without resoldering.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Quick Action Buttons
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    if (ImGui::Button("Quick Swap SL <-> SR", ImVec2(175.0f, 28.0f))) {
        int tmp = state.settings.routing_map[4];
        state.settings.routing_map[4] = state.settings.routing_map[5];
        state.settings.routing_map[5] = tmp;
    }
    ImGui::SameLine();
    if (ImGui::Button("1:1 Straight Passthrough", ImVec2(185.0f, 28.0f))) {
        for (int i = 0; i < 6; ++i) state.settings.routing_map[i] = i;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to Factory Map", ImVec2(165.0f, 28.0f))) {
        for (int i = 0; i < 6; ++i) state.settings.routing_map[i] = i;
    }
    ImGui::PopStyleVar();

    Theme::EndCard();

    ImGui::Spacing();

    // 2. Middle Section: Mode & Synthesizer (Left) vs Physical Pinout (Right)
    float total_w = ImGui::GetContentRegionAvail().x;
    float col_w = (total_w - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    // LEFT: Mode & Synthesizer Engine
    Theme::BeginCard("SynthesizerCard", ImVec2(col_w, 290.0f));
    ImGui::TextColored(Color::PrimaryContainer, "%s", ICON_MD_TUNE);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "Mode & Synthesizer Engine");
    ImGui::SameLine(col_w - 140.0f);
    ImGui::TextColored(Color::IconGray, "5.2 Upmix/Downmix");

    ImGui::Spacing();

    // Stream info bar
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::BeginChild("DetectedStreamBar", ImVec2(0, 26.0f), false);
    ImGui::SetCursorPos(ImVec2(8.0f, 4.0f));
    ImGui::TextColored(Color::StatusOk, "●");
    ImGui::SameLine();
    ImGui::TextColored(Color::TextSecondary, "Detected Windows Stream:");
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "Windows 5.1 Surround");
    ImGui::SameLine(col_w - 180.0f);
    ImGui::TextColored(Color::Tertiary, "32-bit Float / 96 kHz");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // 4 Mode Selection Cards (2x2)
    float mode_card_w = (col_w - 40.0f) * 0.5f;
    struct UpmixModeDef {
        RoutingPreset preset;
        const char* title;
        const char* desc;
    };
    UpmixModeDef upmix_modes[4] = {
        { RoutingPreset::DISCRETE_1TO1, "Direct 1:1 Discrete", "Bit-Perfect Passthrough. Untouched multichannel buffers." },
        { RoutingPreset::STEREO_UPMIX,  "Stereo 2.0 -> 5.1 Upmix", "Matrix surround synthesizer for 2-channel legacy tracks." },
        { RoutingPreset::DOWNMIX_71,    "7.1 -> 5.1 Downmix", "Foldback surrounds into side channels via ITU-R profile." },
        { RoutingPreset::DUAL_MONO,     "Dual-Mono Front Pair", "Summed L+R centered phase alignment for voice checks." }
    };

    for (int m = 0; m < 4; ++m) {
        bool is_sel = (state.settings.routing_preset == upmix_modes[m].preset);
        if (is_sel) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::WithAlpha(Color::PrimaryContainer, 0.25f));
            ImGui::PushStyleColor(ImGuiCol_Border, Color::PrimaryContainer);
        } else {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
            ImGui::PushStyleColor(ImGuiCol_Border, Color::Border);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);

        char mode_card_id[32];
        std::snprintf(mode_card_id, sizeof(mode_card_id), "ModeCard_%d", m);
        ImGui::BeginChild(mode_card_id, ImVec2(mode_card_w, 65.0f), true);

        ImGui::TextColored(Color::TextPrimary, "%s", upmix_modes[m].title);
        ImGui::TextColored(Color::TextSecondary, "%s", upmix_modes[m].desc);

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
            state.settings.routing_preset = upmix_modes[m].preset;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        if (m % 2 == 0) ImGui::SameLine();
        else ImGui::Spacing();
    }

    // Downmix Attenuation Bar
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::SurfaceContainerLow);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::BeginChild("AttenBar", ImVec2(0, 26.0f), false);
    ImGui::SetCursorPos(ImVec2(8.0f, 4.0f));
    ImGui::TextColored(Color::StatusWarn, "Downmix Attenuation:");
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "%.1f dB ITU-R", state.settings.downmix_attenuation_db);
    ImGui::SameLine(col_w - 280.0f);
    ImGui::TextColored(Color::IconGray, "Center & Surrounds scaled to prevent DAC clipping");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    Theme::EndCard();

    ImGui::SameLine();

    // RIGHT: Physical I2S Line Status & Pinout
    Theme::BeginCard("PinoutCard", ImVec2(col_w, 290.0f));
    ImGui::TextColored(Color::StatusInfo, "%s", ICON_MD_MEMORY);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "Physical I2S Line Status & Pinout");
    ImGui::SameLine(col_w - 150.0f);
    ImGui::TextColored(Color::Tertiary, "● PIO SM0-2 MUX OK");

    ImGui::Spacing();

    // 4 Pinout tiles (2x2)
    float pin_w = (col_w - 40.0f) * 0.5f;

    // Bus 0
    Theme::StatTile("Data Bus 0 (GP4)", "ADATA0: FL + FR", "TAS5508 Pins 22 / 23 (Left/Right Front)", Color::TextPrimary, pin_w);
    ImGui::SameLine();
    // Bus 1
    Theme::StatTile("Data Bus 1 (GP5)", "ADATA1: C + LFE", "TAS5508 Pins 24 / 25 (Center / Low-Freq Sub)", Color::TextPrimary, pin_w);

    // Bus 2
    Theme::StatTile("Data Bus 2 (GP6)", "ADATA2: SL + SR", "TAS5508 Pins 26 / 27 (Surround Rear L/R)", Color::TextPrimary, pin_w);
    ImGui::SameLine();
    // Clock Gen
    Theme::StatTile("Clock Gen (GP2 / GP3)", "6.144 MHz / 96k", "64fs Frame Sync Lock (RP2350 PIO)", Color::Tertiary, pin_w);

    ImGui::Spacing();
    // Lock Subtext
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::SurfaceContainerLow);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::BeginChild("LockBar", ImVec2(0, 26.0f), false);
    ImGui::SetCursorPos(ImVec2(8.0f, 4.0f));
    ImGui::TextColored(Color::StatusOk, "●");
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "All 3x I2S Data Lines MUXed to High & Locked");
    ImGui::SameLine(col_w - 130.0f);
    ImGui::TextColored(Color::TextSecondary, "0 JITTER DROPS");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    Theme::EndCard();

    ImGui::Spacing();

    // 3. 6x6 Physical Crosspoint Matrix Table
    Theme::BeginCard("MatrixCard");
    ImGui::TextColored(Color::PrimaryContainer, "%s", ICON_MD_GRID_4X4);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "6x6 Physical Crosspoint Matrix");
    ImGui::SameLine();
    ImGui::TextColored(Color::TextSecondary, "- Click any intersection to toggle routing path from host audio device directly into hardware DAC registers.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const char* in_labels[6] = { "FL (Ch 1)", "FR (Ch 2)", "C (Ch 3)", "LFE (Ch 4)", "SL (Ch 5)", "SR (Ch 6)" };
    const char* out_labels[6] = { "FL (TAS5508 AD0-L)", "FR (TAS5508 AD0-R)", "C (TAS5508 AD1-L)", "LFE (TAS5508 AD1-R)", "SL (TAS5508 AD2-L)", "SR (TAS5508 AD2-R)" };

    if (ImGui::BeginTable("MatrixTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        // Table Header
        ImGui::TableSetupColumn("Physical DAC Line", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        for (int i = 0; i < 6; ++i) {
            ImGui::TableSetupColumn(in_labels[i], ImGuiTableColumnFlags_WidthStretch);
        }
        ImGui::TableHeadersRow();

        // 6 Output Rows
        for (int out = 0; out < 6; ++out) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, 34.0f);

            // Col 0: Output Line Name
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(Color::TextPrimary, "%s", out_labels[out]);

            // Cols 1..6: Input Radio Cell
            for (int in = 0; in < 6; ++in) {
                ImGui::TableSetColumnIndex(in + 1);
                bool is_mapped = (state.settings.routing_map[out] == in);

                char cell_btn_id[32];
                std::snprintf(cell_btn_id, sizeof(cell_btn_id), "%s##mat_%d_%d", is_mapped ? "CONNECTED" : "──", out, in);

                if (is_mapped) {
                    ImGui::PushStyleColor(ImGuiCol_Button, Color::StatusOk);
                    ImGui::PushStyleColor(ImGuiCol_Text, Color::TextPrimary);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, Color::Surface2);
                    ImGui::PushStyleColor(ImGuiCol_Text, Color::IconGray);
                }

                if (ImGui::Button(cell_btn_id, ImVec2(-FLT_MIN, 26.0f))) {
                    state.settings.routing_map[out] = in;
                }

                ImGui::PopStyleColor(2);
            }
        }
        ImGui::EndTable();
    }

    Theme::EndCard();
}

} // namespace ht306::ui
