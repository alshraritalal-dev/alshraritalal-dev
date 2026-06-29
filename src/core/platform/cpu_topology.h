#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace talal::core {

struct LogicalProcessor {
    std::uint16_t group = 0;
    std::uint8_t number = 0;
    std::uint32_t coreIndex = 0;
    std::uint8_t efficiencyClass = 0;
};

struct CpuTopology {
    std::uint32_t logicalProcessorCount = 0;
    std::uint32_t physicalCoreCount = 0;
    std::uint32_t performanceCoreCount = 0;
    std::uint32_t efficiencyCoreCount = 0;
    std::vector<LogicalProcessor> logicalProcessors;
};

CpuTopology query_cpu_topology();
std::uint32_t recommended_worker_count(const CpuTopology& topology, std::uint32_t targetLogicalThreads = 20) noexcept;
std::wstring describe_cpu_topology(const CpuTopology& topology);

} // namespace talal::core
