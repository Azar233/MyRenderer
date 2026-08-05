#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

class RenderPassSequence {
public:
    void add(std::string name, std::function<void()> execute) {
        names_.push_back(name);
        passes_.push_back({std::move(name), std::move(execute)});
    }
    void run() const {
        for (const auto& pass : passes_) pass.execute();
    }
    const std::vector<std::string>& names() const { return names_; }
    std::size_t size() const { return passes_.size(); }

private:
    struct Pass { std::string name; std::function<void()> execute; };
    std::vector<Pass> passes_;
    std::vector<std::string> names_;
};
