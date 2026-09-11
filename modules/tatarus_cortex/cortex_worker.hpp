#pragma once

#include "tatarus/cortex.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace tatarus::cortex::detail {

class CortexWorker {
public:
    CortexWorker(std::shared_ptr<ICortexModelClient> client, std::size_t queueCapacity);
    ~CortexWorker();

    CortexWorker(const CortexWorker&) = delete;
    CortexWorker& operator=(const CortexWorker&) = delete;

    [[nodiscard]] bool submit(CortexRequest request);
    [[nodiscard]] std::optional<CortexCompletedRequest> poll();
    [[nodiscard]] bool idle() const;
    void stop();

private:
    void run();

    std::shared_ptr<ICortexModelClient> client_;
    std::size_t queueCapacity_ = 2;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<CortexRequest> requests_;
    std::deque<CortexCompletedRequest> completed_;
    std::thread thread_;
    bool stopping_ = false;
    bool working_ = false;
};

} // namespace tatarus::cortex::detail
