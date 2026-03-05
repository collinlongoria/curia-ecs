#ifndef CURIA_ARCHETYPE_HPP
#define CURIA_ARCHETYPE_HPP

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "chunk.hpp"
#include "component.hpp"
#include "entity.hpp"

namespace curia {

static constexpr size_t INVALID_OFFSET = std::numeric_limits<size_t>::max();

struct Archetype {
    std::vector<ComponentID> signature;
    std::vector<std::unique_ptr<Chunk>> chunks;

    std::vector<std::vector<Entity>> entities_in_chunk;

    // flat arrays indexed by ComponentID, INVALID_OFFSET means not present
    std::vector<size_t> component_offsets;
    std::vector<size_t> component_sizes;

    uint32_t entities_per_chunk = 0;
    size_t row_stride = 0;

    void ensure_flat_size(ComponentID cid) {
        if (cid >= component_offsets.size()) {
            component_offsets.resize(cid + 1, INVALID_OFFSET);
            component_sizes.resize(cid + 1, 0);
        }
    }

    bool has_component(ComponentID cid) const {
        return cid < component_offsets.size() && component_offsets[cid] != INVALID_OFFSET;
    }

    void set_component_size(ComponentID cid, size_t sz) {
        ensure_flat_size(cid);
        component_sizes[cid] = sz;
    }

    static void compute_layout(Archetype& arch) {
        size_t stride = 0;
        for (auto cid : arch.signature) {
            stride += arch.component_sizes[cid];
        }
        arch.row_stride = stride;
        if (stride > 0) {
            arch.entities_per_chunk = static_cast<uint32_t>(Chunk::CHUNK_SIZE / stride);
        } else {
            arch.entities_per_chunk = Chunk::CHUNK_SIZE;
        }

        size_t offset = 0;
        for (auto cid : arch.signature) {
            arch.component_offsets[cid] = offset;
            offset += arch.component_sizes[cid] * arch.entities_per_chunk;
        }
    }

    std::pair<uint32_t, uint32_t> allocate_row(Entity entity) {
        for (uint32_t ci = 0; ci < chunks.size(); ++ci) {
            if (chunks[ci]->entity_count < entities_per_chunk) {
                uint32_t row = chunks[ci]->entity_count;
                chunks[ci]->entity_count++;
                entities_in_chunk[ci].push_back(entity);
                return {ci, row};
            }
        }
        auto c = std::make_unique<Chunk>();
        c->entity_count = 1;
        chunks.push_back(std::move(c));
        entities_in_chunk.push_back({entity});
        return {static_cast<uint32_t>(chunks.size() - 1), 0};
    }

    void copy_component(ComponentID cid,
                        Chunk* dst_chunk, uint32_t dst_row,
                        const Chunk* src_chunk, uint32_t src_row,
                        const Archetype& src_arch) const {
        size_t sz = component_sizes[cid];
        size_t dst_off = component_offsets[cid] + dst_row * sz;
        size_t src_off = src_arch.component_offsets[cid] + src_row * sz;
        std::memcpy(dst_chunk->data + dst_off, src_chunk->data + src_off, sz);
    }

    void write_component(ComponentID cid, Chunk* chunk, uint32_t row, const void* data) {
        size_t sz = component_sizes[cid];
        size_t off = component_offsets[cid] + row * sz;
        std::memcpy(chunk->data + off, data, sz);
    }

    void read_component(ComponentID cid, const Chunk* chunk, uint32_t row, void* out) const {
        size_t sz = component_sizes[cid];
        size_t off = component_offsets[cid] + row * sz;
        std::memcpy(out, chunk->data + off, sz);
    }

    Entity remove_row(uint32_t chunk_idx, uint32_t row) {
        Chunk* chunk = chunks[chunk_idx].get();
        uint32_t last = chunk->entity_count - 1;
        Entity moved = INVALID_ENTITY;

        if (row != last) {
            for (auto cid : signature) {
                size_t sz = component_sizes[cid];
                size_t row_off = component_offsets[cid] + row * sz;
                size_t last_off = component_offsets[cid] + last * sz;
                std::memcpy(chunk->data + row_off, chunk->data + last_off, sz);
            }
            moved = entities_in_chunk[chunk_idx][last];
            entities_in_chunk[chunk_idx][row] = moved;
        }

        entities_in_chunk[chunk_idx].pop_back();
        chunk->entity_count--;
        return moved;
    }
};

}

#endif