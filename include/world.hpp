#ifndef CURIA_ECS_WORLD_HPP
#define CURIA_ECS_WORLD_HPP

#include "archetype.hpp"
#include "entity.hpp"

namespace curia {

struct EntityRecord {
    Archetype* archetype = nullptr;
    Chunk* chunk = nullptr;
    uint32_t row_index = 0;
};

class World {
    std::vector<EntityRecord> entity_directory;
    std::vector<std::unique_ptr<Archetype>> archetypes;

public:
    Entity create();

    template<IsValidComponent T>
    void add(Entity e, T component_data) {
        EntityRecord& record = entity_directory[EntityTraits::get_index(e)];

        // 1. Find or create the new Archetype signature (Old Signature + T)
        // 2. Allocate space in the new Archetype's active Chunk
        // 3. memcpy all old components from record.chunk to the new Chunk
        // 4. memcpy the new component_data into the new Chunk
        // 5. SWAP AND POP: Move the last entity in the old Chunk into the vacated slot to keep arrays packed.
        // 6. Update the EntityRecord for both moved entities.
    }
};

}

#endif //CURIA_ECS_WORLD_HPP