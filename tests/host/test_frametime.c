#include "test.h"
#include "debug/frametime.h"

static void test_stats(void) {
    frametime_reset();
    for (int i = 0; i < 99; i++) frametime_record(16667, 8000);
    frametime_record(50000, 30000);                       // one long frame
    FrameTimeStats s;
    frametime_get(&s, 16.67f);

    CHECK(s.count == 100);
    CHECK_NEAR(s.max_ms, 50.0, 1e-3);
    CHECK_NEAR(s.min_ms, 16.667, 1e-3);
    CHECK_NEAR(s.avg_ms, (99 * 16.667 + 50.0) / 100, 1e-3);
    // p99 of 100 frames = the 2nd slowest (index n/100 = 1 in descending order)
    CHECK_NEAR(s.p99_ms, 16.667, 1e-3);
    // 1 % low = 1000 / mean of the slowest ceil(100/100) = 1 frame
    CHECK_NEAR(s.low1_fps, 20.0, 1e-2);
    CHECK(s.over_budget == 1);                             // only the 30 ms CPU frame
    CHECK_NEAR(s.cpu_max_ms, 30.0, 1e-3);
    // Histogram: 1.5 ms buckets; 16.667 ms -> bucket 11, 50 ms -> overflow bucket
    CHECK(s.histogram[11] == 99);
    CHECK(s.histogram[FRAMETIME_BUCKETS - 1] == 1);
}

static void test_ring_wraps(void) {
    frametime_reset();
    for (int i = 0; i < FRAMETIME_WINDOW + 50; i++) frametime_record(10000 + i, 5000);
    FrameTimeStats s;
    frametime_get(&s, 16.67f);
    CHECK(s.count == FRAMETIME_WINDOW);
    // Oldest 50 frames were overwritten: the minimum is frame 50
    CHECK_NEAR(s.min_ms, 10.050, 1e-3);
}

void run_frametime_tests(void) {
    RUN_TEST(test_stats);
    RUN_TEST(test_ring_wraps);
}
