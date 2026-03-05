#ifndef CURIA_COMMAND_BUFFER_HPP
#define CURIA_COMMAND_BUFFER_HPP

#include <functional>
#include <mutex>
#include <vector>

#include "component.hpp"
#include "entity.hpp"
#include "world.hpp"


namespace curia {

class CommandBuffer {
    std::vector<std::function<void(World&)>> deferred_commands;
    std::mutex cmd_mutex;

public:
    template <IsValidComponent T>
    void deferred_add(Entity e, T data) {
        std::lock_guard<std::mutex> lock(cmd_mutex);
        deferred_commands.push_back([e, data](World& world) {
            world.add<T>(e, data);
        });
    }

    void deferred_destroy(Entity e) {
        std::lock_guard<std::mutex> lock(cmd_mutex);
        deferred_commands.push_back([e](World& world) {
            world.destroy(e);
        });
    }

    template <IsValidComponent T>
    void deferred_remove(Entity e) {
        std::lock_guard<std::mutex> lock(cmd_mutex);
        deferred_commands.push_back([e](World& world) {
            world.remove<T>(e);
        });
    }

    void execute(World& world) {
        for (auto& cmd : deferred_commands) {
            cmd(world);
        }
        deferred_commands.clear();
    }

    void clear() {
        deferred_commands.clear();
    }

    [[nodiscard]] size_t size() const { return deferred_commands.size(); }
    [[nodiscard]] bool empty() const { return deferred_commands.empty(); }
};

}

#endif //CURIA_COMMAND_BUFFER_HPP