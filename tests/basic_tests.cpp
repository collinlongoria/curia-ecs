#include <cassert>
#include <cstdio>
#include "curia.hpp"

struct Position { float x, y, z; };
struct Velocity { float dx, dy, dz; };
struct Health { int32_t hp, max_hp; };

int main() {
    curia::World world;

    // basic creation
    auto e1 = world.create();
    auto e2 = world.create();
    assert(world.alive(e1));
    assert(world.alive(e2));

    // add components
    world.add<Position>(e1, {1.0f, 2.0f, 3.0f});
    world.add<Velocity>(e1, {0.1f, 0.2f, 0.3f});
    world.add<Position>(e2, {10.0f, 20.0f, 30.0f});

    // has / get
    assert(world.has<Position>(e1));
    assert(world.has<Velocity>(e1));
    assert(!world.has<Health>(e1));
    assert(world.has<Position>(e2));
    assert(!world.has<Velocity>(e2));

    auto* p1 = world.get<Position>(e1);
    assert(p1 && p1->x == 1.0f && p1->y == 2.0f);

    auto* v1 = world.get<Velocity>(e1);
    assert(v1 && v1->dx == 0.1f);

    // set
    world.set<Position>(e1, {5.0f, 6.0f, 7.0f});
    p1 = world.get<Position>(e1);
    assert(p1 && p1->x == 5.0f);

    // query iteration
    int count = 0;
    auto q = world.query<Position, Velocity>();
    q.each([&](Position& pos, Velocity& vel) {
        pos.x += vel.dx;
        count++;
    });
    assert(count == 1);
    p1 = world.get<Position>(e1);
    assert(p1 && p1->x == 5.1f);

    // query with entity
    auto q2 = world.query<Position>();
    int count2 = 0;
    q2.each_with_entity([&](curia::Entity e, Position& pos) {
        count2++;
    });
    assert(count2 == 2);

    // remove component
    world.remove<Velocity>(e1);
    assert(!world.has<Velocity>(e1));
    assert(world.has<Position>(e1));
    p1 = world.get<Position>(e1);
    assert(p1 != nullptr);

    // destroy
    world.destroy(e1);
    assert(!world.alive(e1));
    assert(world.alive(e2));

    // recycled entity gets new generation
    auto e3 = world.create();
    assert(world.alive(e3));
    assert(!world.alive(e1)); // old handle still dead

    // command buffer
    curia::CommandBuffer cmd;
    auto e4 = world.create();
    cmd.deferred_add<Position>(e4, {100, 200, 300});
    assert(!world.has<Position>(e4)); // not applied yet
    cmd.execute(world);
    assert(world.has<Position>(e4));
    auto* p4 = world.get<Position>(e4);
    assert(p4 && p4->x == 100.0f);

    // bulk test
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

    // verify first entity got updated
    auto* pb = world.get<Position>(bulk[0]);
    assert(pb && pb->x == 1.0f);
    auto* pb2 = world.get<Position>(bulk[999]);
    assert(pb2 && pb2->x == 1000.0f);

    // parallel iteration smoke test
    q3 = world.query<Position, Velocity>();
    q3.par_each([](Position& p, Velocity& v) {
        p.y += 1.0f;
    });

    for (auto& ent : bulk) {
        auto* pp = world.get<Position>(ent);
        assert(pp && pp->y == 1.0f);
    }

    // destroy half
    for (size_t i = 0; i < bulk.size(); i += 2) {
        world.destroy(bulk[i]);
    }

    int alive_count = 0;
    auto q4 = world.query<Position>();
    q4.each([&](Position&) { alive_count++; });
    // 5000 from bulk + e2(has Pos) + e4(has Pos) = 5002
    // e3 has no components, so not in Position query
    assert(alive_count == 5002);

    std::printf("all tests passed\n");
    return 0;
}