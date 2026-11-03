#include "ui_mixer.h"
#include "ui_theme.h"
#include "ui_icons.h"
#include "imgui.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace ht306::ui {

void Mixer::Render(AppState& state) {
    // 1. Top Section: Header & Mode Control
    Theme::BeginCard("MixerTopCard");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::PrimaryContainer);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::BeginChild("HwBadgePill", ImVec2(120.0f, 22.0f), false);
    ImGui::SetCursorPos(ImVec2(6.0f, 2.0f));
    ImGui::TextColored(Color::TextPrimary, "RP2350 PIO I2S");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "DSP Multi-Channel Mixer & Alignment");
    ImGui::TextColored(Color::TextSecondary, "Independent per-channel gain trimming, phase inversion, solo/mute matrix, and AV lip-sync delay compensation.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Operating mode toggle pills & Limiter
    ImGui::TextColored(Color::IconGray, "OPERATING MODE:");
    ImGui::SameLine();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    if (state.settings.desktop_os_linked) {
        ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
        ImGui::Button("Desktop (OS Linked)", ImVec2(160.0f, 26.0f));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, Color::Surface2);
        if (ImGui::Button("Bit-Perfect Pass-thru", ImVec2(170.0f, 26.0f))) {
            state.settings.desktop_os_linked = false;
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, Color::Surface2);
        if (ImGui::Button("Desktop (OS Linked)", ImVec2(160.0f, 26.0f))) {
            state.settings.desktop_os_linked = true;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
        ImGui::Button("Bit-Perfect Pass-thru", ImVec2(170.0f, 26.0f));
        ImGui::PopStyleColor();
    }

    // Right-aligned Limiter Switch & Badge
    float r_avail = ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(r_avail - 290.0f);
    ImGui::Checkbox("True-Peak Limiter", &state.settings.limiter_enabled);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::BeginChild("LimiterBadge", ImVec2(130.0f, 22.0f), false);
    ImGui::SetCursorPos(ImVec2(6.0f, 2.0f));
    ImGui::TextColored(Color::Tertiary, "-0.1 dBFS / 1.2ms");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    Theme::EndCard();

    ImGui::Spacing();

    // 2. Master Output & AV Lip-Sync Split Row
    float total_w = ImGui::GetContentRegionAvail().x;
    float left_w = total_w * 0.44f;
    float right_w = total_w - left_w - ImGui::GetStyle().ItemSpacing.x;

    // LEFT: Master Output Level
    Theme::BeginCard("MasterOutputCard", ImVec2(left_w, 230.0f));
    ImGui::TextColored(Color::PrimaryContainer, "%s", ICON_MD_TUNE);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "Master Output Level");

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 140.0f);
    if (ImGui::Button("0.0 dB", ImVec2(55.0f, 22.0f))) {
        state.settings.master_volume_db = 0.0f;
    }
    ImGui::SameLine();
    if (state.settings.mute_master) {
        ImGui::PushStyleColor(ImGuiCol_Button, Color::StatusError);
        if (ImGui::Button("Muted", ImVec2(65.0f, 22.0f))) state.settings.mute_master = false;
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button("Mute", ImVec2(65.0f, 22.0f))) state.settings.mute_master = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Big Numerical Readout + Slider
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::BeginChild("MasterBigBox", ImVec2(90.0f, 75.0f), false);
    ImGui::SetCursorPos(ImVec2(12.0f, 8.0f));
    ImGui::TextColored(Color::IconGray, "LEVEL");
    ImGui::SetCursorPos(ImVec2(10.0f, 28.0f));
    if (Fonts::MonoLarge) ImGui::PushFont(Fonts::MonoLarge);
    ImGui::TextColored(Color::TextPrimary, "%.1f", state.settings.master_volume_db);
    if (Fonts::MonoLarge) ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(12.0f, 52.0f));
    ImGui::TextColored(Color::Tertiary, state.settings.master_volume_db == 0.0f ? "UNITY" : "TRIM");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    ImGui::BeginGroup();
    // Scale labels
    ImGui::TextColored(Color::IconGray, "-60 dB     -24 dB     -12 dB      0 dB      +6 dB");
    ImGui::SetNextItemWidth(left_w - 130.0f);
    ImGui::SliderFloat("##MasterSlider", &state.settings.master_volume_db, -60.0f, +6.0f, "%.1f dB");

    // Integrated visual meter track
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 m_pos = ImGui::GetCursorScreenPos();
    float m_w = left_w - 130.0f;
    float m_h = 6.0f;
    dl->AddRectFilled(m_pos, ImVec2(m_pos.x + m_w, m_pos.y + m_h), Color::ToU32(Color::SurfaceDim), 3.0f);
    float m_norm = (std::clamp)((state.settings.master_volume_db + 60.0f) / 66.0f, 0.0f, 1.0f);
    dl->AddRectFilled(m_pos, ImVec2(m_pos.x + m_w * m_norm, m_pos.y + m_h), Color::ToU32(Color::StatusOk), 3.0f);
    ImGui::Dummy(ImVec2(m_w, m_h + 4.0f));
    ImGui::EndGroup();

    ImGui::Spacing();
    // Snapshots: -18, -12, -6, 0, +3 dB
    ImGui::TextColored(Color::TextSecondary, "Snapshots:");
    ImGui::SameLine();
    float snap_dbs[5] = { -18.0f, -12.0f, -6.0f, 0.0f, +3.0f };
    for (int s = 0; s < 5; ++s) {
        char s_lbl[16];
        std::snprintf(s_lbl, sizeof(s_lbl), "%+.0f dB##sn_%d", snap_dbs[s], s);
        bool is_cur = (std::abs(state.settings.master_volume_db - snap_dbs[s]) < 0.2f);
        if (is_cur) ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
        else ImGui::PushStyleColor(ImGuiCol_Button, Color::Surface2);
        if (ImGui::Button(s_lbl, ImVec2(55.0f, 22.0f))) {
            state.settings.master_volume_db = snap_dbs[s];
        }
        ImGui::PopStyleColor();
        if (s < 4) ImGui::SameLine();
    }
    Theme::EndCard();

    ImGui::SameLine();

    // RIGHT: AV Lip-Sync Delay Engine
    Theme::BeginCard("LipSyncCard", ImVec2(right_w, 230.0f));
    ImGui::TextColored(Color::PrimaryContainer, "%s", ICON_MD_SYNC);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "AV Lip-Sync Delay Engine");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 220.0f);
    ImGui::TextColored(Color::TextSecondary, "DMA Ring: 19200 Samples");
    ImGui::SameLine();
    ImGui::TextColored(Color::StatusOk, "●");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Value Display + Steppers
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::Surface2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::BeginChild("DelayBox", ImVec2(180.0f, 85.0f), false);
    ImGui::SetCursorPos(ImVec2(10.0f, 6.0f));
    ImGui::TextColored(Color::IconGray, "GLOBAL AUDIO LAG");
    ImGui::SetCursorPos(ImVec2(10.0f, 24.0f));
    if (Fonts::MonoLarge) ImGui::PushFont(Fonts::MonoLarge);
    ImGui::TextColored(Color::TextPrimary, "%d ms", state.settings.lip_sync_delay_ms);
    if (Fonts::MonoLarge) ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(10.0f, 54.0f));
    ImGui::TextColored(Color::Tertiary, "%d subframes @ 60Hz", (int)(state.settings.lip_sync_delay_ms / 3.0f));
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    ImGui::BeginGroup();
    // Stepper buttons (-10, -1, +1, +10)
    if (ImGui::Button("-10##d", ImVec2(40.0f, 24.0f))) state.settings.lip_sync_delay_ms = (std::max)(0, state.settings.lip_sync_delay_ms - 10);
    ImGui::SameLine();
    if (ImGui::Button("-1##d", ImVec2(35.0f, 24.0f))) state.settings.lip_sync_delay_ms = (std::max)(0, state.settings.lip_sync_delay_ms - 1);
    ImGui::SameLine();
    if (ImGui::Button("+1##d", ImVec2(35.0f, 24.0f))) state.settings.lip_sync_delay_ms = (std::min)(200, state.settings.lip_sync_delay_ms + 1);
    ImGui::SameLine();
    if (ImGui::Button("+10##d", ImVec2(40.0f, 24.0f))) state.settings.lip_sync_delay_ms = (std::min)(200, state.settings.lip_sync_delay_ms + 10);

    // Timeline bar
    ImVec2 t_pos = ImGui::GetCursorScreenPos();
    float t_w = right_w - 210.0f;
    float t_h = 24.0f;
    dl->AddRectFilled(t_pos, ImVec2(t_pos.x + t_w, t_pos.y + t_h), Color::ToU32(Color::Surface2), 4.0f);
    float d_norm = (std::clamp)(state.settings.lip_sync_delay_ms / 200.0f, 0.0f, 1.0f);
    dl->AddRectFilled(t_pos, ImVec2(t_pos.x + t_w * d_norm, t_pos.y + t_h), Color::ToU32(Color::PrimaryContainer), 4.0f);
    dl->AddText(ImVec2(t_pos.x + 8.0f, t_pos.y + 4.0f), Color::ToU32(Color::TextPrimary), "SYNCED I2S FIFO LEAD");
    ImGui::Dummy(ImVec2(t_w, t_h + 4.0f));

    // Slider
    ImGui::SetNextItemWidth(t_w);
    ImGui::SliderInt("##DelaySlider", &state.settings.lip_sync_delay_ms, 0, 200, "%d ms");
    ImGui::EndGroup();

    ImGui::Spacing();
    // Profiles: Bypass (0 ms), Gaming (12 ms), Movie (24 ms), Projector (65 ms)
    ImGui::TextColored(Color::TextSecondary, "Profiles:");
    ImGui::SameLine();
    struct DelayProfile { const char* name; int ms; };
    DelayProfile d_profs[4] = { { "Bypass (0 ms)", 0 }, { "Gaming (12 ms)", 12 }, { "Movie (24 ms)", 24 }, { "Projector (65 ms)", 65 } };
    for (int p = 0; p < 4; ++p) {
        bool is_cur = (state.settings.lip_sync_delay_ms == d_profs[p].ms);
        if (is_cur) ImGui::PushStyleColor(ImGuiCol_Button, Color::PrimaryContainer);
        else ImGui::PushStyleColor(ImGuiCol_Button, Color::Surface2);
        if (ImGui::Button(d_profs[p].name)) {
            state.settings.lip_sync_delay_ms = d_profs[p].ms;
        }
        ImGui::PopStyleColor();
        if (p < 3) ImGui::SameLine();
    }
    Theme::EndCard();

    ImGui::Spacing();

    // 3. 6-Channel Mixer Strip Console (TAS5508 Core)
    Theme::BeginCard("ConsoleCard");
    ImGui::TextColored(Color::PrimaryContainer, "%s", ICON_MD_EQUALIZER);
    ImGui::SameLine();
    ImGui::TextColored(Color::TextPrimary, "Channel Alignment Console (TAS5508 Core)");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 260.0f);
    ImGui::TextColored(Color::StatusOk, "● 6x Direct PCM Output Channels Active");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 6 Columns of Vertical Fader Cards
    struct ChInfo {
        const char* tag;
        const char* bus;
        const char* full_name;
    };
    ChInfo ch_infos[6] = {
        { "FL",  "AD0-L", "Front Left" },
        { "FR",  "AD0-R", "Front Right" },
        { "C",   "AD1-L", "Center Voice" },
        { "LFE", "AD1-R", "Subwoofer 120Hz" },
        { "SL",  "AD2-L", "Surround Left" },
        { "SR",  "AD2-R", "Surround Right" }
    };

    float strip_w = (ImGui::GetContentRegionAvail().x - (ImGui::GetStyle().ItemSpacing.x * 5)) / 6.0f;
    float strip_h = 330.0f;

    for (int c = 0; c < 6; ++c) {
        char card_id[32];
        std::snprintf(card_id, sizeof(card_id), "FaderStrip_%d", c);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Color::SurfaceContainerLow);
        ImGui::PushStyleColor(ImGuiCol_Border, Color::Border);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));

        ImGui::BeginChild(card_id, ImVec2(strip_w, strip_h), true, ImGuiWindowFlags_AlwaysUseWindowPadding);

        // Header: Tag + Bus
        ImGui::TextColored(Color::TextPrimary, "%s", ch_infos[c].tag);
        ImGui::SameLine(strip_w - 55.0f);
        ImGui::TextColored(Color::IconGray, "%s", ch_infos[c].bus);
        ImGui::TextColored(Color::TextSecondary, "%s", ch_infos[c].full_name);

        ImGui::Spacing();

        // Meter + Vertical Slider Area (Height ~170px)
        float fader_box_h = 170.0f;
        ImVec2 fader_area_p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(fader_area_p, ImVec2(fader_area_p.x + strip_w - 16.0f, fader_area_p.y + fader_box_h),
                          Color::ToU32(Color::Surface2), 6.0f);

        // Live Vertical VU Meter Bar (left of slider)
        float vu_x = fader_area_p.x + 10.0f;
        float vu_w = 8.0f;
        float vu_h = fader_box_h - 16.0f;
        float vu_y = fader_area_p.y + 8.0f;
        dl->AddRectFilled(ImVec2(vu_x, vu_y), ImVec2(vu_x + vu_w, vu_y + vu_h), Color::ToU32(Color::SurfaceDim), 2.0f);
        
        float db = state.meters[c].peak_db;
        float vu_norm = (std::clamp)((db + 60.0f) / 60.0f, 0.0f, 1.0f);
        float fill_h = vu_h * vu_norm;
        if (fill_h > 1.0f) {
            dl->AddRectFilled(ImVec2(vu_x, vu_y + vu_h - fill_h), ImVec2(vu_x + vu_w, vu_y + vu_h), Color::ToU32(Color::StatusOk), 2.0f);
        }

        // Vertical Slider for Trims (-12 to +12 dB)
        ImGui::SetCursorPos(ImVec2(28.0f, 54.0f));
        char vslider_id[32];
        std::snprintf(vslider_id, sizeof(vslider_id), "##vsl_%d", c);
        ImGui::VSliderFloat(vslider_id, ImVec2(24.0f, fader_box_h - 16.0f), &state.settings.trims[c], -12.0f, +12.0f, "");

        // Scale Hash marks (+12, +6, 0, -6, -12) on right
        float scale_x = fader_area_p.x + strip_w - 42.0f;
        dl->AddText(ImVec2(scale_x, vu_y), Color::ToU32(Color::StatusWarn), "+12");
        dl->AddText(ImVec2(scale_x, vu_y + vu_h * 0.25f), Color::ToU32(Color::TextSecondary), "+6");
        dl->AddText(ImVec2(scale_x, vu_y + vu_h * 0.50f), Color::ToU32(Color::TextPrimary), "0");
        dl->AddText(ImVec2(scale_x, vu_y + vu_h * 0.75f), Color::ToU32(Color::TextSecondary), "-6");
        dl->AddText(ImVec2(scale_x, vu_y + vu_h - 12.0f), Color::ToU32(Color::TextSecondary), "-12");

        ImGui::SetCursorPosY(54.0f + fader_box_h + 4.0f);

        // Precision Value Stepper (▼, 0.0 dB, ▲)
        char dec_id[32], inc_id[32];
        std::snprintf(dec_id, sizeof(dec_id), "▼##dec_%d", c);
        std::snprintf(inc_id, sizeof(inc_id), "▲##inc_%d", c);

        float btn_sz = 20.0f;
        if (ImGui::Button(dec_id, ImVec2(btn_sz, btn_sz))) state.settings.trims[c] = (std::max)(-12.0f, state.settings.trims[c] - 0.5f);
        ImGui::SameLine();
        ImGui::TextColored(Color::TextPrimary, "%+.1f dB", state.settings.trims[c]);
        ImGui::SameLine(strip_w - 30.0f);
        if (ImGui::Button(inc_id, ImVec2(btn_sz, btn_sz))) state.settings.trims[c] = (std::min)(+12.0f, state.settings.trims[c] + 0.5f);

        ImGui::Spacing();

        // Buttons: S (Solo), M (Mute), Ø (Phase Invert)
        float action_btn_w = (strip_w - 24.0f) / 3.0f;
        char solo_id[32], mute_id[32], phase_id[32];
        std::snprintf(solo_id, sizeof(solo_id), "S##s_%d", c);
        std::snprintf(mute_id, sizeof(mute_id), "M##m_%d", c);
        std::snprintf(phase_id, sizeof(phase_id), "Ø##p_%d", c);

        if (state.settings.solos[c]) ImGui::PushStyleColor(ImGuiCol_Button, Color::StatusWarn);
        else ImGui::PushStyleColor(ImGuiCol_Button, Color::Surface2);
        if (ImGui::Button(solo_id, ImVec2(action_btn_w, 24.0f))) state.settings.solos[c] = !state.settings.solos[c];
        ImGui::PopStyleColor();

        ImGui::SameLine();

        if (state.settings.mutes[c]) ImGui::PushStyleColor(ImGuiCol_Button, Color::StatusError);
        else ImGui::PushStyleColor(ImGuiCol_Button, Color::Surface2);
        if (ImGui::Button(mute_id, ImVec2(action_btn_w, 24.0f))) state.settings.mutes[c] = !state.settings.mutes[c];
        ImGui::PopStyleColor();

        ImGui::SameLine();

        if (state.settings.phase_inverts[c]) ImGui::PushStyleColor(ImGuiCol_Button, Color::StatusInfo);
        else ImGui::PushStyleColor(ImGuiCol_Button, Color::Surface2);
        if (ImGui::Button(phase_id, ImVec2(action_btn_w, 24.0f))) state.settings.phase_inverts[c] = !state.settings.phase_inverts[c];
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Reset to 0 dB
        char rst_id[32];
        std::snprintf(rst_id, sizeof(rst_id), "Reset 0 dB##rst_%d", c);
        if (ImGui::Button(rst_id, ImVec2(strip_w - 16.0f, 20.0f))) {
            state.settings.trims[c] = 0.0f;
            state.settings.mutes[c] = false;
            state.settings.solos[c] = false;
            state.settings.phase_inverts[c] = false;
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);

        if (c < 5) ImGui::SameLine();
    }

    Theme::EndCard();
}

} // namespace ht306::ui
