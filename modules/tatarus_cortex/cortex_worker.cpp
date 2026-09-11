#include "cortex_worker.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace tatarus::cortex::detail {

CortexWorker::CortexWorker(
    std::shared_ptr<ICortexModelClient> client,
    std::size_t queueCapacity)
    : client_(std::move(client)), queueCapacity_(queueCapacity) {
    if (!client_) throw std::invalid_argument("CortexWorker requires a model client");
    if (queueCapacity_ == 0U || queueCapacity_ > 64U) {
        throw std::invalid_argument("CortexWorker queue capacity must be in [1,64]");
    }
    thread_ = std::thread([this] { run(); });
}

CortexWorker::~CortexWorker() {
    stop();
}

bool CortexWorker::submit(CortexRequest request) {
    std::scoped_lock lock(mutex_);
    if (stopping_ || requests_.size() >= queueCapacity_) return false;
    requests_.push_back(std::move(request));
    condition_.notify_one();
    return true;
}

std::optional<CortexCompletedRequest> CortexWorker::poll() {
    std::scoped_lock lock(mutex_);
    if (completed_.empty()) return std::nullopt;
    CortexCompletedRequest item = std::move(completed_.front());
    completed_.pop_front();
    return item;
}

bool CortexWorker::idle() const {
    std::scoped_lock lock(mutex_);
    return requests_.empty() && !working_;
}

void CortexWorker::stop() {
    {
        std::scoped_lock lock(mutex_);
        if (stopping_) {
            if (!thread_.joinable()) return;
        } else {
            stopping_ = true;
            condition_.notify_all();
        }
    }
    if (thread_.joinable()) thread_.join();
}

void CortexWorker::run() {
    while (true) {
        CortexRequest request;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !requests_.empty(); });
            if (stopping_ && requests_.empty()) return;
            request = std::move(requests_.front());
            requests_.pop_front();
            working_ = true;
        }

        CortexCompletedRequest completed;
        completed.request = request;
        try {
            completed.response = client_->complete(request);
            completed.success = true;
        } catch (const std::exception& error) {
            completed.success = false;
            completed.error = error.what();
        } catch (...) {
            completed.success = false;
            completed.error = "Unknown cortex model client failure";
        }

        {
            std::scoped_lock lock(mutex_);
            working_ = false;
            // Bound completed results as well. The host only needs recent results.
            if (completed_.size() >= queueCapacity_ * 2U) completed_.pop_front();
            completed_.push_back(std::move(completed));
        }
    }
}

} // namespace tatarus::cortex::detail
