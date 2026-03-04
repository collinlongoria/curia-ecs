#ifndef CURIA_CHUNK_HPP
#define CURIA_CHUNK_HPP

#include <cstddef>
#include <vector>
#include <memory>

namespace curia {

struct Chunk {
    static constexpr size_t CHUNK_SIZE = 16384; // 16KB
    alignas(16) std::byte data[CHUNK_SIZE];
    uint32_t entity_count = 0;

    // Pointer arithmetic to get the start of a specific component's array
    void* get_array(size_t component_offset) {
        return data + component_offset;
    }
};

}

#endif //CURIA_CHUNK_HPP