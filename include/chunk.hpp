#ifndef CURIA_CHUNK_HPP
#define CURIA_CHUNK_HPP

#include <cstddef>
#include <cstdint>

namespace curia {

struct Chunk {
    static constexpr size_t CHUNK_SIZE = 16384;
    alignas(16) std::byte data[CHUNK_SIZE];
    uint32_t entity_count = 0;

    void* get_array(size_t component_offset) {
        return data + component_offset;
    }

    const void* get_array(size_t component_offset) const {
        return data + component_offset;
    }
};

}

#endif