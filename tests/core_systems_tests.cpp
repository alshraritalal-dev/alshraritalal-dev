#include <catch2/catch_test_macros.hpp>

#include "core/containers/ring_buffer.h"
#include "core/containers/slot_map.h"
#include "core/containers/sparse_set.h"
#include "core/memory/arena_allocator.h"
#include "core/memory/pool_allocator.h"
#include "core/memory/tlsf_heap.h"
#include "core/platform/cpu_topology.h"
#include "core/platform/high_res_timer.h"
#include "core/threading/job_system.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

using namespace talal::core;

TEST_CASE("Arena allocator supports aligned marks and rewinds")
{
    ArenaAllocator arena(1024);

    int* value = arena.construct<int>(42);
    REQUIRE(value != nullptr);
    REQUIRE(*value == 42);

    const std::size_t marker = arena.mark();
    void* block = arena.allocate(128, 64);
    REQUIRE(block != nullptr);
    REQUIRE(reinterpret_cast<std::uintptr_t>(block) % 64 == 0);
    REQUIRE(arena.used() > marker);

    arena.rewind(marker);
    REQUIRE(arena.used() == marker);

    arena.reset();
    REQUIRE(arena.used() == 0);
    REQUIRE(arena.remaining() == arena.capacity());
}

TEST_CASE("Pool allocator reuses fixed blocks")
{
    PoolAllocator pool(32, 2);

    void* first = pool.allocate();
    void* second = pool.allocate();
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(first != second);
    REQUIRE(pool.allocate() == nullptr);
    REQUIRE(pool.used() == 2);

    pool.deallocate(first);
    REQUIRE(pool.used() == 1);
    REQUIRE(pool.allocate() == first);
}

TEST_CASE("TLSF heap splits, coalesces, and tracks largest free block")
{
    TlsfHeap heap(64 * 1024);

    void* first = heap.allocate(4096);
    void* second = heap.allocate(8192);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(heap.owns(first));
    REQUIRE(heap.owns(second));

    heap.deallocate(first);
    heap.deallocate(second);
    REQUIRE(heap.largest_free_block() >= 60 * 1024);

    void* large = heap.allocate(48 * 1024);
    REQUIRE(large != nullptr);
    heap.deallocate(large);
}

TEST_CASE("Sparse set keeps dense membership after erase")
{
    SparseSet<std::uint32_t> set;
    REQUIRE(set.insert(4));
    REQUIRE(set.insert(16));
    REQUIRE_FALSE(set.insert(4));
    REQUIRE(set.contains(16));

    REQUIRE(set.erase(4));
    REQUIRE_FALSE(set.contains(4));
    REQUIRE(set.contains(16));
    REQUIRE(set.size() == 1);
}

TEST_CASE("Slot map rejects stale handles")
{
    SlotMap<std::string> slots;

    const SlotMapHandle first = slots.insert("first");
    REQUIRE(slots.contains(first));
    REQUIRE(*slots.get(first) == "first");

    REQUIRE(slots.erase(first));
    REQUIRE_FALSE(slots.contains(first));
    REQUIRE(slots.get(first) == nullptr);

    const SlotMapHandle second = slots.insert("second");
    REQUIRE(slots.contains(second));
    REQUIRE_FALSE(first == second);
    REQUIRE(slots.get(first) == nullptr);
    REQUIRE(*slots.get(second) == "second");
}

TEST_CASE("Ring buffer preserves FIFO order and capacity")
{
    RingBuffer<int, 3> ring;

    REQUIRE(ring.push(1));
    REQUIRE(ring.push(2));
    REQUIRE(ring.push(3));
    REQUIRE(ring.full());
    REQUIRE_FALSE(ring.push(4));

    REQUIRE(ring.pop() == 1);
    REQUIRE(ring.pop() == 2);
    REQUIRE(ring.push(4));
    REQUIRE(ring.pop() == 3);
    REQUIRE(ring.pop() == 4);
    REQUIRE_FALSE(ring.pop().has_value());
}

TEST_CASE("High-res timer exposes monotonic elapsed time")
{
    HighResTimer timer;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(timer.ticks() > 0);
    REQUIRE(timer.seconds() >= 0.0);
    REQUIRE(timer.milliseconds() >= 0.0);
    REQUIRE(HighResTimer::seconds_per_tick() > 0.0);
}

TEST_CASE("CPU topology query provides DEMO_WORKSTATION worker recommendation")
{
    const CpuTopology topology = query_cpu_topology();
    REQUIRE(topology.logicalProcessorCount >= 1);
    REQUIRE(topology.physicalCoreCount >= 1);
    REQUIRE(topology.logicalProcessors.size() == topology.logicalProcessorCount);
    REQUIRE(recommended_worker_count(topology, 20) >= 1);
    REQUIRE(recommended_worker_count(topology, 20) <= 20);
    REQUIRE_FALSE(describe_cpu_topology(topology).empty());
}

TEST_CASE("Work-stealing job system completes submitted jobs")
{
    const CpuTopology topology = query_cpu_topology();
    const std::uint32_t workers = std::min<std::uint32_t>(4, recommended_worker_count(topology, 20));
    JobSystem jobs(workers);
    std::atomic<int> completed = 0;

    for (int i = 0; i < 512; ++i) {
        jobs.submit([&completed] {
            completed.fetch_add(1, std::memory_order_relaxed);
        });
    }

    jobs.wait_idle();
    REQUIRE(completed.load(std::memory_order_relaxed) == 512);
    REQUIRE(jobs.worker_count() == workers);
}
