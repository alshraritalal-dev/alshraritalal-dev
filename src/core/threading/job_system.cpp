#include "core/threading/job_system.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <exception>

namespace talal::core {

JobSystem::JobSystem(std::uint32_t workerCount)
    : topology_(query_cpu_topology())
{
    workerCount_ = workerCount == 0 ? recommended_worker_count(topology_, 20) : workerCount;
    workerCount_ = std::max(1u, workerCount_);
    workers_.reserve(workerCount_);

    for (std::uint32_t i = 0; i < workerCount_; ++i) {
        auto worker = std::make_unique<Worker>();
        if (!topology_.logicalProcessors.empty()) {
            worker->logicalProcessor = topology_.logicalProcessors[i % topology_.logicalProcessors.size()];
        }
        workers_.push_back(std::move(worker));
    }

    for (std::uint32_t i = 0; i < workerCount_; ++i) {
        workers_[i]->thread = std::thread([this, i] { worker_loop(i); });
    }
}

JobSystem::~JobSystem()
{
    wait_idle();
    stopping_.store(true, std::memory_order_release);
    wakeCv_.notify_all();
    for (auto& worker : workers_) {
        if (worker->thread.joinable()) {
            worker->thread.join();
        }
    }
}

void JobSystem::submit(std::function<void()> job)
{
    pendingJobs_.fetch_add(1, std::memory_order_acq_rel);
    const std::uint32_t index = nextSubmitWorker_.fetch_add(1, std::memory_order_relaxed) % workerCount_;
    {
        std::scoped_lock lock(workers_[index]->mutex);
        workers_[index]->queue.push_back(std::move(job));
    }
    wakeCv_.notify_one();
}

void JobSystem::wait_idle()
{
    std::unique_lock lock(idleMutex_);
    idleCv_.wait(lock, [this] {
        return pendingJobs_.load(std::memory_order_acquire) == 0;
    });
}

bool JobSystem::try_pop_local(std::uint32_t workerIndex, std::function<void()>& outJob)
{
    Worker& worker = *workers_[workerIndex];
    std::scoped_lock lock(worker.mutex);
    if (worker.queue.empty()) {
        return false;
    }
    outJob = std::move(worker.queue.back());
    worker.queue.pop_back();
    return true;
}

bool JobSystem::try_steal(std::uint32_t thiefIndex, std::function<void()>& outJob)
{
    for (std::uint32_t offset = 1; offset < workerCount_; ++offset) {
        const std::uint32_t victimIndex = (thiefIndex + offset) % workerCount_;
        Worker& victim = *workers_[victimIndex];
        std::scoped_lock lock(victim.mutex);
        if (!victim.queue.empty()) {
            outJob = std::move(victim.queue.front());
            victim.queue.pop_front();
            return true;
        }
    }
    return false;
}

void JobSystem::worker_loop(std::uint32_t workerIndex)
{
    bind_worker_to_topology(workerIndex);

    for (;;) {
        std::function<void()> job;
        if (try_pop_local(workerIndex, job) || try_steal(workerIndex, job)) {
            try {
                job();
            } catch (...) {
            }
            finish_job();
            continue;
        }

        if (stopping_.load(std::memory_order_acquire) &&
            pendingJobs_.load(std::memory_order_acquire) == 0) {
            break;
        }

        std::unique_lock lock(wakeMutex_);
        wakeCv_.wait_for(lock, std::chrono::milliseconds(1));
    }
}

void JobSystem::bind_worker_to_topology(std::uint32_t workerIndex)
{
    if (workerIndex >= workers_.size()) {
        return;
    }

    const LogicalProcessor& logical = workers_[workerIndex]->logicalProcessor;
    if (logical.number >= sizeof(KAFFINITY) * 8) {
        return;
    }

    GROUP_AFFINITY affinity = {};
    affinity.Group = logical.group;
    affinity.Mask = KAFFINITY { 1 } << logical.number;
    SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr);

    const std::wstring name = L"TalalJobWorker" + std::to_wstring(workerIndex);
    SetThreadDescription(GetCurrentThread(), name.c_str());
}

void JobSystem::finish_job() noexcept
{
    if (pendingJobs_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        idleCv_.notify_all();
    }
}

} // namespace talal::core
