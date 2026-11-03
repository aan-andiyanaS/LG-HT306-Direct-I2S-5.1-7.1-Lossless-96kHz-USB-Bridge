#include "ui_theme.h"

namespace ht306::ui {

void Theme::Apply() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Rounding from Stitch
    style.WindowRounding    = 0.0f; // Window container seamless
    style.ChildRounding     = 10.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 8.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 6.0f;

    // Spacing & Padding
    style.WindowPadding     = ImVec2(0.0f, 0.0f);
    style.FramePadding      = ImVec2(10.0f, 6.0f);
    style.ItemSpacing       = ImVec2(10.0f, 10.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 6.0f);
    style.WindowBorderSize  = 0.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;

    // Color Palette
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]             = Color::Background;
    colors[ImGuiCol_ChildBg]              = Color::Surface;
    colors[ImGuiCol_PopupBg]              = Color::Surface2;
    colors[ImGuiCol_Border]               = Color::Border;
    colors[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    colors[ImGuiCol_Text]                 = Color::TextPrimary;
    colors[ImGuiCol_TextDisabled]         = Color::TextSecondary;

    colors[ImGuiCol_FrameBg]              = Color::Surface2;
    colors[ImGuiCol_FrameBgHovered]       = Color::SurfaceVariant;
    colors[ImGuiCol_FrameBgActive]        = Color::Border;

    colors[ImGuiCol_TitleBg]              = Color::SurfaceContainerLowest;
    colors[ImGuiCol_TitleBgActive]        = Color::SurfaceContainerLowest;
    colors[ImGuiCol_TitleBgCollapsed]     = Color::SurfaceContainerLowest;
    colors[ImGuiCol_MenuBarBg]            = Color::SurfaceContainerLowest;

    // Buttons: Primary LG Red
    colors[ImGuiCol_Button]               = Color::PrimaryContainer;
    colors[ImGuiCol_ButtonHovered]        = Color::LgRedHover;
    colors[ImGuiCol_ButtonActive]         = Color::LgRedPressed;

    // Header & Selectable
    colors[ImGuiCol_Header]               = Color::Surface2;
    colors[ImGuiCol_HeaderHovered]        = Color::SurfaceVariant;
    colors[ImGuiCol_HeaderActive]         = Color::PrimaryContainer;

    // Sliders & Tabs
    colors[ImGuiCol_SliderGrab]           = Color::Tertiary;
    colors[ImGuiCol_SliderGrabActive]     = Color::StatusOk;
    colors[ImGuiCol_CheckMark]            = Color::Tertiary;

    colors[ImGuiCol_Separator]            = Color::Border;
    colors[ImGuiCol_SeparatorHovered]     = Color::Border;
    colors[ImGuiCol_SeparatorActive]      = Color::PrimaryContainer;

    colors[ImGuiCol_ScrollbarBg]          = Color::Background;
    colors[ImGuiCol_ScrollbarGrab]        = Color::Surface2;
    colors[ImGuiCol_ScrollbarGrabHovered] = Color::SurfaceVariant;
    colors[ImGuiCol_ScrollbarGrabActive]  = Color::Border;
}

void Theme::StatTile(const char* label, const char* val1, const char* val2, const ImVec4& val1_col, float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleColor(ImGuiCol_Border, Color::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));

    ImGui::BeginChild(label, ImVec2(width, 68.0f), ImGuiChildFlags_Border, ImGuiWindowFlags_AlwaysUseWindowPadding);
    ImGui::TextColored(Color::TextSecondary, "%s", label);
    ImGui::TextColored(val1_col, "%s", val1);
    ImGui::SameLine();
    ImGui::TextColored(Color::IconGray, "%s", val2);
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

} // namespace ht306::ui
