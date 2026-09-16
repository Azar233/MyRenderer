#include "pathtracer/TileScheduler.h"

#include <algorithm>
#include <stdexcept>

namespace pathtracer {

std::vector<RenderTile> makeRenderTiles(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t tileSize
) {
    if (!width || !height || !tileSize)
        throw std::invalid_argument("Invalid render tile dimensions");
    std::vector<RenderTile> tiles;
    const std::size_t columns = (static_cast<std::size_t>(width) + tileSize - 1U) / tileSize;
    const std::size_t rows = (static_cast<std::size_t>(height) + tileSize - 1U) / tileSize;
    tiles.reserve(columns * rows);
    for (std::uint32_t y = 0U; y < height; y += tileSize) {
        for (std::uint32_t x = 0U; x < width; x += tileSize) {
            tiles.push_back(RenderTile{
                x,
                y,
                std::min<std::uint32_t>(x + tileSize, width),
                std::min<std::uint32_t>(y + tileSize, height)
            });
        }
    }
    return tiles;
}

TileThreadPool::TileThreadPool(std::size_t workerCount) {
    workerCount = std::max<std::size_t>(workerCount, 1U);
    workers_.reserve(workerCount);
    try {
        for (std::size_t index = 0U; index < workerCount; ++index)
            workers_.emplace_back(&TileThreadPool::workerLoop, this);
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        workAvailable_.notify_all();
        for (auto& worker : workers_)
            if (worker.joinable()) worker.join();
        throw;
    }
}

TileThreadPool::~TileThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    workAvailable_.notify_all();
    for (auto& worker : workers_)
        if (worker.joinable()) worker.join();
}

bool TileThreadPool::execute(
    std::size_t itemCount,
    const std::atomic<bool>* cancel,
    std::function<void(std::size_t)> function
) {
    if (!function) throw std::invalid_argument("Missing tile function");
    if (!itemCount) return true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (workersRemaining_ != 0U)
            throw std::logic_error("Tile thread pool already executing");
        function_ = std::move(function);
        cancel_ = cancel;
        itemCount_ = itemCount;
        nextItem_.store(0U);
        workersRemaining_ = workers_.size();
        exception_ = nullptr;
        ++generation_;
    }
    workAvailable_.notify_all();
    std::exception_ptr exception;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        workFinished_.wait(lock, [this] { return workersRemaining_ == 0U; });
        exception = exception_;
        function_ = {};
        cancel_ = nullptr;
        itemCount_ = 0U;
        exception_ = nullptr;
    }
    if (exception) std::rethrow_exception(exception);
    return !(cancel && cancel->load());
}

void TileThreadPool::workerLoop() {
    std::uint64_t observedGeneration = 0U;
    for (;;) {
        std::unique_lock<std::mutex> lock(mutex_);
        workAvailable_.wait(lock, [this, &observedGeneration] {
            return stopping_ || generation_ != observedGeneration;
        });
        if (stopping_) return;
        observedGeneration = generation_;
        lock.unlock();

        for (;;) {
            if (cancel_ && cancel_->load()) break;
            const std::size_t item = nextItem_.fetch_add(1U);
            if (item >= itemCount_) break;
            try {
                function_(item);
            } catch (...) {
                std::lock_guard<std::mutex> exceptionLock(mutex_);
                if (!exception_) exception_ = std::current_exception();
                nextItem_.store(itemCount_);
                break;
            }
        }

        lock.lock();
        if (--workersRemaining_ == 0U)
            workFinished_.notify_one();
    }
}

} // namespace pathtracer
