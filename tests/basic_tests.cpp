#include <cassert>
#include <cstdio>
#include <vector>
#include "curia.hpp"

struct Position { float x, y, z; };
struct Velocity { float dx, dy, dz; };
struct Health { int32_t hp, max_hp; };

// NEW: Zero-Sized Tags
struct IsPineapple {};
struct IsFalling {};

int main() {
    curia::World world;

    // --- Basic Creation ---
    auto e1 = world.create();
    auto e2 = world.create();
    assert(world.alive(e1));
    assert(world.alive(e2));

    // --- Add Components ---
    world.add<Position>(e1, {1.0f, 2.0f, 3.0f});
    world.add<Velocity>(e1, {0.1f, 0.2f, 0.3f});
    world.add<Position>(e2, {10.0f, 20.0f, 30.0f});

    // --- Has / Get ---
    assert(world.has<Position>(e1));
    assert(world.has<Velocity>(e1));
    assert(!world.has<Health>(e1));
    assert(world.has<Position>(e2));
    assert(!world.has<Velocity>(e2));

    auto* p1 = world.get<Position>(e1);
    assert(p1 && p1->x == 1.0f && p1->y == 2.0f);

    auto* v1 = world.get<Velocity>(e1);
    assert(v1 && v1->dx == 0.1f);

    // --- Set ---
    world.set<Position>(e1, {5.0f, 6.0f, 7.0f});
    p1 = world.get<Position>(e1);
    assert(p1 && p1->x == 5.0f);

    // --- Query Iteration ---
    int count = 0;
    auto q = world.query<Position, Velocity>();
    q.each([&](Position& pos, Velocity& vel) {
        pos.x += vel.dx;
        count++;
    });
    assert(count == 1);
    p1 = world.get<Position>(e1);
    assert(p1 && p1->x == 5.1f);

    // --- Query With Entity ---
    auto q2 = world.query<Position>();
    int count2 = 0;
    q2.each_with_entity([&](curia::Entity e, Position& pos) {
        count2++;
    });
    assert(count2 == 2);

    // --- Remove Component ---
    world.remove<Velocity>(e1);
    assert(!world.has<Velocity>(e1));
    assert(world.has<Position>(e1));
    p1 = world.get<Position>(e1);
    assert(p1 != nullptr);

    // --- Destroy ---
    world.destroy(e1);
    assert(!world.alive(e1));
    assert(world.alive(e2));

    // --- Recycled entity gets new generation ---
    auto e3 = world.create();
    assert(world.alive(e3));
    assert(!world.alive(e1)); // old handle still dead

    // --- Command Buffer ---
    curia::CommandBuffer cmd;
    auto e4 = world.create();
    cmd.deferred_add<Position>(e4, {100, 200, 300});
    assert(!world.has<Position>(e4)); // not applied yet
    cmd.execute(world);
    assert(world.has<Position>(e4));
    auto* p4 = world.get<Position>(e4);
    assert(p4 && p4->x == 100.0f);

    // --- ZST (Zero-Sized Tag) Tests ---
    auto zst_e = world.create();
    world.add<IsPineapple>(zst_e, {});
    world.add<Position>(zst_e, {1, 1, 1});
    assert(world.has<IsPineapple>(zst_e));

    // Test ZST get
    auto* pineapple_ptr = world.get<IsPineapple>(zst_e);
    assert(pineapple_ptr != nullptr);

    // Test ZST set (should be no-op but compile cleanly)
    world.set<IsPineapple>(zst_e, {});

    // Test ZST iteration
    int zst_count = 0;
    auto zst_q = world.query<Position, IsPineapple>();
    zst_q.each([&](Position&, IsPineapple&) { zst_count++; });
    assert(zst_count == 1);

    // Test ZST removal
    world.remove<IsPineapple>(zst_e);
    assert(!world.has<IsPineapple>(zst_e));


    // --- Observer Tests ---
    int obs_add_count = 0;
    int obs_rem_count = 0;
    world.observe_add<IsFalling>([&](curia::Entity) { obs_add_count++; });
    world.observe_remove<IsFalling>([&](curia::Entity) { obs_rem_count++; });

    auto obs_e = world.create();

    // Trigger observe_add
    world.add<IsFalling>(obs_e, {});
    assert(obs_add_count == 1);

    // Trigger observe_remove
    world.remove<IsFalling>(obs_e);
    assert(obs_rem_count == 1);

    // Test CommandBuffer re-entrancy safety within Observers
    curia::CommandBuffer obs_cmd;
    world.observe_add<Health>([&](curia::Entity e) {
        obs_cmd.deferred_add<IsPineapple>(e, {});
    });

    world.add<Health>(obs_e, {100, 100}); // Triggers observer, which queues ZST addition
    assert(!world.has<IsPineapple>(obs_e)); // Command buffer not executed yet

    obs_cmd.execute(world);
    assert(world.has<IsPineapple>(obs_e)); // Successfully processed safe deferred addition

    // --- Bulk Iteration / Parallel Smoke Test ---
    std::vector<curia::Entity> bulk;
    for (int i = 0; i < 10000; i++) {
        auto e = world.create();
        world.add<Position>(e, {float(i), 0, 0});
        world.add<Velocity>(e, {1, 0, 0});
        bulk.push_back(e);
    }

    auto q3 = world.query<Position, Velocity>();
    q3.each([](Position& p, Velocity& v) {
        p.x += v.dx;
    });

    auto* pb = world.get<Position>(bulk[0]);
    assert(pb && pb->x == 1.0f);
    auto* pb2 = world.get<Position>(bulk[999]);
    assert(pb2 && pb2->x == 1000.0f);

    q3.par_each([](Position& p, Velocity& v) {
        p.y += 1.0f;
    });

    for (auto& ent : bulk) {
        auto* pp = world.get<Position>(ent);
        assert(pp && pp->y == 1.0f);
    }

    // Destroy half
    for (size_t i = 0; i < bulk.size(); i += 2) {
        world.destroy(bulk[i]);
    }

    int alive_count = 0;
    auto q4 = world.query<Position>();
    q4.each([&](Position&) { alive_count++; });
    // 5000 from bulk + e2(has Pos) + e4(has Pos) + zst_e(has Pos) = 5003
    assert(alive_count == 5003);

    std::printf("all tests passed\n");
    return 0;
}