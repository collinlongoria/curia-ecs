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
    static constexpr size_t BUFFER_CAPACITY = 2 * 1024 * 1024; // 2MB Page
    std::unique_ptr<std::byte[]> buffer;

    // atomic for command insertion offsets
    std::atomic<size_t> write_offset{0};

    using ExecFn = void(*)(World&, std::byte*);

    struct CommandHeader {
        ExecFn exec;
        size_t size;
    };

    static size_t align_up(size_t size, size_t alignment = alignof(std::max_align_t)) {
        return (size + alignment - 1) & ~(alignment - 1);
    }

public:
    CommandBuffer() : buffer(new std::byte[BUFFER_CAPACITY]) {}

    template <IsValidComponent T>
    void deferred_add(Entity e, T data) {
        // defines what is being stored in the buffer
        struct Payload {
            Entity e;
            T data;
        };

        size_t payload_size = sizeof(CommandHeader) + sizeof(Payload);
        size_t alloc_size = align_up(payload_size);

        // keeps this lock free
        size_t current_offset = write_offset.fetch_add(alloc_size, std::memory_order_relaxed);
        assert(current_offset + alloc_size <= BUFFER_CAPACITY && "CommandBuffer overflow!");

        std::byte* cmd_ptr = buffer.get() + current_offset;

        // write the header
        auto* header = reinterpret_cast<CommandHeader*>(cmd_ptr);
        header->size = alloc_size;

        // static lambda acts as function pointer
        header->exec = [](World& world, std::byte* ptr) {
            auto* payload = reinterpret_cast<Payload*>(ptr + sizeof(CommandHeader));
            world.add<T>(payload->e, std::move(payload->data));
            // explicitly calls the deconstructor because there is a chance memory was allocated internally
            payload->~Payload();
        };

        // write the payload
        new (cmd_ptr + sizeof(CommandHeader)) Payload{e, std::move(data)};
    }

    void deferred_destroy(Entity e) {
        size_t alloc_size = align_up(sizeof(CommandHeader) + sizeof(Entity));
        size_t current_offset = write_offset.fetch_add(alloc_size, std::memory_order_relaxed);
        assert(current_offset + alloc_size <= BUFFER_CAPACITY && "CommandBuffer overflow!");

        std::byte* cmd_ptr = buffer.get() + current_offset;

        auto* header = reinterpret_cast<CommandHeader*>(cmd_ptr);
        header->size = alloc_size;
        header->exec = [](World& world, std::byte* ptr) {
            auto* entity_ptr = reinterpret_cast<Entity*>(ptr + sizeof(CommandHeader));
            world.destroy(*entity_ptr);
        };

        new (cmd_ptr + sizeof(CommandHeader)) Entity{e};
    }

    template <IsValidComponent T>
    void deferred_remove(Entity e) {
        size_t alloc_size = align_up(sizeof(CommandHeader) + sizeof(Entity));
        size_t current_offset = write_offset.fetch_add(alloc_size, std::memory_order_relaxed);
        assert(current_offset + alloc_size <= BUFFER_CAPACITY && "CommandBuffer overflow!");

        std::byte* cmd_ptr = buffer.get() + current_offset;

        auto* header = reinterpret_cast<CommandHeader*>(cmd_ptr);
        header->size = alloc_size;
        header->exec = [](World& world, std::byte* ptr) {
            auto* entity_ptr = reinterpret_cast<Entity*>(ptr + sizeof(CommandHeader));
            world.remove<T>(*entity_ptr);
        };

        new (cmd_ptr + sizeof(CommandHeader)) Entity{e};
    }

    void execute(World& world) {
        size_t end_offset = write_offset.load(std::memory_order_acquire);
        size_t current = 0;

        // go through the byte buffer
        while (current < end_offset) {
            std::byte* cmd_ptr = buffer.get() + current;
            auto* header = reinterpret_cast<CommandHeader*>(cmd_ptr);

            // execute each function pointer
            header->exec(world, cmd_ptr);

            // jump to the next command
            current += header->size;
        }

        // reset the buffer for the next frame
        clear();
    }

    void clear() {
        // CANNOT call this without calling execute first, it WILL cause memory leaks if ANY component has a complex deconstructor
        write_offset.store(0, std::memory_order_release);
    }

    [[nodiscard]] bool empty() const {
        return write_offset.load(std::memory_order_relaxed) == 0;
    }

};

}

#endif //CURIA_COMMAND_BUFFER_HPP