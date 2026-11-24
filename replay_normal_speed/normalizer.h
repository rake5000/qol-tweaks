#ifndef DK2_REPLAY_NORMALIZER_H
#define DK2_REPLAY_NORMALIZER_H
#include <stdint.h>

// Pace replay snapshots by elapsed simulation time, regardless of the vanilla
// slow-motion flag. Equal game timestamps represent a pause. Leave reversed,
// discontinuous and overflowing intervals to the engine.
static uint32_t replay_interval(uint32_t real_source, uint32_t real_target,
    uint32_t game_source, uint32_t game_target) {
    if (real_target <= real_source || game_target <= game_source)
        return real_target;
    uint32_t real_dt = real_target - real_source;
    uint32_t game_dt = game_target - game_source;
    if (real_dt > 1000 || game_dt > 1000 || game_dt > UINT32_MAX - real_source)
        return real_target;
    return real_source + game_dt;
}
#endif
