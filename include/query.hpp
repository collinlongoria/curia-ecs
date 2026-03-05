#ifndef CURIA_QUERY_HPP
#define CURIA_QUERY_HPP

#include <algorithm>
#include <execution>
#include <tuple>
#include <vector>

#include "archetype.hpp"
#include "component.hpp"

namespace curia {

template <typename... Components>
class Query {
    std::vector<Archetype*> matching_archetypes;

    static bool archetype_matches(const Archetype* arch) {
        auto has = [&](ComponentID cid) {
            return arch->component_offsets.count(cid) > 0;
        };
        return (has(ComponentType<Components>::id()) && ...);
    }

public:
    void refresh(const std::vector<std::unique_ptr<Archetype>>& all_archetypes) {
        matching_archetypes.clear();
        for (auto& a : all_archetypes) {
            if (archetype_matches(a.get())) {
                matching_archetypes.push_back(a.get());
            }
        }
    }

    // single-threaded iteration
    template <typename Func>
    void each(Func&& callback) {
        for (Archetype* arch : matching_archetypes) {
            for (auto& chunk_ptr : arch->chunks) {
                Chunk* chunk = chunk_ptr.get();
                auto arrays = std::make_tuple(
                    static_cast<Components*>(chunk->get_array(
                        arch->component_offsets.at(ComponentType<Components>::id())))...
                );
                for (uint32_t i = 0; i < chunk->entity_count; ++i) {
                    callback(std::get<Components*>(arrays)[i]...);
                }
            }
        }
    }

    // parallel iteration across chunks
    template <typename Func>
    void par_each(Func&& callback) {
        std::vector<std::pair<Archetype*, Chunk*>> work;
        for (Archetype* arch : matching_archetypes) {
            for (auto& chunk_ptr : arch->chunks) {
                work.push_back({arch, chunk_ptr.get()});
            }
        }

        std::for_each(std::execution::par_unseq, work.begin(), work.end(),
            [&](std::pair<Archetype*, Chunk*> item) {
                auto [arch, chunk] = item;
                auto arrays = std::make_tuple(
                    static_cast<Components*>(chunk->get_array(
                        arch->component_offsets.at(ComponentType<Components>::id())))...
                );
                for (uint32_t i = 0; i < chunk->entity_count; ++i) {
                    callback(std::get<Components*>(arrays)[i]...);
                }
            }
        );
    }

    // iteration with entity handle included
    template <typename Func>
    void each_with_entity(Func&& callback) {
        for (Archetype* arch : matching_archetypes) {
            for (uint32_t ci = 0; ci < arch->chunks.size(); ++ci) {
                Chunk* chunk = arch->chunks[ci].get();
                auto arrays = std::make_tuple(
                    static_cast<Components*>(chunk->get_array(
                        arch->component_offsets.at(ComponentType<Components>::id())))...
                );
                for (uint32_t i = 0; i < chunk->entity_count; ++i) {
                    Entity e = arch->entities_in_chunk[ci][i];
                    callback(e, std::get<Components*>(arrays)[i]...);
                }
            }
        }
    }
};

}

#endif