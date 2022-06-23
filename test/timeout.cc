#include <chrono>
#include <ctime>
#include <thread>

#include <gtest/gtest.h>

#include <a3/util.h>

#include <sc/timeout.h>

using namespace testing;
using namespace std::chrono_literals;

struct TimeoutObject {
    ScTimeout timeout;
    bool      fired;
};

class TimeoutTest : public Test {
protected:
    ScTimer*      timer;     // NOLINT(misc-non-private-member-variables-in-classes)
    TimeoutObject victim {}; // NOLINT(misc-non-private-member-variables-in-classes)
    std::timespec base {};   // NOLINT(misc-non-private-member-variables-in-classes)

    TimeoutTest() : timer { sc_timer_new() } { std::timespec_get(&base, TIME_UTC); }
    ~TimeoutTest() { sc_timer_free(timer); }

    void timeout_add(time_t delay = 1) {
        sc_timeout_init(
            &victim.timeout,
            [](ScTimeout* timeout) {
                A3_CONTAINER_OF(timeout, TimeoutObject, timeout)->fired = true;
            },
            delay);
        std::timespec_get(&base, TIME_UTC);
        sc_timeout_add(timer, &victim.timeout);
    }

    void tick(time_t seconds = 1) {
        std::timespec later { base };
        later.tv_sec += seconds;
        sc_timer_tick_manual(timer, later);
    }
};

TEST_F(TimeoutTest, trivial) { EXPECT_TRUE(timer) << "Timer was not initialized."; }

TEST_F(TimeoutTest, add) {
    timeout_add(1);
    EXPECT_TRUE(sc_timer_next(timer));
}

TEST_F(TimeoutTest, fire) {
    timeout_add(1);
    tick(5);
    EXPECT_TRUE(victim.fired) << "Timeout did not fire.";
    EXPECT_FALSE(sc_timer_next(timer));
}

TEST_F(TimeoutTest, many) {
    for (time_t i = 1; i <= 10; i++)
        timeout_add(i);

    EXPECT_TRUE(sc_timer_next(timer));

    for (time_t i = 1; i <= 10; i++) {
        EXPECT_FALSE(victim.fired);
        tick(i - 1);
        EXPECT_FALSE(victim.fired);
        tick(1);
        EXPECT_TRUE(victim.fired);
        victim.fired = false;
    }

    EXPECT_FALSE(sc_timer_next(timer));
}
