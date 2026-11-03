#include "ui_sidebar.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "imgui.h"
#include <cmath>
#include <cstdio>

namespace ht306::ui {

struct NavItemDef {
    NavTab tab;
    const char* label;
    const char* icon;
};

static const NavItemDef kNavItems[] = {
    { NavTab::DASHBOARD, "Dashboard", ICON_MD_DASHBOARD },
    { NavTab::MIXER,     "Mixer",     ICON_MD_TUNE },
    { NavTab::ROUTING,   "Routing",   ICON_MD_ALT_ROUTE },
    { NavTab::PROFILES,  "Profiles",  ICON_MD_LAYERS },
    { NavTab::TEST,      "Test",      ICON_MD_VOLUME_UP },
    { NavTab::LOG,       "Log",       ICON_MD_TERMINAL },
    { NavTab::SETTINGS,  "Settings",  ICON_MD_SETTINGS }
};

void Sidebar::Render(AppState& state, float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::SurfaceContainerLow);
    ImGui::PushStyleColor(ImGuiCol_Border, Color::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 14.0f));

    ImGui::BeginChild("SidebarNav", ImVec2(width, 0), true, ImGuiWindowFlags_AlwaysUseWindowPadding);

    // 1. Top Section Badge (Matches HTML: AUDIO ENGINE / 96kHz / 24b)
    ImGui::TextColored(Color::IconGray, "AUDIO ENGINE");
    ImGui::SameLine(width - 92.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::BeginChild("HeaderRatePill", ImVec2(76.0f, 20.0f), false);
    ImGui::SetCursorPos(ImVec2(6.0f, 2.0f));
    ImGui::TextColored(Color::Tertiary, "96k / 32b");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 2. Navigation Pills List
    NavTab active_tab = state.current_tab.load();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float btn_w = width - 20.0f;
    float btn_h = 36.0f;

    for (const auto& item : kNavItems) {
        bool is_active = (active_tab == item.tab);

        ImVec2 p_min = ImGui::GetCursorScreenPos();
        ImVec2 p_max = ImVec2(p_min.x + btn_w, p_min.y + btn_h);

        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color::LgRedHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color::LgRedPressed);
            ImGui::PushStyleColor(ImGuiCol_Text, Color::TextPrimary);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color::Surface2);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color::SurfaceVariant);
            ImGui::PushStyleColor(ImGuiCol_Text, Color::TextSecondary);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        char btn_id[64];
        std::snprintf(btn_id, sizeof(btn_id), "##nav_%s", item.label);

        if (ImGui::Button(btn_id, ImVec2(btn_w, btn_h))) {
            state.current_tab.store(item.tab);
        }

        // Draw Icon and Text manually to ensure exact padding and crisp rendering
        ImU32 icon_col = is_active ? Color::ToU32(Color::TextPrimary) : Color::ToU32(Color::TextSecondary);
        dl->AddText(ImVec2(p_min.x + 12.0f, p_min.y + 9.0f), icon_col, item.icon);

        ImU32 text_col = is_active ? Color::ToU32(Color::TextPrimary) : Color::ToU32(Color::TextSecondary);
        dl->AddText(ImVec2(p_min.x + 36.0f, p_min.y + 9.0f), text_col, item.label);

        // Active Pulse Dot on right side (Stitch spec)
        if (is_active) {
            float t = (float)ImGui::GetTime();
            float alpha = 0.5f + 0.5f * std::sin(t * 4.0f);
            ImU32 dot_col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, alpha));
            dl->AddCircleFilled(ImVec2(p_max.x - 14.0f, p_min.y + btn_h * 0.5f), 3.0f, dot_col);
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        ImGui::Spacing();
    }

    // 3. Push to bottom footer
    float avail_y = ImGui::GetContentRegionAvail().y;
    if (avail_y > 80.0f) {
        ImGui::Dummy(ImVec2(0, avail_y - 80.0f));
    }

    ImGui::Separator();
    ImGui::Spacing();

    // 4. Footer: WinUSB Bulk | COM4 chip
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::BeginChild("FooterPill", ImVec2(btn_w, 24.0f), false);
    ImGui::SetCursorPos(ImVec2(6.0f, 4.0f));
    ImGui::TextColored(Color::TextSecondary, "WinUSB Bulk");
    ImGui::SameLine();
    ImGui::TextColored(Color::Border, "|");
    ImGui::SameLine();
    ImGui::TextColored(Color::Tertiary, "%s", state.hw_info.com_port.c_str());
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::TextColored(Color::IconGray, "FIRMWARE BRIDGE");
    ImGui::SameLine(width - 56.0f);
    ImGui::TextColored(Color::TextSecondary, "%s", state.hw_info.firmware_version.c_str());

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

} // namespace ht306::ui
