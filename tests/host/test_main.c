#include "test.h"

int g_checks = 0, g_failures = 0;
const char *g_current_test = "";

int main(void) {
    run_vec3_tests();
    run_collision_tests();
    run_physics_tests();
    run_action_tests();
    run_camera_tests();
    run_frametime_tests();
    run_mesh_tests();
    run_particle_tests();
    run_audio_tests();
    run_menu_tests();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
