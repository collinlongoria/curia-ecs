#ifndef CURIA_COMPONENT_HPP
#define CURIA_COMPONENT_HPP

#include <atomic>
#include <type_traits>

namespace curia {

using ComponentID = uint32_t;
inline static constinit std::atomic<ComponentID> s_componentCounter{0};

template <typename T>
concept IsValidComponent = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

template <typename T>
struct ComponentType {
    static ComponentID id() {
        static const ComponentID value = s_componentCounter++;
        return value;
    }
};

}

#endif //CURIA_COMPONENT_HPP