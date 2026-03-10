#ifndef CURIA_WORLD_HPP
#define CURIA_WORLD_HPP

#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#include "archetype.hpp"
#include "entity.hpp"
#include "component.hpp"
#include "query.hpp"

namespace curia {

struct EntityRecord {
    Archetype* archetype = nullptr;
    uint32_t chunk_index = 0;
    uint32_t row_index = 0;
    uint32_t generation = 0;
};

class World {
    std::vector<EntityRecord> entity_directory;
    std::vector<std::unique_ptr<Archetype>> archetypes;

    std::vector<uint32_t> free_indices;

    Archetype* empty_archetype = nullptr;

    // global flat array of component sizes, indexed by ComponentID
    std::vector<size_t> global_component_sizes;

    // call back functions for observers
    std::vector<std::vector<std::function<void(Entity)>>> on_add_observer;
    std::vector<std::vector<std::function<void(Entity)>>> on_remove_observer;

    void register_component_size(ComponentID cid, size_t sz) {
        if (cid >= global_component_sizes.size()) {
            global_component_sizes.resize(cid + 1, 0);
        }
        global_component_sizes[cid] = sz;
    }

    Archetype* find_or_create_archetype(std::vector<ComponentID> sig) {
        std::sort(sig.begin(), sig.end());
        for (auto& a : archetypes) {
            if (a->signature == sig) return a.get();
        }
        auto a = std::make_unique<Archetype>();
        a->signature = sig;
        for (auto cid : sig) {
            a->set_component_size(cid, global_component_sizes[cid]);
        }
        Archetype::compute_layout(*a);
        Archetype* ptr = a.get();
        archetypes.push_back(std::move(a));
        return ptr;
    }

    void move_entity(Entity e, EntityRecord& record, Archetype* dst_arch) {
        Archetype* src_arch = record.archetype;
        uint32_t src_ci = record.chunk_index;
        uint32_t src_row = record.row_index;
        Chunk* src_chunk = src_arch->chunks[src_ci].get();

        auto [dst_ci, dst_row] = dst_arch->allocate_row(e);
        Chunk* dst_chunk = dst_arch->chunks[dst_ci].get();

        for (auto cid : src_arch->signature) {
            if (dst_arch->has_component(cid)) {
                dst_arch->copy_component(cid, dst_chunk, dst_row, src_chunk, src_row, *src_arch);
            }
        }

        Entity moved = src_arch->remove_row(src_ci, src_row);
        if (moved != INVALID_ENTITY) {
            uint32_t moved_idx = EntityTraits::get_index(moved);
            entity_directory[moved_idx].row_index = src_row;
        }

        record.archetype = dst_arch;
        record.chunk_index = dst_ci;
        record.row_index = dst_row;
    }

public:
    World() {
        auto a = std::make_unique<Archetype>();
        a->entities_per_chunk = Chunk::CHUNK_SIZE;
        a->row_stride = 0;
        empty_archetype = a.get();
        archetypes.push_back(std::move(a));
    }

    Entity create() {
        uint32_t index;
        uint32_t generation;
        if (!free_indices.empty()) {
            index = free_indices.back();
            free_indices.pop_back();
            generation = entity_directory[index].generation;
        } else {
            index = static_cast<uint32_t>(entity_directory.size());
            entity_directory.push_back({});
            generation = 0;
        }

        Entity e = EntityTraits::create(index, generation);

        auto [ci, row] = empty_archetype->allocate_row(e);
        EntityRecord& rec = entity_directory[index];
        rec.archetype = empty_archetype;
        rec.chunk_index = ci;
        rec.row_index = row;
        rec.generation = generation;
        return e;
    }

    bool alive(Entity e) const {
        uint32_t idx = EntityTraits::get_index(e);
        if (idx >= entity_directory.size()) return false;
        return entity_directory[idx].generation == EntityTraits::get_generation(e)
            && entity_directory[idx].archetype != nullptr;
    }

    void destroy(Entity e) {
        if (!alive(e)) return;
        uint32_t idx = EntityTraits::get_index(e);
        EntityRecord& record = entity_directory[idx];

        Entity moved = record.archetype->remove_row(record.chunk_index, record.row_index);
        if (moved != INVALID_ENTITY) {
            uint32_t moved_idx = EntityTraits::get_index(moved);
            entity_directory[moved_idx].row_index = record.row_index;
        }

        record.archetype = nullptr;
        record.generation++;
        free_indices.push_back(idx);
    }

    template <IsValidComponent T>
    void add(Entity e, T component_data) {
        assert(alive(e));
        uint32_t idx = EntityTraits::get_index(e);
        EntityRecord& record = entity_directory[idx];

        ComponentID cid = ComponentType<T>::id();
        size_t actual_size = std::is_empty_v<T> ? 0 : sizeof(T);
        register_component_size(cid, actual_size);

        if (record.archetype->has_component(cid)) return;

        Archetype* dst = nullptr;

        // Check the graph for a cached transition
        auto it = record.archetype->add_edges.find(cid);
        if (it != record.archetype->add_edges.end()) {
            dst = it->second;
        }
        else {
            // build new signature and find/create archetype
            std::vector<ComponentID> new_sig = record.archetype->signature;
            new_sig.push_back(cid);

            dst = find_or_create_archetype(std::move(new_sig));

            // Cache the transition bidirectionally for future use
            record.archetype->add_edges[cid] = dst;
            dst->remove_edges[cid] = record.archetype;
        }

        move_entity(e, record, dst);

        if constexpr (!std::is_empty_v<T>) { // only perform write data if actually has a size
            Chunk* chunk = dst->chunks[record.chunk_index].get();
            dst->write_component(cid, chunk, record.row_index, &component_data);
        }

        if (cid < on_add_observer.size()) {
            for (const auto& callback : on_add_observer[cid]) {
                callback(e);
            }
        }
    }

    template <IsValidComponent T>
    void remove(Entity e) {
        assert(alive(e));
        uint32_t idx = EntityTraits::get_index(e);
        EntityRecord& record = entity_directory[idx];

        ComponentID cid = ComponentType<T>::id();

        if (!record.archetype->has_component(cid)) return;

        Archetype* dst = nullptr;

        // Check the graph for a cached transition
        auto it = record.archetype->remove_edges.find(cid);
        if (it != record.archetype->remove_edges.end()) {
            dst = it->second;
        }
        else {
            // Build new signature and find/create archetype
            std::vector<ComponentID> new_sig;
            for (auto c : record.archetype->signature) {
                if (c != cid) new_sig.push_back(c);
            }

            if (new_sig.empty()) {
                dst = empty_archetype;
            }
            else {
                dst = find_or_create_archetype(std::move(new_sig));
            }

            // Cache the transition for future use
            record.archetype->remove_edges[cid] = dst;
            dst->add_edges[cid] = record.archetype;
        }

        move_entity(e, record, dst);

        // trigger remove observe
        if (cid < on_remove_observer.size()) {
            for (const auto& callback : on_remove_observer[cid]) {
                callback(e);
            }
        }
    }

    template <IsValidComponent T>
    bool has(Entity e) const {
        if (!alive(e)) return false;
        uint32_t idx = EntityTraits::get_index(e);
        const EntityRecord& record = entity_directory[idx];
        return record.archetype->has_component(ComponentType<T>::id());
    }

    template <IsValidComponent T>
    T* get(Entity e) {
        if (!alive(e)) return nullptr;
        uint32_t idx = EntityTraits::get_index(e);
        EntityRecord& record = entity_directory[idx];
        ComponentID cid = ComponentType<T>::id();
        if (!record.archetype->has_component(cid)) return nullptr;

        // tag returns static dummy instance
        if constexpr (std::is_empty_v<T>) {
            static thread_local T dummy{};
            return &dummy;
        }

        Chunk* chunk = record.archetype->chunks[record.chunk_index].get();
        size_t offset = record.archetype->component_offsets[cid]
                      + record.row_index * sizeof(T);
        return reinterpret_cast<T*>(chunk->data + offset);
    }

    template <IsValidComponent T>
    const T* get(Entity e) const {
        if (!alive(e)) return nullptr;
        uint32_t idx = EntityTraits::get_index(e);
        const EntityRecord& record = entity_directory[idx];
        ComponentID cid = ComponentType<T>::id();
        if (!record.archetype->has_component(cid)) return nullptr;

        if constexpr (std::is_empty_v<T>) {
            static thread_local T dummy{};
            return &dummy;
        }

        const Chunk* chunk = record.archetype->chunks[record.chunk_index].get();
        size_t offset = record.archetype->component_offsets[cid]
                      + record.row_index * sizeof(T);
        return reinterpret_cast<const T*>(chunk->data + offset);
    }

    template <IsValidComponent T>
    void set(Entity e, T component_data) {
        if constexpr (std::is_empty_v<T>) return; // no data to set on tags
        T* ptr = get<T>(e);
        if (ptr) *ptr = component_data;
    }

    template <typename... Components>
    Query<Components...> query() {
        Query<Components...> q;
        q.refresh(archetypes);
        return q;
    }

    size_t entity_count() const {
        size_t count = 0;
        for (auto& a : archetypes) {
            for (auto& c : a->chunks) {
                count += c->entity_count;
            }
        }
        return count;
    }

    size_t archetype_count() const {
        return archetypes.size();
    }

    const std::vector<std::unique_ptr<Archetype>>& get_archetypes() const {
        return archetypes;
    }

    // observer methods
    template<IsValidComponent T>
    void observe_add(std::function<void(Entity)> callback) {
        ComponentID cid = ComponentType<T>::id();
        if (cid >= on_add_observer.size()) {
            on_add_observer.resize(cid + 1);
        }
        on_add_observer[cid].push_back(std::move(callback));
    }

    template<IsValidComponent T>
    void observe_remove(std::function<void(Entity)> callback) {
        ComponentID cid = ComponentType<T>::id();
        if (cid >= on_remove_observer.size()) {
            on_remove_observer.resize(cid + 1);
        }
        on_remove_observer[cid].push_back(std::move(callback));
    }
};

}

#endif