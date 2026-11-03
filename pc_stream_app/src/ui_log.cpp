#include "ui_log.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "imgui.h"
#include <cstdio>
#include <string>
#include <algorithm>

namespace ht306::ui {

void Log::Render(AppState& state) {
    // 1. Top Section: Header & Control Bar
    Theme::BeginCard("LogHeroCard");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::BeginChild("RevBadge", ImVec2(120.0f, 22.0f), false);
    ImGui::SetCursorPos(ImVec2(6.0f, 2.0f));
    ImGui::TextColored(Color::TextSecondary, "REV 1.3.0-RC4");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::TextColored(Color::StatusOk, "●");
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "SYSTEM DIAGNOSTICS & TELEMETRY LOG");

    ImGui::TextColored(Color::TextSecondary, "Real-time CDC-ACM control channel trace, WinUSB bulk packet inspector, link stability telemetry, and diagnostic incident reporter.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Copy / Export buttons
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    if (ImGui::Button("Copy Full Log", ImVec2(130.0f, 28.0f))) {
        std::string all_txt;
        for (const auto& l : state.logs) {
            all_txt += "[" + l.timestamp + "] [" + l.subsystem + "] " + l.message + "\n";
        }
        ImGui::SetClipboardText(all_txt.c_str());
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
    if (ImGui::Button("Export Diagnostic ZIP", ImVec2(170.0f, 28.0f))) {}
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    Theme::EndCard();

    ImGui::Spacing();

    // 2. 4 KPI Diagnostic Telemetry Cards
    float total_w = ImGui::GetContentRegionAvail().x;
    float card_w = (total_w - ImGui::GetStyle().ItemSpacing.x * 3) / 4.0f;

    Theme::StatTile("Transport Health", "0 loss / hr", "FIFO Depth: 1024 B | Jitter: <0.18ms", Color::StatusOk, card_w);
    ImGui::SameLine();
    Theme::StatTile("Lossless Packetizer", "58.4% Ratio", "RICE-FLAC4 Order-4 Delta", Color::TextPrimary, card_w);
    ImGui::SameLine();
    Theme::StatTile("PIO FIFO State", "1024 B Chunks", "SM0-2 MUX Direct Hardware Lock", Color::Tertiary, card_w);
    ImGui::SameLine();
    Theme::StatTile("USB Bandwidth", "716 KB/s", "Full-Speed Iso/Bulk Envelope", Color::StatusInfo, card_w);

    ImGui::Spacing();

    // 3. Quick Action / Filtering Toolbar
    Theme::BeginCard("LogToolbarCard");

    // Filter Level Chips (ALL, DEBUG, INFO, WARN, ERROR)
    const char* f_names[] = { "ALL", "DEBUG", "INFO", "WARN", "ERROR" };
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    for (int f = 0; f < 5; ++f) {
        bool is_sel = (state.log_filter_level == f);
        if (is_sel) ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
        else ImGui::PushStyleColor(ImGuiCol_Button, Color::Surface2);

        char f_id[32];
        std::snprintf(f_id, sizeof(f_id), "%s##flt_%d", f_names[f], f);
        if (ImGui::Button(f_id, ImVec2(70.0f, 26.0f))) {
            state.log_filter_level = f;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::PopStyleVar();

    // Search bar
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputTextWithHint("##LogSearch", "Filter by subsystem, opcode...", state.log_search, sizeof(state.log_search));

    ImGui::SameLine();
    if (ImGui::Button("Clear Buffer", ImVec2(90.0f, 26.0f))) {
        state.logs.clear();
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto-Scroll", &state.log_autoscroll);

    Theme::EndCard();

    ImGui::Spacing();

    // 4. Live Log Console Table
    Theme::BeginCard("LogConsoleCard", ImVec2(0, 340.0f));

    if (ImGui::BeginTable("LogTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("TIMESTAMP", ImGuiTableColumnFlags_WidthFixed, 105.0f);
        ImGui::TableSetupColumn("SUBSYSTEM", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("LEVEL", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("TRACE EVENT MESSAGE", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto& log : state.logs) {
            // Apply level filter
            if (state.log_filter_level == 1 && log.level != LogLevel::LOG_DEBUG) continue;
            if (state.log_filter_level == 2 && log.level != LogLevel::LOG_INFO) continue;
            if (state.log_filter_level == 3 && log.level != LogLevel::LOG_WARN) continue;
            if (state.log_filter_level == 4 && log.level != LogLevel::LOG_ERROR) continue;

            // Apply search filter
            if (state.log_search[0] != '\0') {
                std::string s_lower = state.log_search;
                std::string msg_lower = log.message;
                std::string sub_lower = log.subsystem;
                std::transform(s_lower.begin(), s_lower.end(), s_lower.begin(), ::tolower);
                std::transform(msg_lower.begin(), msg_lower.end(), msg_lower.begin(), ::tolower);
                std::transform(sub_lower.begin(), sub_lower.end(), sub_lower.begin(), ::tolower);
                if (msg_lower.find(s_lower) == std::string::npos && sub_lower.find(s_lower) == std::string::npos) {
                    continue;
                }
            }

            ImGui::TableNextRow();

            // Col 0: Timestamp
            ImGui::TableSetColumnIndex(0);
            if (Fonts::Mono) ImGui::PushFont(Fonts::Mono);
            ImGui::TextColored(Color::IconGray, "%s", log.timestamp.c_str());
            if (Fonts::Mono) ImGui::PopFont();

            // Col 1: Subsystem
            ImGui::TableSetColumnIndex(1);
            if (Fonts::Mono) ImGui::PushFont(Fonts::Mono);
            ImGui::TextColored(Color::Tertiary, "%s", log.subsystem.c_str());
            if (Fonts::Mono) ImGui::PopFont();

            // Col 2: Level
            ImGui::TableSetColumnIndex(2);
            switch (log.level) {
                case LogLevel::LOG_DEBUG: ImGui::TextColored(Color::StatusInfo, "DEBUG"); break;
                case LogLevel::LOG_INFO:  ImGui::TextColored(Color::StatusOk, "INFO"); break;
                case LogLevel::LOG_WARN:  ImGui::TextColored(Color::StatusWarn, "WARN"); break;
                case LogLevel::LOG_ERROR: ImGui::TextColored(Color::StatusError, "ERROR"); break;
            }

            // Col 3: Message
            ImGui::TableSetColumnIndex(3);
            if (Fonts::Mono) ImGui::PushFont(Fonts::Mono);
            ImGui::TextColored(Color::TextPrimary, "%s", log.message.c_str());
            if (Fonts::Mono) ImGui::PopFont();
        }

        if (state.log_autoscroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndTable();
    }

    Theme::EndCard();
}

} // namespace ht306::ui
