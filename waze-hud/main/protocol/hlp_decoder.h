#pragma once

#include "cJSON.h"
#include "state/hud_state.h"

namespace waze_hud {

class HlpDecoder {
public:
    bool handleHi(const cJSON *root, HudState &state);
    bool decodeState(const cJSON *root, HudState &state);
    void resetSession();

private:
    uint32_t session_{0};
    uint32_t lastTimestamp_{0};
    bool haveTimestamp_{false};
};

}  // namespace waze_hud
