#pragma once

// okn-input — a tiny, backend-agnostic action map. Bind gameplay ACTIONS (your own
// scoped enum, last enumerator `Count`) to one or two keys each, feed it raw key
// up/down events, and query held / just-pressed / just-released per action. KeyCode
// is a plain uint32_t, so this header never pulls in a windowing/input backend: pass
// your platform's keycodes straight through as integers, e.g.
// `m.bind(Action::Jump, (okn::input::KeyCode)SAPP_KEYCODE_SPACE)`. This replaces the
// per-game InputMap struct that was copy-pasted across the demos.
//
// The template is header-only; binding (de)serialization lives in action_map_io.cpp
// so okn-input is a real compiled static lib (and the I/O is testable on its own).

#include <cstdint>
#include <string>

namespace okn::input {

using KeyCode = std::uint32_t;
inline constexpr KeyCode kNoKey = 0;

namespace detail {
// Binary (de)serialize a flat keys table [n_actions][2]. Defined in action_map_io.cpp.
bool save_bindings(const std::string& path, const KeyCode* keys, int n_actions);
bool load_bindings(const std::string& path, KeyCode* keys, int n_actions);
}  // namespace detail

// `A` is a scoped enum whose last enumerator is `Count`. Each action has a primary
// and an alternate key slot (0 / 1); either firing makes the action active.
template <class A, int N = static_cast<int>(A::Count)>
class ActionMap {
public:
    static constexpr int kActions = N;
    static constexpr int kSlots = 2;

    void bind(A a, KeyCode primary, KeyCode alt = kNoKey) {
        keys_[idx(a)][0] = primary;
        keys_[idx(a)][1] = alt;
    }
    void rebind(A a, int slot, KeyCode key) {
        if (slot >= 0 && slot < kSlots) { keys_[idx(a)][slot] = key; }
    }
    [[nodiscard]] KeyCode key_of(A a, int slot = 0) const {
        return (slot >= 0 && slot < kSlots) ? keys_[idx(a)][slot] : kNoKey;
    }

    // Feed one raw key event. Updates held state plus this frame's rising/falling edge.
    void on_key(KeyCode k, bool is_down) {
        if (k == kNoKey) { return; }
        for (int a = 0; a < N; ++a) {
            if (keys_[a][0] == k || keys_[a][1] == k) {
                if (is_down && !down_[a]) { pressed_[a] = true; }
                if (!is_down && down_[a]) { released_[a] = true; }
                down_[a] = is_down;
            }
        }
    }
    // Clear this frame's edges. Call once per frame AFTER querying just()/just_released().
    void end_frame() {
        for (int a = 0; a < N; ++a) { pressed_[a] = false; released_[a] = false; }
    }
    // Drop all held + edge state (e.g. on focus loss or a scene change) without
    // disturbing the bindings.
    void clear_state() {
        for (int a = 0; a < N; ++a) { down_[a] = pressed_[a] = released_[a] = false; }
    }

    [[nodiscard]] bool held(A a) const { return down_[idx(a)]; }
    [[nodiscard]] bool just(A a) const { return pressed_[idx(a)]; }            // rising edge this frame
    [[nodiscard]] bool just_released(A a) const { return released_[idx(a)]; }  // falling edge this frame

    // Persist / restore the key bindings (not the transient held/edge state). load()
    // rejects a file whose action count differs, leaving the current bindings intact.
    bool save(const std::string& path) const { return detail::save_bindings(path, &keys_[0][0], N); }
    bool load(const std::string& path) { return detail::load_bindings(path, &keys_[0][0], N); }

private:
    static constexpr int idx(A a) { return static_cast<int>(a); }
    KeyCode keys_[N][2]{};
    bool down_[N]{};
    bool pressed_[N]{};
    bool released_[N]{};
};

}  // namespace okn::input
