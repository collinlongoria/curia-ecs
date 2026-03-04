#include <benchmark/benchmark.h>

static void BM_EntityCreation(benchmark::State& state) {
    for (auto _ : state) {

    }
}

BENCHMARK(BM_EntityCreation)->RangeMultiplier(10)->Range(10000, 1000000);

BENCHMARK_MAIN();