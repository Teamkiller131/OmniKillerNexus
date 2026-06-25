#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <okn/input/action_map.hpp>

#include <cstdio>

using namespace okn::input;

namespace {
enum class Act { Left, Right, Jump, Count };
// Stand-in keycodes (any nonzero integers; real games pass platform keycodes).
constexpr KeyCode K_A = 1, K_LEFT = 2, K_D = 3, K_RIGHT = 4, K_SPACE = 5, K_W = 6, K_X = 99;
}  // namespace

TEST_CASE("okn-input: held tracks key down then up") {
    ActionMap<Act> m;
    m.bind(Act::Left, K_A, K_LEFT);
    CHECK_FALSE(m.held(Act::Left));
    m.on_key(K_A, true);
    CHECK(m.held(Act::Left));
    m.on_key(K_A, false);
    CHECK_FALSE(m.held(Act::Left));
}

TEST_CASE("okn-input: just() is a one-frame rising edge cleared by end_frame") {
    ActionMap<Act> m;
    m.bind(Act::Jump, K_SPACE);
    m.on_key(K_SPACE, true);
    CHECK(m.just(Act::Jump));
    CHECK(m.held(Act::Jump));
    m.on_key(K_SPACE, true);          // repeat-down while already held is NOT a new edge
    CHECK(m.just(Act::Jump));         // (still set from the genuine press this frame)
    m.end_frame();
    CHECK_FALSE(m.just(Act::Jump));   // edge cleared
    CHECK(m.held(Act::Jump));         // but still held
    m.on_key(K_SPACE, true);
    CHECK_FALSE(m.just(Act::Jump));   // no fresh edge from a repeat
}

TEST_CASE("okn-input: just_released is a one-frame falling edge") {
    ActionMap<Act> m;
    m.bind(Act::Jump, K_SPACE);
    m.on_key(K_SPACE, true);
    m.end_frame();
    CHECK_FALSE(m.just_released(Act::Jump));
    m.on_key(K_SPACE, false);
    CHECK(m.just_released(Act::Jump));
    CHECK_FALSE(m.held(Act::Jump));
    m.end_frame();
    CHECK_FALSE(m.just_released(Act::Jump));
}

TEST_CASE("okn-input: the alternate key fires the same action; an unbound key is inert") {
    ActionMap<Act> m;
    m.bind(Act::Right, K_D, K_RIGHT);
    m.on_key(K_RIGHT, true);          // alternate slot
    CHECK(m.held(Act::Right));
    m.on_key(K_X, true);              // unbound key
    CHECK_FALSE(m.just(Act::Left));
    CHECK_FALSE(m.held(Act::Left));
}

TEST_CASE("okn-input: actions are independent") {
    ActionMap<Act> m;
    m.bind(Act::Left, K_A);
    m.bind(Act::Right, K_D);
    m.on_key(K_A, true);
    CHECK(m.held(Act::Left));
    CHECK_FALSE(m.held(Act::Right));
}

TEST_CASE("okn-input: rebind redirects the action; key_of reflects it") {
    ActionMap<Act> m;
    m.bind(Act::Jump, K_SPACE);
    m.rebind(Act::Jump, 0, K_W);
    CHECK(m.key_of(Act::Jump, 0) == K_W);
    m.on_key(K_SPACE, true);
    CHECK_FALSE(m.held(Act::Jump));   // old key no longer bound
    m.on_key(K_W, true);
    CHECK(m.held(Act::Jump));
}

TEST_CASE("okn-input: clear_state drops held + edges but keeps the bindings") {
    ActionMap<Act> m;
    m.bind(Act::Jump, K_SPACE);
    m.on_key(K_SPACE, true);
    m.clear_state();
    CHECK_FALSE(m.held(Act::Jump));
    CHECK_FALSE(m.just(Act::Jump));
    CHECK(m.key_of(Act::Jump, 0) == K_SPACE);   // binding intact
}

TEST_CASE("okn-input: save/load round-trips bindings; a mismatched action count is rejected") {
    const char* path = "okn_input_test_bindings.bin";
    std::remove(path);

    ActionMap<Act> m;
    m.bind(Act::Left, K_A, K_LEFT);
    m.bind(Act::Right, K_D, K_RIGHT);
    m.bind(Act::Jump, K_SPACE, K_W);
    REQUIRE(m.save(path));

    ActionMap<Act> n;
    REQUIRE(n.load(path));
    CHECK(n.key_of(Act::Left, 0) == K_A);
    CHECK(n.key_of(Act::Left, 1) == K_LEFT);
    CHECK(n.key_of(Act::Jump, 1) == K_W);

    // A map with a different action count must reject the file (keeps its own bindings).
    enum class Other { A, B, Count };
    ActionMap<Other> o;
    CHECK_FALSE(o.load(path));

    std::remove(path);
}
