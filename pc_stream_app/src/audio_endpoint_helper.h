#pragma once

#include "app_state.h"

namespace ht306::audio {

class AudioEndpointHelper {
public:
    static void EnumerateDevices(AppState& state);
};

} // namespace ht306::audio
