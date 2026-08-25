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
    using PassCallback = std::function<void(std::size_t, const std::string&)>;

    void run(
        const PassCallback& before = {},
        const PassCallback& after = {}
    ) const {
        for (std::size_t index = 0; index < passes_.size(); ++index) {
            const auto& pass = passes_[index];
            if (before) before(index, pass.name);
            pass.execute();
            if (after) after(index, pass.name);
        }
    }
    const std::vector<std::string>& names() const { return names_; }
    std::size_t size() const { return passes_.size(); }

private:
    struct Pass { std::string name; std::function<void()> execute; };
    std::vector<Pass> passes_;
    std::vector<std::string> names_;
};
