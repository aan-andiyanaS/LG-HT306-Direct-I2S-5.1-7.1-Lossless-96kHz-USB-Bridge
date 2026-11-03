#include "ui_profiles.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "imgui.h"
#include <cstdio>

namespace ht306::ui {

void Profiles::Render(AppState& state) {
    // 1. Header & Global Actions Bar
    Theme::BeginCard("ProfilesHeroCard");
    ImGui::TextColored(Color::Tertiary, "DSP CONFIGURATION");
    ImGui::SameLine();
    ImGui::TextColored(Color::IconGray, "/");
    ImGui::SameLine();
    ImGui::TextColored(Color::PrimaryContainer, "APPLICATION DETECTOR");

    ImGui::Spacing();
    ImGui::TextColored(Color::TextPrimary, "Audio Profiles & Application Rules");
    ImGui::TextColored(Color::TextSecondary, "Manage hardware and DSP presets with low-jitter handoff. Automates discrete parameter swaps (lip-sync delays, upmix matrix, DAC master clocks) keyed to Windows foreground processes.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    if (ImGui::Button("Import JSON", ImVec2(120.0f, 28.0f))) {}
    ImGui::SameLine();
    if (ImGui::Button("Export Presets", ImVec2(130.0f, 28.0f))) {}
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
    if (ImGui::Button("+ Create New Profile", ImVec2(170.0f, 28.0f))) {}
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    Theme::EndCard();

    ImGui::Spacing();

    // 2. Active Profile Summary Banner
    Theme::BeginCard("ActiveBannerCard");
    ImGuiWindowFlags no_scroll = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 999.0f);
    ImGui::BeginChild("ProfileActivePill", ImVec2(205.0f, 22.0f), false, no_scroll);
    ImGui::SetCursorPos(ImVec2(8.0f, 2.0f));
    ImGui::TextColored(Color::StatusOk, "● PROFILE BROADCAST ACTIVE");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::PrimaryContainer);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 999.0f);
    ImGui::BeginChild("EnginePresetPill", ImVec2(185.0f, 22.0f), false, no_scroll);
    ImGui::SetCursorPos(ImVec2(8.0f, 2.0f));
    ImGui::TextColored(Color::TextPrimary, "CURRENT ENGINE PRESET");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::TextColored(Color::IconGray, "DSP Slot #01");

    ImGui::Spacing();
    ImGui::TextColored(Color::TextPrimary, "%s", state.settings.active_profile.c_str());
    ImGui::SameLine();
    ImGui::TextColored(Color::TextSecondary, "[ITU-R BS.775 Upmix Matrix • Active]");

    ImGui::Spacing();

    // Summary pills
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);

    auto DrawSummaryPill = [no_scroll](const char* id, const char* k, const char* v, const ImVec4& v_col, float w) {
        ImGui::BeginChild(id, ImVec2(w, 22.0f), false, no_scroll);
        ImGui::SetCursorPos(ImVec2(6.0f, 2.0f));
        ImGui::TextColored(Color::IconGray, "%s", k);
        ImGui::SameLine();
        ImGui::TextColored(v_col, "%s", v);
        ImGui::EndChild();
    };

    DrawSummaryPill("Pill1", "Mode:", state.settings.desktop_os_linked ? "Desktop OS" : "Bit-Perfect", Color::StatusInfo, 160.0f);
    ImGui::SameLine();
    DrawSummaryPill("Pill2", "Upmix:", "ITU-R 5.1 Matrix", Color::Tertiary, 165.0f);
    ImGui::SameLine();
    char d_buf[32]; std::snprintf(d_buf, sizeof(d_buf), "%d ms", state.settings.lip_sync_delay_ms);
    DrawSummaryPill("Pill3", "Lip-Sync:", d_buf, Color::StatusWarn, 115.0f);
    ImGui::SameLine();
    DrawSummaryPill("Pill4", "Clarity:", "+1.5 dB", Color::TextPrimary, 120.0f);
    ImGui::SameLine();
    DrawSummaryPill("Pill5", "Sub LPF:", "120 Hz Active", Color::StatusInfo, 135.0f);

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    Theme::EndCard();

    ImGui::Spacing();

    // 3. Grid Profile Cards (4 Cards)
    Theme::BeginCard("ProfileCardsGrid");
    ImGui::TextColored(Color::PrimaryContainer, "%s", ICON_MD_LAYERS);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "Available Audio Profiles");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float total_w = ImGui::GetContentRegionAvail().x;
    float card_w = (total_w - ImGui::GetStyle().ItemSpacing.x * 3) / 4.0f;
    float card_h = 175.0f;

    for (size_t p = 0; p < state.profiles.size(); ++p) {
        auto& item = state.profiles[p];
        bool is_active = (state.settings.active_profile == item.name);

        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::WithAlpha(Color::PrimaryContainer, 0.20f));
            ImGui::PushStyleColor(ImGuiCol_Border, Color::PrimaryContainer);
        } else {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
            ImGui::PushStyleColor(ImGuiCol_Border, Color::Border);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));

        char c_id[32];
        std::snprintf(c_id, sizeof(c_id), "ProfCard_%d", (int)p);
        ImGui::BeginChild(c_id, ImVec2(card_w, card_h), true, ImGuiWindowFlags_AlwaysUseWindowPadding | no_scroll);

        ImGui::TextColored(Color::TextPrimary, "%s", item.name.c_str());
        ImGui::TextColored(Color::TextSecondary, "%s", item.description.c_str());

        ImGui::Spacing();
        ImGui::TextColored(Color::IconGray, "Delay: %d ms | Clarity: %+.1f dB", item.lipsync_ms, item.center_clarity_db);

        ImGui::Spacing();
        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, Color::StatusOk);
            ImGui::Button("● Active Preset", ImVec2(card_w - 16.0f, 24.0f));
            ImGui::PopStyleColor();
        } else {
            if (ImGui::Button("Activate Profile", ImVec2(card_w - 16.0f, 24.0f))) {
                state.settings.active_profile = item.name;
                state.settings.lip_sync_delay_ms = item.lipsync_ms;
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);

        if (p < state.profiles.size() - 1) ImGui::SameLine();
    }
    Theme::EndCard();

    ImGui::Spacing();

    // 4. Application Auto-Switching Rules Table
    Theme::BeginCard("RulesTableCard");
    ImGui::TextColored(Color::PrimaryContainer, "%s", ICON_MD_SETTINGS);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "Application Foreground Auto-Switching Rules");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTable("RulesTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Application Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Process Executable", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Assigned Audio Profile", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Auto-Switch Status", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableHeadersRow();

        for (size_t r = 0; r < state.app_rules.size(); ++r) {
            auto& rule = state.app_rules[r];
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(Color::TextPrimary, "%s", rule.app_name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(Color::Tertiary, "%s", rule.process_name.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(Color::TextSecondary, "%s", rule.target_profile.c_str());

            ImGui::TableSetColumnIndex(3);
            char chk_id[32];
            std::snprintf(chk_id, sizeof(chk_id), "%s##rule_%d", rule.enabled ? "Active" : "Disabled", (int)r);
            ImGui::Checkbox(chk_id, &rule.enabled);
        }
        ImGui::EndTable();
    }
    Theme::EndCard();
}

} // namespace ht306::ui
