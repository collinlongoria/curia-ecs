#include <benchmark/benchmark.h>
#include "curia.hpp"

struct Position {
    float x, y, z;
};

struct Velocity {
    float dx, dy, dz;
};

struct Health {
    int32_t hp;
    int32_t max_hp;
};

struct Rotation {
    float angle;
    float axis_x, axis_y, axis_z;
};

// NEW: Zero-sized tag for benchmarking ZST logic
struct IsActive {};

// entity creation
static void BM_EntityCreation(benchmark::State& state) {
    for (auto _ : state) {
        curia::World world;
        for (int64_t i = 0; i < state.range(0); ++i) {
            benchmark::DoNotOptimize(world.create());
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_EntityCreation)->RangeMultiplier(10)->Range(10000, 1000000);

// entity creation + immediate structural add
static void BM_EntityCreateWithComponents(benchmark::State& state) {
    for (auto _ : state) {
        curia::World world;
        for (int64_t i = 0; i < state.range(0); ++i) {
            auto e = world.create();
            world.add<Position>(e, {1.0f, 2.0f, 3.0f});
            world.add<Velocity>(e, {0.1f, 0.2f, 0.3f});
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_EntityCreateWithComponents)->RangeMultiplier(10)->Range(10000, 1000000);

// tests the Batched Command Buffer throughput
static void BM_CommandBufferAdd(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        curia::World world;
        curia::CommandBuffer cmd;
        std::vector<curia::Entity> entities;
        for (int64_t i = 0; i < state.range(0); ++i) {
            entities.push_back(world.create());
        }
        state.ResumeTiming();

        for (auto e : entities) {
            cmd.deferred_add<Position>(e, {0, 0, 0});
            cmd.deferred_add<Velocity>(e, {1, 1, 1});
        }
        cmd.execute(world);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_CommandBufferAdd)->RangeMultiplier(10)->Range(10000, 1000000);

// single-threaded iteration over Position+Velocity
static void BM_IteratePositionVelocity(benchmark::State& state) {
    curia::World world;
    for (int64_t i = 0; i < state.range(0); ++i) {
        auto e = world.create();
        world.add<Position>(e, {0, 0, 0});
        world.add<Velocity>(e, {1, 1, 1});
    }

    auto q = world.query<Position, Velocity>();

    for (auto _ : state) {
        q.each([](Position& pos, Velocity& vel) {
            pos.x += vel.dx;
            pos.y += vel.dy;
            pos.z += vel.dz;
        });
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_IteratePositionVelocity)->RangeMultiplier(10)->Range(10000, 1000000);

// parallel iteration over Position+Velocity
static void BM_ParIteratePositionVelocity(benchmark::State& state) {
    curia::World world;
    for (int64_t i = 0; i < state.range(0); ++i) {
        auto e = world.create();
        world.add<Position>(e, {0, 0, 0});
        world.add<Velocity>(e, {1, 1, 1});
    }

    auto q = world.query<Position, Velocity>();

    for (auto _ : state) {
        q.par_each([](Position& pos, Velocity& vel) {
            pos.x += vel.dx;
            pos.y += vel.dy;
            pos.z += vel.dz;
        });
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_ParIteratePositionVelocity)->RangeMultiplier(10)->Range(10000, 1000000);

// component get (random access)
static void BM_RandomGetComponent(benchmark::State& state) {
    curia::World world;
    std::vector<curia::Entity> entities;
    for (int64_t i = 0; i < state.range(0); ++i) {
        auto e = world.create();
        world.add<Position>(e, {float(i), 0, 0});
        entities.push_back(e);
    }

    size_t idx = 0;
    for (auto _ : state) {
        auto* p = world.get<Position>(entities[idx % entities.size()]);
        benchmark::DoNotOptimize(p);
        idx++;
    }
}
BENCHMARK(BM_RandomGetComponent)->RangeMultiplier(10)->Range(10000, 1000000);

// destroy entities
static void BM_EntityDestroy(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        curia::World world;
        std::vector<curia::Entity> entities;
        for (int64_t i = 0; i < state.range(0); ++i) {
            auto e = world.create();
            world.add<Position>(e, {0, 0, 0});
            entities.push_back(e);
        }
        state.ResumeTiming();
        for (auto e : entities) {
            world.destroy(e);
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_EntityDestroy)->RangeMultiplier(10)->Range(10000, 1000000);

// add component to existing entities (archetype migration)
static void BM_AddComponent(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        curia::World world;
        std::vector<curia::Entity> entities;
        for (int64_t i = 0; i < state.range(0); ++i) {
            auto e = world.create();
            world.add<Position>(e, {0, 0, 0});
            entities.push_back(e);
        }
        state.ResumeTiming();
        for (auto e : entities) {
            world.add<Velocity>(e, {1, 1, 1});
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_AddComponent)->RangeMultiplier(10)->Range(10000, 1000000);

// iterate with 4 components to test wider archetypes
static void BM_IterateWideArchetype(benchmark::State& state) {
    curia::World world;
    for (int64_t i = 0; i < state.range(0); ++i) {
        auto e = world.create();
        world.add<Position>(e, {0, 0, 0});
        world.add<Velocity>(e, {1, 1, 1});
        world.add<Health>(e, {100, 100});
        world.add<Rotation>(e, {0, 0, 1, 0});
    }

    auto q = world.query<Position, Velocity, Health>();

    for (auto _ : state) {
        q.each([](Position& pos, Velocity& vel, Health& hp) {
            pos.x += vel.dx;
            pos.y += vel.dy;
            pos.z += vel.dz;
            if (hp.hp < hp.max_hp) hp.hp++;
        });
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_IterateWideArchetype)->RangeMultiplier(10)->Range(10000, 1000000);


// NEW: ZST Addition (Tests archetype graph migration without memcpy cost)
static void BM_AddZST(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        curia::World world;
        std::vector<curia::Entity> entities;
        for (int64_t i = 0; i < state.range(0); ++i) {
            auto e = world.create();
            world.add<Position>(e, {0, 0, 0});
            entities.push_back(e);
        }
        state.ResumeTiming();
        for (auto e : entities) {
            world.add<IsActive>(e, {});
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_AddZST)->RangeMultiplier(10)->Range(10000, 1000000);

// NEW: ZST Iteration (Tests chunk dummy array extraction efficiency)
static void BM_IterateWithZST(benchmark::State& state) {
    curia::World world;
    for (int64_t i = 0; i < state.range(0); ++i) {
        auto e = world.create();
        world.add<Position>(e, {0, 0, 0});
        world.add<Velocity>(e, {1, 1, 1});
        world.add<IsActive>(e, {});
    }

    auto q = world.query<Position, Velocity, IsActive>();

    for (auto _ : state) {
        q.each([](Position& pos, Velocity& vel, IsActive&) {
            pos.x += vel.dx;
            pos.y += vel.dy;
            pos.z += vel.dz;
        });
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_IterateWithZST)->RangeMultiplier(10)->Range(10000, 1000000);

// NEW: Observer Trigger Overhead (Measures std::function penalty during structural changes)
static void BM_AddComponentWithObserver(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        curia::World world;
        world.observe_add<IsActive>([](curia::Entity) { /* Empty observer */ });
        std::vector<curia::Entity> entities;
        for (int64_t i = 0; i < state.range(0); ++i) {
            auto e = world.create();
            world.add<Position>(e, {0, 0, 0});
            entities.push_back(e);
        }
        state.ResumeTiming();
        for (auto e : entities) {
            world.add<IsActive>(e, {});
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_AddComponentWithObserver)->RangeMultiplier(10)->Range(10000, 1000000);

BENCHMARK_MAIN();