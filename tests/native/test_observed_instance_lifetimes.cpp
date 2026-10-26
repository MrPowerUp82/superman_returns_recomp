// test_observed_instance_lifetimes.cpp
#include "test_main.h"
#include "../../port/src/graphics/observed_instance_lifetimes.h"
#include <limits>

using namespace superman_returns::graphics;

SR_TEST(unknown_not_alive) {
    ObservedInstanceLifetimes lif;
    SR_CHECK(!lif.Find(0x10000).has_value());
    SR_CHECK(!lif.IsAlive(ObservedInstanceToken{0x10000, 1}));
}

SR_TEST(create_find_gen_positive) {
    ObservedInstanceLifetimes lif;
    auto t = lif.Create(0x10000);
    SR_CHECK(t.has_value());
    if (!t) return;
    SR_CHECK(t->generation > 0);
    SR_CHECK_EQ(t->address, 0x10000u);
    auto found = lif.Find(0x10000);
    SR_CHECK(found.has_value());
    if (!found) return;
    SR_CHECK_EQ(found->generation, t->generation);
    SR_CHECK(lif.IsAlive(*t));
}

SR_TEST(destroy_double) {
    ObservedInstanceLifetimes lif;
    auto t = lif.Create(0x10000);
    SR_CHECK(t.has_value());
    if (!t) return;
    lif.Destroy(0x10000);
    lif.Destroy(0x10000);
    SR_CHECK(!lif.Find(0x10000).has_value());
    SR_CHECK(!lif.IsAlive(*t));
}

SR_TEST(reuse_reconstruct_invalidates_token) {
    ObservedInstanceLifetimes lif;
    auto t1 = lif.Create(0x20000);
    SR_CHECK(t1.has_value());
    if (!t1) return;
    auto t2 = lif.Create(0x20000);
    SR_CHECK(t2.has_value());
    if (!t2) return;
    SR_CHECK(!(*t1 == *t2));
    SR_CHECK(!lif.IsAlive(*t1));
    SR_CHECK(lif.IsAlive(*t2));
    auto found = lif.Find(0x20000);
    SR_CHECK(found.has_value());
    if (!found) return;
    SR_CHECK_EQ(found->generation, t2->generation);
}

SR_TEST(capacity_1_preserves_and_accepts_reconstruct) {
    ObservedInstanceLifetimes lif(1);
    auto t1 = lif.Create(0x10000);
    SR_CHECK(t1.has_value());
    if (!t1) return;
    auto t_rej = lif.Create(0x20000);
    SR_CHECK(!t_rej.has_value());
    SR_CHECK(lif.IsAlive(*t1));
    auto t1_new = lif.Create(0x10000);
    SR_CHECK(t1_new.has_value());
    if (!t1_new) return;
    SR_CHECK(!lif.IsAlive(*t1));
    SR_CHECK(lif.IsAlive(*t1_new));
}

SR_TEST(invalid_address_atomic) {
    ObservedInstanceLifetimes lif;
    SR_CHECK(!lif.Create(0x0).has_value());
    SR_CHECK(!lif.Create(0xFFFC).has_value());
    SR_CHECK(!lif.Create(0x10001).has_value());
    SR_CHECK(!lif.Create(0x10002).has_value());
    auto t = lif.Create(0x10000);
    SR_CHECK(t.has_value());
    if (!t) return;
    SR_CHECK_EQ(t->generation, 1ULL);
}

SR_TEST(gen_zero_and_fake_wrong_address_not_alive) {
    ObservedInstanceLifetimes lif;
    auto t = lif.Create(0x10000);
    SR_CHECK(t.has_value());
    if (!t) return;
    SR_CHECK(!lif.IsAlive(ObservedInstanceToken{0x10000, 0}));
    SR_CHECK(!lif.IsAlive(ObservedInstanceToken{0x20000, t->generation}));
}

SR_TEST(seed_overflow_fail_closed) {
    ObservedInstanceLifetimes lif(65536, std::numeric_limits<uint64_t>::max() - 1);
    auto t_max = lif.Create(0x10000);
    SR_CHECK(t_max.has_value());
    if (!t_max) return;
    SR_CHECK_EQ(t_max->generation, std::numeric_limits<uint64_t>::max());
    SR_CHECK(lif.IsAlive(*t_max));
    SR_CHECK(!lif.exhausted());
    auto t_overflow = lif.Create(0x20000);
    SR_CHECK(!t_overflow.has_value());
    SR_CHECK(lif.exhausted());
    SR_CHECK(!lif.IsAlive(*t_max));
    SR_CHECK(!lif.Find(0x10000).has_value());
    SR_CHECK(!lif.Create(0x30000).has_value());
}
