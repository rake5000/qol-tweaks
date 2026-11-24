#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../replay_normal_speed/normalizer.h"

__declspec(dllexport) const char *DESCRIPTION = "Replay-only normalization of recorded native slow motion and custom simulation speeds.";

static uintptr_t image;
#include "port_common.h"
DK2ML_PLUGIN_MANIFEST(3, "Normal Speed Replays", "1.0.1", "Rake5000", "", 112);
static unsigned normalized;
static bool previously_replaying;
static const uint32_t mission_ending_state = 2;


static void log_text(int level, const char *message) {
    (void)level; api->Log("%s", message);
}
static bool read_at(uintptr_t address, void *out, SIZE_T count) {
    SIZE_T actual = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), (void *)address,
        out, count, &actual) && actual == count;
}
static bool is_replay(void) {
    uintptr_t stream = 0;
    return read_at(image + 0x137cdb8, &stream, sizeof(stream)) && stream;
}

static void advance_post(DK2ML_Regs *regs, void *user) {
    (void)user;
    if (!enabled || !regs->scratch[0]) return;
    void *client = (void *)(uintptr_t)regs->scratch[1];
    uintptr_t p = (uintptr_t)client;
    uint32_t previous_game_source = (uint32_t)regs->scratch[2];
    bool have_previous = regs->scratch[3] != 0;
    if (!is_replay()) return;
    uint32_t phase, mission_state, t[4];
    if (!read_at(p + 0x7e208, &phase, 4) || phase != 8 ||
        !read_at(p + 0x290, &mission_state, 4) || mission_state == mission_ending_state ||
        !read_at(p + 0x107260, t, sizeof(t))) return;
    // t = interpolation real-source, real-target, game-source, game-target.
    uint32_t game_source = t[2], game_target = t[3];
    // Replay packets are often consumed immediately, without lookahead. In
    // that case the engine sets both game endpoints to the new snapshot and
    // supplies a default real interval. Use the last observed game delta.
    if (game_target == game_source && have_previous) {
        game_source = previous_game_source;
        game_target = t[2];
    }
    uint32_t adjusted = replay_interval(t[0], t[1], game_source, game_target);
    if (adjusted == t[1]) return;
    SIZE_T actual = 0;
    if (!WriteProcessMemory(GetCurrentProcess(), (void *)(p + 0x107264),
        &adjusted, sizeof(adjusted), &actual) || actual != sizeof(adjusted)) return;
    ++normalized;
    if (normalized <= 3) {
        char message[180];
        snprintf(message, sizeof(message), "DK2 Replay POC: interval %lu -> %lu ms (game delta %lu); snapshot state retained.",
            (unsigned long)(t[1] - t[0]), (unsigned long)(adjusted - t[0]),
            (unsigned long)(game_target - game_source));
        log_text(3, message);
    }
}


static int advance_pre(DK2ML_Regs *regs, void *user) {
    (void)user;
    uint32_t previous = 0;
    regs->scratch[0] = enabled && is_replay();
    regs->scratch[1] = DK2ML_Arg(regs, 0);
    regs->scratch[3] = regs->scratch[0] &&
        read_at((uintptr_t)regs->scratch[1] + 0x107268, &previous, sizeof(previous));
    regs->scratch[2] = previous;
    return DK2ML_CALL_ORIGINAL;
}
static void frame(const DK2ML_Event *event, void *user) {
    (void)event; (void)user;
    bool replaying = is_replay();
    if (replaying && !previously_replaying) normalized = 0;
    previously_replaying = replaying;
}
DK2ML_EXPORT int DK2ML_PluginInit(const DK2ML_API *loader, const DK2ML_PluginInfo *info) {
    (void)info; image = (uintptr_t)loader->ResolveSymbol("GameClient::UpdateCamera") - 0x187f70;
    if (configure(loader, "Play replays at normal speed", "Normal-speed replay action with the native mission-ending slowdown.") ||
        !hook_named("GameClient::AdvanceSnapshot", 0x183990, advance_pre, advance_post) ||
        api->Subscribe(DK2ML_EVENT_FRAME, frame, NULL) != DK2ML_OK) return 1;
    api->Log("Ready: normal-speed replay action and original mission-ending slowdown.");
    return 0;
}
