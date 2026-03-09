#ifndef CURIA_COMMAND_BUFFER_HPP
#define CURIA_COMMAND_BUFFER_HPP

#include <memory>
#include <vector>
#include <utility>
#include <limits>

#include "component.hpp"
#include "entity.hpp"
#include "world.hpp"

namespace curia {

class CommandBuffer {
    struct IQueue {
        virtual ~IQueue() = default;
        virtual void execute(World& world) = 0;
        virtual void clear() = 0;
        [[nodiscard]] virtual bool empty() const = 0;
    };

    template <IsValidComponent T>
    struct TypedQueue final : IQueue {
        std::vector<std::pair<Entity, T>> adds;
        std::vector<Entity> removes;

        void execute(World& world) override {
            // compiler will fully inline world functions here
            for (auto& [e, data] : adds) {
                world.add<T>(e, std::move(data));
            }
            for (Entity e : removes) {
                world.remove<T>(e);
            }
        }

        void clear() override {
            adds.clear();
            removes.clear();
        }

        [[nodiscard]] bool empty() const override {
            return adds.empty() && removes.empty();
        }
    };

    std::vector<Entity> destroy_queue;
    std::vector<std::unique_ptr<IQueue>> queues;
    std::vector<size_t> component_to_queue;

    template <IsValidComponent T>
    TypedQueue<T>& get_queue() {
        ComponentID cid = ComponentType<T>::id();

        if (cid >= component_to_queue.size()) {
            component_to_queue.resize(cid + 1, std::numeric_limits<size_t>::max());
        }

        if (component_to_queue[cid] == std::numeric_limits<size_t>::max()) {
            component_to_queue[cid] = queues.size();
            queues.push_back(std::make_unique<TypedQueue<T>>());
        }

        return static_cast<TypedQueue<T>&>(*queues[component_to_queue[cid]]);
    }

public:
    CommandBuffer() = default;

    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;

    CommandBuffer(CommandBuffer&&) = default;
    CommandBuffer& operator=(CommandBuffer&&) = default;

    template <IsValidComponent T>
    void deferred_add(Entity e, T data) {
        get_queue<T>().adds.emplace_back(e, std::move(data));
    }

    template <IsValidComponent T>
    void deferred_remove(Entity e) {
        get_queue<T>().removes.push_back(e);
    }

    void deferred_destroy(Entity e) {
        destroy_queue.push_back(e);
    }

    void execute(World& world) {
        for (auto& q : queues) {
            if (!q->empty()) {
                q->execute(world);
            }
        }

        for (Entity e : destroy_queue) {
            world.destroy(e);
        }

        clear();
    }

    void clear() {
        for (auto& q : queues) {
            q->clear();
        }
        destroy_queue.clear();
    }

    [[nodiscard]] bool empty() const {
        for (const auto& q : queues) {
            if (!q->empty()) return false;
        }
        return destroy_queue.empty();
    }
};

}

#endif //CURIA_COMMAND_BUFFER_HPP