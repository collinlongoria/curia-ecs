#ifndef CURIA_WORLD_HPP
#define CURIA_WORLD_HPP

#include <algorithm>
#include <cassert>
#include <cstring>
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

    // free list for recycled entity indices
    std::vector<uint32_t> free_indices;

    // the empty archetype new entities start in
    Archetype* empty_archetype = nullptr;

    Archetype* find_or_create_archetype(std::vector<ComponentID> sig, const std::unordered_map<ComponentID, size_t>& sizes) {
        std::sort(sig.begin(), sig.end());
        for (auto& a : archetypes) {
            if (a->signature == sig) return a.get();
        }
        auto a = std::make_unique<Archetype>();
        a->signature = sig;
        for (auto cid : sig) {
            a->component_sizes[cid] = sizes.at(cid);
        }
        Archetype::compute_layout(*a);
        Archetype* ptr = a.get();
        archetypes.push_back(std::move(a));
        return ptr;
    }


    // gathers all known component sizes from an existing archetype + extras
    static std::unordered_map<ComponentID, size_t> gather_sizes(const Archetype* arch) {
        std::unordered_map<ComponentID, size_t> sizes;
        if (arch) {
            sizes = arch->component_sizes;
        }
        return sizes;
    }

    void move_entity(Entity e, EntityRecord& record, Archetype* dst_arch) {
        Archetype* src_arch = record.archetype;
        uint32_t src_ci = record.chunk_index;
        uint32_t src_row = record.row_index;
        Chunk* src_chunk = src_arch->chunks[src_ci].get();

        // allocate in destination
        auto [dst_ci, dst_row] = dst_arch->allocate_row(e);
        Chunk* dst_chunk = dst_arch->chunks[dst_ci].get();

        // copy shared components
        for (auto cid : src_arch->signature) {
            if (dst_arch->component_offsets.count(cid)) {
                dst_arch->copy_component(cid, dst_chunk, dst_row, src_chunk, src_row, *src_arch);
            }
        }

        // swap-and-pop the old slot
        Entity moved = src_arch->remove_row(src_ci, src_row);
        if (moved != INVALID_ENTITY) {
            uint32_t moved_idx = EntityTraits::get_index(moved);
            entity_directory[moved_idx].row_index = src_row;
        }

        // update record
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
        }
        else {
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
        return entity_directory[idx].generation == EntityTraits::get_generation(e) && entity_directory[idx].archetype != nullptr;
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

        // already has this component?
        if (record.archetype->component_offsets.count(cid)) return;

        // build new signature
        std::vector<ComponentID> new_sig = record.archetype->signature;
        new_sig.push_back(cid);

        auto sizes = gather_sizes(record.archetype);
        sizes[cid] = sizeof(T);

        Archetype* dst = find_or_create_archetype(std::move(new_sig), sizes);

        move_entity(e, record, dst);

        // write the new component data
        Chunk* chunk = dst->chunks[record.chunk_index].get();
        dst->write_component(cid, chunk, record.row_index, &component_data);
    }

    template <IsValidComponent T>
    void remove(Entity e) {
        assert(alive(e));
        uint32_t idx = EntityTraits::get_index(e);
        EntityRecord& record = entity_directory[idx];

        ComponentID cid = ComponentType<T>::id();

        if (!record.archetype->component_offsets.count(cid)) return;

        std::vector<ComponentID> new_sig;
        for (auto c : record.archetype->signature) {
            if (c != cid) new_sig.push_back(c);
        }

        auto sizes = gather_sizes(record.archetype);

        Archetype* dst;
        if (new_sig.empty()) {
            dst = empty_archetype;
        }
        else {
            dst = find_or_create_archetype(std::move(new_sig), sizes);
        }

        move_entity(e, record, dst);
    }

    template <IsValidComponent T>
    bool has(Entity e) const {
        if (!alive(e)) return false;
        uint32_t idx = EntityTraits::get_index(e);
        const EntityRecord& record = entity_directory[idx];
        return record.archetype->component_offsets.count(ComponentType<T>::id()) > 0;
    }

    template <IsValidComponent T>
    T* get(Entity e) {
        if (!alive(e)) return nullptr;
        uint32_t idx = EntityTraits::get_index(e);
        EntityRecord& record = entity_directory[idx];
        ComponentID cid = ComponentType<T>::id();
        if (!record.archetype->component_offsets.count(cid)) return nullptr;

        Chunk* chunk = record.archetype->chunks[record.chunk_index].get();
        size_t offset = record.archetype->component_offsets.at(cid) + record.row_index * sizeof(T);
        return reinterpret_cast<T*>(chunk->data + offset);
    }

    template <IsValidComponent T>
    const T* get(Entity e) const {
        if (!alive(e)) return nullptr;
        uint32_t idx = EntityTraits::get_index(e);
        const EntityRecord& record = entity_directory[idx];
        ComponentID cid = ComponentType<T>::id();
        if (!record.archetype->component_offsets.count(cid)) return nullptr;

        const Chunk* chunk = record.archetype->chunks[record.chunk_index].get();
        size_t offset = record.archetype->component_offsets.at(cid) + record.row_index * sizeof(T);
        return reinterpret_cast<const T*>(chunk->data + offset);
    }

    template <IsValidComponent T>
    void set(Entity e, T component_data) {
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
};

}

#endif //CURIA_WORLD_HPP