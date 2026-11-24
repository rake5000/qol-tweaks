#include <stdint.h>
#include <stdlib.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
extern float test_speed_step(void *, float);
extern void test_speed_return(void);
uintptr_t speed_replay_slot, speed_resume;
float speed_selected_factor = 0.7f;
const float speed_slow_factor = 0.3f;
#ifdef R5K_QOL_NATIVE
int speed_feature_enabled=1;
volatile long speed_update_armed;
static int rearm=1;
#endif
static void expect(void *server, float expected) {
#ifdef R5K_QOL_NATIVE
    if(rearm)speed_update_armed=1;
#endif
    float *native = (float *)((char *)server + 0x7e210);
    float before = *native;
    float result = test_speed_step(server, 100.0f);
    assert(fabsf(result - expected) < 0.0001f);
    assert(*native == before);
}
int main(void) {
    void *server = calloc(1, 0x7e220);
    assert(server);
    uint32_t *phase = (uint32_t *)((char *)server + 0x290);
    uint32_t *flags = (uint32_t *)((char *)server + 0x294);
    float *native = (float *)((char *)server + 0x7e210);
    uintptr_t replay_stream = 0;
    speed_replay_slot = (uintptr_t)&replay_stream;
    speed_resume = (uintptr_t)&test_speed_return;
    *phase = 1;
    // Exercise the actual production assembly for repeated 70->30->70
    // toggles, including several updates within each frame. Never write
    // back into the game's native speed storage.
    for (int i = 0; i < 10000; ++i) {
        *flags = 0; *native = 1.0f;
        speed_selected_factor = 0.7f;
        expect(server, 70.0f);
        *flags = 0x20000; *native = 0.3f;
        expect(server, 30.0f);
        expect(server, 30.0f);
        speed_selected_factor = 2.0f;
        expect(server, 30.0f);
        *flags = 0; *native = 1.0f;
        expect(server, 200.0f);
        speed_selected_factor = 0.7f;
        expect(server, 70.0f);
        speed_selected_factor = 1.0f;
        expect(server, 100.0f);
    }
    // Ending/loading phases and replay playback retain native timing.
    speed_selected_factor = 2.0f;
    *phase = 2; *native = 0.12f;
    expect(server, 12.0f);
    *phase = 0; expect(server, 12.0f);
    *phase = 1; replay_stream = 1;
    expect(server, 12.0f);
    // Both ends of the selectable range, including a simultaneous slowmo flag.
    replay_stream = 0; *native = 1.0f;
    speed_selected_factor = 0.1f; expect(server, 10.0f);
    speed_selected_factor = 3.0f; expect(server, 300.0f);
    *flags = 0x20000; expect(server, 30.0f);
#ifdef R5K_QOL_NATIVE
    // Disabled native feature must execute the game's multiplier verbatim,
    // including custom native values and a simultaneous slow-motion flag.
    speed_feature_enabled=0;*native=0.42f;expect(server,42.0f);
    *flags=0;expect(server,42.0f);
    speed_feature_enabled=1;expect(server,300.0f);
    // Simulate loader fault: no post callback, no future pre callbacks.
    // A previously armed step may finish once; all subsequent steps are native.
    rearm=0;speed_update_armed=1;expect(server,300.0f);
    assert(speed_update_armed==0);
    for(int i=0;i<10000;i++)expect(server,42.0f);
    // Replay consumes its permission too; it cannot leak into a later step.
    speed_update_armed=1;replay_stream=1;expect(server,42.0f);
    replay_stream=0;expect(server,42.0f);
#endif
    free(server);
    puts("Actual timestep assembly passed 10,000 toggle cycles, stored-speed changes, native-value preservation, ending/replay guards and range checks.");
    return 0;
}
