#pragma once

#include "app_state.h"

namespace ht306::ui {

class Dashboard {
public:
    static void Render(AppState& state);

private:
    static void RenderHeader(AppState& state);
    static void RenderHardwareCard(AppState& state);
    static void RenderTelemetryCard(AppState& state);
    static void RenderVuMetersCard(AppState& state);
    static void RenderControlsCard(AppState& state);
};

} // namespace ht306::ui
