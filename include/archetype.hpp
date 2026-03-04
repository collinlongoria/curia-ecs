#ifndef CURIA_ARCHETYPE_HPP
#define CURIA_ARCHETYPE_HPP

#include <memory>
#include <unordered_map>

#include "chunk.hpp"
#include "component.hpp"
#include "entity.hpp"

namespace curia {

struct Archetype {
    std::vector<ComponentID> signature;
    std::vector<std::unique_ptr<Chunk>> chunks;

    // Per-chunk entity ID arrays, parallel to chunks vector
    std::vector<std::vector<Entity>> entities_in_chunk;

    // Maps a ComponentID to its byte-offset within the Chunk's internal layout
    std::unordered_map<ComponentID, size_t> component_offsets;
    std::unordered_map<ComponentID, size_t> component_sizes;

    uint32_t entities_per_chunk = 0;
    size_t row_stride = 0; // total bytes per entity

    static void compute_layout(Archetype& arch) {
        size_t stride = 0;
        for (auto cid : arch.signature) {
            stride += arch.component_sizes[cid];
        }
        arch.row_stride = stride;
        if (stride > 0) {
            arch.entities_per_chunk = static_cast<uint32_t>(Chunk::CHUNK_SIZE / stride);
        }
        else {
            arch.entities_per_chunk = Chunk::CHUNK_SIZE; // pack lots
        }

        // Compute offsets
        // Each component array starts after the previous
        size_t offset = 0;
        for (auto cid : arch.signature) {
            arch.component_offsets[cid] = offset;
            offset += arch.component_sizes[cid] * arch.entities_per_chunk;
        }
    }
};

}

#endif //CURIA_ARCHETYPE_HPP