#pragma once

#include "imgui.h"
#include <cstdint>

namespace ht306::ui {

namespace Color {
    // Exact Stitch Design Tokens
    inline constexpr ImVec4 Background             { 0.075f, 0.075f, 0.075f, 1.0f }; // #131313
    inline constexpr ImVec4 Surface                { 0.110f, 0.110f, 0.118f, 1.0f }; // #1C1C1E
    inline constexpr ImVec4 Surface2               { 0.149f, 0.149f, 0.157f, 1.0f }; // #262628
    inline constexpr ImVec4 SurfaceDim             { 0.075f, 0.075f, 0.075f, 1.0f }; // #131313
    inline constexpr ImVec4 SurfaceContainerLowest { 0.055f, 0.055f, 0.055f, 1.0f }; // #0E0E0E
    inline constexpr ImVec4 SurfaceContainerLow    { 0.110f, 0.106f, 0.106f, 1.0f }; // #1C1B1B
    inline constexpr ImVec4 SurfaceVariant         { 0.208f, 0.208f, 0.204f, 1.0f }; // #353534
    inline constexpr ImVec4 Border                 { 0.220f, 0.220f, 0.227f, 1.0f }; // #38383A

    // Text & Icons
    inline constexpr ImVec4 TextPrimary            { 1.000f, 1.000f, 1.000f, 1.0f }; // #FFFFFF
    inline constexpr ImVec4 TextSecondary          { 0.722f, 0.722f, 0.737f, 1.0f }; // #B8B8BC
    inline constexpr ImVec4 IconGray               { 0.420f, 0.420f, 0.420f, 1.0f }; // #6B6B6B

    // Status Colors
    inline constexpr ImVec4 StatusOk               { 0.184f, 0.749f, 0.443f, 1.0f }; // #2FBF71
    inline constexpr ImVec4 StatusWarn             { 0.961f, 0.651f, 0.137f, 1.0f }; // #F5A623
    inline constexpr ImVec4 StatusError            { 1.000f, 0.353f, 0.373f, 1.0f }; // #FF5A5F
    inline constexpr ImVec4 StatusInfo             { 0.357f, 0.616f, 1.000f, 1.0f }; // #5B9DFF
    inline constexpr ImVec4 Tertiary               { 0.341f, 0.875f, 0.553f, 1.0f }; // #57DF8D (Bright Mint)

    // LG Brand Colors
    inline constexpr ImVec4 PrimaryContainer       { 0.647f, 0.000f, 0.204f, 1.0f }; // #A50034
    inline constexpr ImVec4 LgRedHover             { 0.761f, 0.090f, 0.290f, 1.0f }; // #C2174A
    inline constexpr ImVec4 LgRedPressed           { 0.490f, 0.000f, 0.153f, 1.0f }; // #7D0027

    inline ImU32 ToU32(const ImVec4& col) {
        return ImGui::ColorConvertFloat4ToU32(col);
    }

    inline ImVec4 WithAlpha(const ImVec4& col, float alpha) {
        return ImVec4(col.x, col.y, col.z, alpha);
    }
} // namespace Color

struct Fonts {
    static inline ImFont* UiRegular   = nullptr;
    static inline ImFont* UiTitle     = nullptr;
    static inline ImFont* Mono        = nullptr;
    static inline ImFont* MonoLarge   = nullptr;
};

class Theme {
public:
    static void Apply();

    static void BeginCard(const char* id, const ImVec2& size = ImVec2(0, 0)) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface);
        ImGui::PushStyleColor(ImGuiCol_Border, Color::Border);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
        ImGui::BeginChild(id, size, ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_AlwaysUseWindowPadding);
    }

    static void EndCard() {
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

    static void StatTile(const char* label, const char* val1, const char* val2, const ImVec4& val1_col, float width);
};

} // namespace ht306::ui
