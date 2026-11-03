#include "ui_sidebar.h"
#include "ui_theme.h"
#include "imgui.h"

namespace ht306::ui {

struct NavItemDef {
    NavTab tab;
    const char* label;
    const char* icon;
};

static const NavItemDef kNavItems[] = {
    { NavTab::DASHBOARD, "Dashboard", "[#]" },
    { NavTab::MIXER,     "Mixer",     "[=]" },
    { NavTab::ROUTING,   "Routing",   "[X]" },
    { NavTab::PROFILES,  "Profiles",  "[P]" },
    { NavTab::TEST,      "Test 5.1",  "[~]" },
    { NavTab::LOG,       "Log",       "[>]" },
    { NavTab::SETTINGS,  "Settings",  "[*]" }
};

void Sidebar::Render(AppState& state, float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::SurfaceContainerLow);
    ImGui::PushStyleColor(ImGuiCol_Border, Color::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 16.0f));

    ImGui::BeginChild("SidebarNav", ImVec2(width, 0), true, ImGuiWindowFlags_AlwaysUseWindowPadding);

    // Section Title
    ImGui::TextColored(Color::IconGray, "AUDIO ENGINE");
    ImGui::SameLine(width - 96.0f);
    ImGui::TextColored(Color::Tertiary, "96k / 32b");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    NavTab active_tab = state.current_tab.load();

    for (const auto& item : kNavItems) {
        bool is_active = (active_tab == item.tab);

        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color::LgRedHover);
            ImGui::PushStyleColor(ImGuiCol_Text, Color::TextPrimary);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color::Surface2);
            ImGui::PushStyleColor(ImGuiCol_Text, Color::TextSecondary);
        }

        char btn_label[64];
        std::snprintf(btn_label, sizeof(btn_label), "%s  %s", item.icon, item.label);

        if (ImGui::Button(btn_label, ImVec2(width - 24.0f, 36.0f))) {
            state.current_tab.store(item.tab);
        }

        ImGui::PopStyleColor(3);
        ImGui::Spacing();
    }

    // Push to bottom
    float avail_y = ImGui::GetContentRegionAvail().y;
    if (avail_y > 70.0f) {
        ImGui::Dummy(ImVec2(0, avail_y - 70.0f));
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Footer
    ImGui::TextColored(Color::TextSecondary, "%s", state.hw_info.port_or_path.c_str());
    ImGui::TextColored(Color::IconGray, "FIRMWARE %s", state.hw_info.firmware_version.c_str());

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

} // namespace ht306::ui
