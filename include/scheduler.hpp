#ifndef CURIA_SCHEDULER_HPP
#define CURIA_SCHEDULER_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "command_buffer.hpp"
#include "component.hpp"
#include "world.hpp"

namespace curia {

struct SystemNode {
    std::string name;
    std::vector<ComponentID> reads;
    std::vector<ComponentID> writes;
    std::function<void(World&, CommandBuffer&)> exec;
    CommandBuffer cmd;
    int phase = -1;
};

class SystemBuilder {
    SystemNode& node;
public:
    explicit SystemBuilder(SystemNode& node) : node(node) {}

    template <IsValidComponent T>
    SystemBuilder& reads() {
        node.reads.push_back(ComponentType<T>::id());
        return *this;
    }

    template <IsValidComponent T>
    SystemBuilder& writes() {
        node.writes.push_back(ComponentType<T>::id());
        return *this;
    }

    template <typename Func>
    void execute(Func&& fn) {
        node.exec = std::forward<Func>(fn);
    }
};

class Scheduler {
    World& world;
    std::vector<std::unique_ptr<SystemNode>> systems;
    std::vector<std::vector<SystemNode*>> phases;
    bool compiled = false;

public:
    explicit Scheduler(World& w) : world(w) {}

    // Adds system and returns builder to configure dependencies
    SystemBuilder add_system(std::string name) {
        compiled = false; // MUST recmplie
        auto node = std::make_unique<SystemNode>();
        node->name = std::move(name);
        systems.push_back(std::move(node));
        return SystemBuilder(*systems.back());
    }

    // Compile TDG
    void compile() {
        phases.clear();

        for (size_t i = 0; i < systems.size(); ++i) {
            SystemNode* current = systems[i].get();
            int target_phase = 0;

            // Check against all previously added systems
            for (size_t j = 0; j < i; ++j) {
                SystemNode* prev = systems[j].get();
                bool conflict = false;

                // potential read-write conflict
                for (auto r : current->reads) {
                    if (std::find(prev->writes.begin(), prev->writes.end(), r) != prev->writes.end()) conflict = true;
                }
                // write-read and write-write conflict
                for (auto w : current->writes) {
                    if (std::find(prev->reads.begin(), prev->reads.end(), w) != prev->reads.end()) conflict = true;
                    if (std::find(prev->writes.begin(), prev->writes.end(), w) != prev->writes.end()) conflict = true;
                }

                // If data race is found system must run in phase AFTER previous system
                if (conflict) {
                    target_phase = std::max(target_phase, prev->phase + 1);
                }
            }

            current->phase = target_phase;
            if (target_phase >= phases.size()) {
                phases.resize(target_phase + 1);
            }
            phases[target_phase].push_back(current);
        }
        compiled = true;
    }

    // Execute graph
    void run() {
        if (!compiled) compile();

        for (auto& phase : phases) {
            // Run all independent systems in phase concurrently
            std::for_each(std::execution::par_unseq, phase.begin(), phase.end(),
                [this](SystemNode* sys) {
                    sys->exec(world, sys->cmd);
                }
            );

            // Flush all command buffers sequentially
            for (auto* sys : phase) {
                if (!sys->cmd.empty()) {
                    sys->cmd.execute(world);
                }
            }
        }
    }

    // Debug utility to visualize the graph layout
    void print_graph() const {
        std::printf("--- System Dependency Graph ---\n");
        for (size_t i = 0; i < phases.size(); ++i) {
            std::printf("Phase %zu:\n", i);
            for (const auto* sys : phases[i]) {
                std::printf("  [%s]\n", sys->name.c_str());
            }
        }
        std::printf("-------------------------------\n");
    }
};

}

#endif //CURIA_SCHEDULER_HPP