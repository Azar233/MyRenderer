#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace pathtracer {

struct RenderTile {
    std::uint32_t xBegin{0U};
    std::uint32_t yBegin{0U};
    std::uint32_t xEnd{0U};
    std::uint32_t yEnd{0U};
};

std::vector<RenderTile> makeRenderTiles(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t tileSize
);

class TileThreadPool {
  public:
    explicit TileThreadPool(std::size_t workerCount);
    ~TileThreadPool();

    TileThreadPool(const TileThreadPool&) = delete;
    TileThreadPool& operator=(const TileThreadPool&) = delete;

    std::size_t workerCount() const { return workers_.size(); }
    bool execute(
        std::size_t itemCount,
        const std::atomic<bool>* cancel,
        std::function<void(std::size_t)> function
    );

  private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::condition_variable workFinished_;
    std::function<void(std::size_t)> function_;
    const std::atomic<bool>* cancel_{nullptr};
    std::atomic<std::size_t> nextItem_{0U};
    std::size_t itemCount_{0U};
    std::size_t workersRemaining_{0U};
    std::uint64_t generation_{0U};
    std::exception_ptr exception_;
    bool stopping_{false};
};

} // namespace pathtracer
