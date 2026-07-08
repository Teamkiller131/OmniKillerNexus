// Swarm — the ECS scale/perf gate (ROADMAP v5 §12D / §13 step 4).
//
// A headless console sim that runs 20k entities through the parallel scheduler:
// a 3-wide conflict-free level (Move/Spin/Pulse) then a dependent level (Bounce,
// which reads Position and writes Velocity). It runs the SAME sim twice — once
// sequential, once on okn-platform's work-stealing pool — and asserts:
//   1. state_hash(sequential) == state_hash(parallel)  — scheduling-independence,
//      the determinism oracle's second consumer (after the save/load gate test);
//   2. the sim actually ran (final hash != initial hash);
//   3. a LOOSE per-frame budget (the scale tripwire — generous on purpose so shared
//      runners don't flake; the measured ratio is REPORTED, not asserted).
// Writes "SWARM OK n=... seqMs=... parMs=... speedup=..." to swarm_result.txt and
// exits nonzero on any failure, voidborne-marker style, so the gate can assert it.

#include <okn/ecs/world.hpp>
#include <okn/ecs/scheduler/scheduler.hpp>
#include <okn/ecs/scheduler/system_graph.hpp>
#include <okn/ecs/serialization/serialize.hpp>
#include <okn/platform/thread/thread_pool.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>

namespace okn::ecs {
namespace {

constexpr int kEntities = 20000;
constexpr int kFrames = 60;
constexpr float kDt = 1.0f / 60.0f;
constexpr float kBound = 100.0f;

struct Position { float x = 0, y = 0, z = 0; };
struct Velocity { float dx = 0, dy = 0, dz = 0; };
struct Angle    { float a = 0, rate = 0; };
struct Energy   { float e = 0; };

// Level 1 (all conflict-free, dispatched concurrently):
class MoveSystem : public System {
public:
    MoveSystem() { set_name("Move"); }
    auto reads() const -> std::vector<ComponentTypeId> override {
        return {World::component_type_id<Velocity>()};
    }
    auto writes() const -> std::vector<ComponentTypeId> override {
        return {World::component_type_id<Position>()};
    }
    void execute(World& world, float dt) override {
        for (auto [e, pos, vel] : world.query<Position, Velocity>()) {
            (void)e;
            pos->x += vel->dx * dt;
            pos->y += vel->dy * dt;
            pos->z += vel->dz * dt;
        }
    }
};

class SpinSystem : public System {
public:
    SpinSystem() { set_name("Spin"); }
    auto writes() const -> std::vector<ComponentTypeId> override {
        return {World::component_type_id<Angle>()};
    }
    void execute(World& world, float dt) override {
        for (auto [e, ang] : world.query<Angle>()) {
            (void)e;
            ang->a += ang->rate * dt;
            if (ang->a > 6.2831853f) { ang->a -= 6.2831853f; }
        }
    }
};

class PulseSystem : public System {
public:
    PulseSystem() { set_name("Pulse"); }
    auto writes() const -> std::vector<ComponentTypeId> override {
        return {World::component_type_id<Energy>()};
    }
    void execute(World& world, float dt) override {
        for (auto [e, en] : world.query<Energy>()) {
            (void)e;
            en->e = en->e * (1.0f - 0.1f * dt) + 0.01f;
        }
    }
};

// Level 2 (reads Position — conflicts with Move's write, so it runs after):
class BounceSystem : public System {
public:
    BounceSystem() { set_name("Bounce"); }
    auto reads() const -> std::vector<ComponentTypeId> override {
        return {World::component_type_id<Position>()};
    }
    auto writes() const -> std::vector<ComponentTypeId> override {
        return {World::component_type_id<Velocity>()};
    }
    void execute(World& world, float) override {
        for (auto [e, pos, vel] : world.query<Position, Velocity>()) {
            (void)e;
            if (pos->x > kBound || pos->x < -kBound) { vel->dx = -vel->dx; }
            if (pos->y > kBound || pos->y < -kBound) { vel->dy = -vel->dy; }
            if (pos->z > kBound || pos->z < -kBound) { vel->dz = -vel->dz; }
        }
    }
};

// Deterministic i-based init — no RNG, so both runs build byte-identical Worlds.
void populate(World& w) {
    for (int i = 0; i < kEntities; ++i) {
        const float fi = static_cast<float>(i);
        Entity e = w.create_entity();
        w.add_component(e, Position{std::fmod(fi * 0.37f, kBound) - kBound * 0.5f,
                                    std::fmod(fi * 0.73f, kBound) - kBound * 0.5f,
                                    std::fmod(fi * 1.13f, kBound) - kBound * 0.5f});
        w.add_component(e, Velocity{10.0f + std::fmod(fi, 17.0f), -8.0f + std::fmod(fi, 13.0f),
                                    6.0f - std::fmod(fi, 11.0f)});
        w.add_component(e, Angle{0.0f, 0.5f + std::fmod(fi, 5.0f)});
        w.add_component(e, Energy{std::fmod(fi, 3.0f)});
    }
}

auto make_graph() -> SystemGraph {
    SystemGraph g;
    g.add_system(std::make_unique<MoveSystem>());
    g.add_system(std::make_unique<SpinSystem>());
    g.add_system(std::make_unique<PulseSystem>());
    g.add_system(std::make_unique<BounceSystem>());
    return g;
}

// Runs the sim and returns {elapsed_ms, final_hash}.
struct RunResult { double ms = 0; u64 hash = 0; };
auto run_sim(okn::platform::WorkStealingThreadPool* pool) -> RunResult {
    World w;
    populate(w);
    SystemGraph graph = make_graph();
    Scheduler sched(graph);
    if (pool != nullptr) { sched.set_job_system(pool); }

    const auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < kFrames; ++f) { sched.run(w, kDt); }
    const auto t1 = std::chrono::steady_clock::now();

    RunResult r;
    r.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    r.hash = state_hash(w);
    return r;
}

} // namespace
} // namespace okn::ecs

int main() {
    using namespace okn::ecs;

    // The untouched baseline hash (proves the sim below actually mutated state).
    World initial;
    populate(initial);
    const u64 hash0 = state_hash(initial);

    const RunResult seq = run_sim(nullptr);

    okn::platform::WorkStealingThreadPool pool(4);
    const RunResult par = run_sim(&pool);

    const double seq_frame = seq.ms / kFrames;
    const double par_frame = par.ms / kFrames;
    const double speedup = par.ms > 0.0 ? seq.ms / par.ms : 0.0;

    const bool hashes_match = seq.hash == par.hash;
    const bool sim_ran = seq.hash != hash0;
    const bool budget_ok = par_frame < 100.0;   // LOOSE: a scale tripwire, not a benchmark assert

    std::ofstream out("swarm_result.txt", std::ios::trunc);
    const char* verdict = (hashes_match && sim_ran && budget_ok) ? "SWARM OK" : "SWARM FAIL";
    char line[256];
    std::snprintf(line, sizeof(line),
                  "%s n=%d frames=%d seqMs=%.1f parMs=%.1f perFrameMs=%.2f speedup=%.2fx "
                  "workers=%u hashMatch=%d simRan=%d budget=%d",
                  verdict, kEntities, kFrames, seq.ms, par.ms, par_frame, speedup,
                  static_cast<unsigned>(pool.worker_count()),
                  hashes_match ? 1 : 0, sim_ran ? 1 : 0, budget_ok ? 1 : 0);
    out << line << "\n";
    std::printf("%s\n", line);
    (void)seq_frame;

    return (hashes_match && sim_ran && budget_ok) ? 0 : 1;
}
