// NETBOX — the netcode's first game consumer: a headless server-authoritative
// state-sync demo (console exe like swarm/voidborne; no window, no GPU).
//
// SERVER: 32 bouncing boxes (a POD Body2D) stepped deterministically on an okn-ecs
// World. Each tick it snapshots the world (okn-network Snapshot, sorted-by-id),
// encodes a DELTA against the previous tick, and sends it RELIABLY over the
// testkit FaultyLink — the hostile channel that drops every 3rd and reorders every
// other data frame. CLIENT: receives the deltas in order (the ReliabilityLayer's
// job), applies each onto its mirror snapshot, and materializes the blobs into its
// OWN World through the type-erased reflection (add_component_by_id /
// component_data_by_id). VERIFY: per-id blob equality AND okn-ecs state_hash
// equality between the two Worlds — the replication stack, end to end, under loss.
//
// Result marker: netbox_result.txt "NETBOX SYNC OK ..." (asserted by the gate).

#include <okn/ecs/world.hpp>
#include <okn/ecs/serialization/serialize.hpp>   // state_hash — the determinism oracle
#include <okn/network/message/snapshot.hpp>
#include <okn/network/reliability/reliability_layer.hpp>
#include <okn/network/testkit/loopback_link.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace {

using okn::ecs::Entity;
using okn::ecs::World;
using okn::network::Snapshot;
using okn::network::ReliabilityLayer;
using okn::network::testkit::FaultyLink;
using u8 = okn::network::u8;
using u32 = okn::network::u32;

constexpr int kBoxes = 32;
constexpr int kTicks = 200;
constexpr float kDt = 1.0f / 60.0f;
constexpr float kWall = 10.0f;

// The replicated component: a flat POD, byte-copied into snapshot blobs.
struct Body2D {
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
};

void step_sim(World& w) {
    for (auto [e, b] : w.query<Body2D>()) {
        b->x += b->vx * kDt;
        b->y += b->vy * kDt;
        if (b->x > kWall) { b->x = kWall; b->vx = -b->vx; }
        if (b->x < -kWall) { b->x = -kWall; b->vx = -b->vx; }
        if (b->y > kWall) { b->y = kWall; b->vy = -b->vy; }
        if (b->y < -kWall) { b->y = -kWall; b->vy = -b->vy; }
    }
}

// Snapshot the server world: one EntityState per entity, id = the entity index
// (stable here — nothing is destroyed), blob = the raw Body2D bytes.
void snapshot_world(World& w, Snapshot& out) {
    out.clear();
    for (auto [e, b] : w.query<Body2D>()) {
        out.set(e.index(), b, sizeof(Body2D));
    }
}

// Messages are length-prefixed: ReliabilityLayer::recv fills a buffer but does not
// report the payload size, so the delta's byte count travels in the first 4 bytes.
auto frame_message(const std::vector<u8>& delta) -> std::vector<u8> {
    std::vector<u8> msg(4 + delta.size());
    const u32 len = static_cast<u32>(delta.size());
    std::memcpy(msg.data(), &len, 4);
    std::memcpy(msg.data() + 4, delta.data(), delta.size());
    return msg;
}

}  // namespace

int main() {
    // ── the wire: a deterministic lossy/reordering link + reliability on both ends ──
    FaultyLink link;
    ReliabilityLayer server_rel(link.a);
    ReliabilityLayer client_rel(link.b);
    ReliabilityLayer::Config cfg{};
    cfg.resend_timeout = 0.05f;
    cfg.max_retries = 200;
    server_rel.configure(cfg);
    client_rel.configure(cfg);

    // ── server world: 32 deterministic bouncing boxes ──
    World server;
    const auto body_id = World::component_type_id<Body2D>();
    for (int i = 0; i < kBoxes; ++i) {
        const Entity e = server.create_entity();
        Body2D b;
        b.x = static_cast<float>((i * 7) % 19) - 9.0f;
        b.y = static_cast<float>((i * 13) % 17) - 8.0f;
        b.vx = 1.5f + 0.37f * static_cast<float>(i % 5);
        b.vy = -2.0f + 0.53f * static_cast<float>(i % 7);
        server.add_component(e, b);
    }

    // ── client world: starts EMPTY; everything it knows arrived over the wire ──
    World client;
    Snapshot server_prev;                       // what the server last sent
    Snapshot client_mirror;                     // what the client has reassembled
    std::unordered_map<u32, Entity> id_to_ent;  // snapshot id -> client entity

    std::vector<u8> rx(64 * 1024);
    int deltas_applied = 0;

    // The server must drain its inbox too: the client's ACK frames are processed
    // inside recv(), and without them the congestion window never opens.
    auto pump_server = [&]() {
        for (;;) {
            u32 seq = 0;
            if (!server_rel.recv(rx.data(), rx.size(), seq)) {
                if (link.a.has_unread()) { continue; }
                break;
            }
        }
    };

    auto pump_client = [&]() {
        for (;;) {
            u32 seq = 0;
            if (!client_rel.recv(rx.data(), rx.size(), seq)) {
                if (link.b.has_unread()) { continue; }   // consumed a frame, nothing new yet
                break;
            }
            u32 len = 0;
            std::memcpy(&len, rx.data(), 4);
            std::vector<u8> delta(rx.data() + 4, rx.data() + 4 + len);
            u32 tick = 0, base_tick = 0;
            if (client_mirror.apply_delta(delta, tick, base_tick)) { ++deltas_applied; }
            // Materialize the mirror into the client's ECS World via reflection.
            for (const auto& es : client_mirror.entities()) {
                auto it = id_to_ent.find(es.id);
                if (it == id_to_ent.end()) {
                    const Entity e = client.create_entity();
                    client.add_component_by_id(e, body_id, sizeof(Body2D));
                    it = id_to_ent.emplace(es.id, e).first;
                }
                void* dst = client.component_data_by_id(it->second, body_id);
                if (dst != nullptr && es.data.size() == sizeof(Body2D)) {
                    std::memcpy(dst, es.data.data(), sizeof(Body2D));
                }
            }
        }
    };

    // ── run: step, delta, send reliably, pump — under drops + reordering ──
    for (int t = 1; t <= kTicks; ++t) {
        step_sim(server);
        Snapshot cur;
        snapshot_world(server, cur);
        const std::vector<u8> delta =
            cur.encode_delta(server_prev, static_cast<u32>(t), static_cast<u32>(t - 1));
        const std::vector<u8> msg = frame_message(delta);
        server_rel.send(msg.data(), msg.size(), /*reliable=*/true);
        server_prev = cur;

        server_rel.update(kDt);   // retransmit timers
        client_rel.update(kDt);
        pump_client();
        pump_server();            // consume the client's acks (opens the cwnd)
    }
    // Drain: give retransmits time to recover the tail drops.
    for (int i = 0; i < 400 && deltas_applied < kTicks; ++i) {
        server_rel.update(kDt);
        client_rel.update(kDt);
        pump_client();
        pump_server();
    }

    // ── verify: blobs per id, then the two Worlds' state hashes ──
    Snapshot server_final;
    snapshot_world(server, server_final);
    bool blobs_ok = server_final.size() == client_mirror.size() && server_final.size() == kBoxes;
    if (blobs_ok) {
        for (const auto& es : server_final.entities()) {
            const auto* ce = client_mirror.find(es.id);
            if (ce == nullptr || ce->data != es.data) { blobs_ok = false; break; }
        }
    }
    const auto server_hash = okn::ecs::state_hash(server);
    const auto client_hash = okn::ecs::state_hash(client);
    const bool hash_ok = server_hash == client_hash;
    const bool link_was_hostile = link.data_seen > 0 && link.data_seen / 3 > 0;
    const bool ok = blobs_ok && hash_ok && deltas_applied == kTicks && link_was_hostile;

    std::ofstream f("netbox_result.txt");
    f << (ok ? "NETBOX SYNC OK" : "NETBOX SYNC FAIL")
      << " boxes=" << kBoxes << " ticks=" << kTicks
      << " deltas=" << deltas_applied
      << " framesSeen=" << link.data_seen << " dropped~=" << link.data_seen / 3
      << " blobs=" << (blobs_ok ? 1 : 0) << " hash=" << (hash_ok ? 1 : 0)
      << " h=" << std::hex << server_hash;
    std::printf("%s boxes=%d ticks=%d deltas=%d framesSeen=%u blobs=%d hash=%d\n",
                ok ? "NETBOX SYNC OK" : "NETBOX SYNC FAIL", kBoxes, kTicks, deltas_applied,
                link.data_seen, blobs_ok ? 1 : 0, hash_ok ? 1 : 0);
    return ok ? 0 : 1;
}
