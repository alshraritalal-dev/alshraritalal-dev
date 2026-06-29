#pragma once

#include "core/platform/cpu_topology.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace talal::core {

class JobSystem {
public:
    explicit JobSystem(std::uint32_t workerCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void submit(std::function<void()> job);
    void wait_idle();

    std::uint32_t worker_count() const noexcept { return workerCount_; }
    const CpuTopology& topology() const noexcept { return topology_; }

private:
    struct Worker {
        std::deque<std::function<void()>> queue;
        std::mutex mutex;
        std::thread thread;
        LogicalProcessor logicalProcessor;
    };

    bool try_pop_local(std::uint32_t workerIndex, std::function<void()>& outJob);
    bool try_steal(std::uint32_t thiefIndex, std::function<void()>& outJob);
    void worker_loop(std::uint32_t workerIndex);
    void bind_worker_to_topology(std::uint32_t workerIndex);
    void finish_job() noexcept;

    CpuTopology topology_;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::uint32_t workerCount_ = 0;
    std::atomic<bool> stopping_ = false;
    std::atomic<std::uint64_t> pendingJobs_ = 0;
    std::atomic<std::uint32_t> nextSubmitWorker_ = 0;
    std::mutex wakeMutex_;
    std::condition_variable wakeCv_;
    std::mutex idleMutex_;
    std::condition_variable idleCv_;
};

} // namespace talal::core
