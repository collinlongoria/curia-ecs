#ifndef CURIA_ENTITY_HPP
#define CURIA_ENTITY_HPP

#include <cstdint>

namespace curia {

using Entity = uint64_t;

struct EntityTraits {
    static constexpr uint32_t get_index(Entity e) { return static_cast<uint32_t>(e); }
    static constexpr uint32_t get_generation(Entity e) { return static_cast<uint32_t>(e >> 32); }
    static constexpr Entity create(uint32_t index, uint32_t generation) {
        return (static_cast<Entity>(generation) << 32) | index;
    }
};

}

#endif //CURIA_ENTITY_HPP