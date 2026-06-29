#include "core/platform/cpu_topology.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <thread>

namespace talal::core {

namespace {

void AddLogicalProcessorsFromMask(
    CpuTopology& topology,
    std::uint32_t coreIndex,
    std::uint8_t efficiencyClass,
    const GROUP_AFFINITY& affinity)
{
    for (std::uint8_t bit = 0; bit < sizeof(KAFFINITY) * 8; ++bit) {
        const KAFFINITY mask = KAFFINITY { 1 } << bit;
        if ((affinity.Mask & mask) != 0) {
            topology.logicalProcessors.push_back(LogicalProcessor {
                affinity.Group,
                bit,
                coreIndex,
                efficiencyClass
            });
        }
    }
}

CpuTopology FallbackTopology()
{
    CpuTopology topology;
    topology.logicalProcessorCount = std::max(1u, std::thread::hardware_concurrency());
    topology.physicalCoreCount = topology.logicalProcessorCount;
    topology.performanceCoreCount = topology.physicalCoreCount;
    topology.efficiencyCoreCount = 0;
    topology.logicalProcessors.reserve(topology.logicalProcessorCount);
    for (std::uint32_t i = 0; i < topology.logicalProcessorCount; ++i) {
        topology.logicalProcessors.push_back(LogicalProcessor { 0, static_cast<std::uint8_t>(i), i, 0 });
    }
    return topology;
}

} // namespace

CpuTopology query_cpu_topology()
{
    DWORD bytes = 0;
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes) == FALSE &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return FallbackTopology();
    }

    std::vector<std::byte> buffer(bytes);
    if (GetLogicalProcessorInformationEx(
            RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
            &bytes) == FALSE) {
        return FallbackTopology();
    }

    CpuTopology topology;
    std::uint32_t coreIndex = 0;
    std::uint8_t maxEfficiencyClass = 0;

    std::byte* cursor = buffer.data();
    const std::byte* end = buffer.data() + bytes;
    while (cursor < end) {
        auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(cursor);
        if (info->Relationship == RelationProcessorCore) {
            const auto efficiency = info->Processor.EfficiencyClass;
            maxEfficiencyClass = std::max(maxEfficiencyClass, efficiency);
            for (WORD groupIndex = 0; groupIndex < info->Processor.GroupCount; ++groupIndex) {
                AddLogicalProcessorsFromMask(topology, coreIndex, efficiency, info->Processor.GroupMask[groupIndex]);
            }
            ++coreIndex;
        }
        cursor += info->Size;
    }

    if (topology.logicalProcessors.empty()) {
        return FallbackTopology();
    }

    topology.logicalProcessorCount = static_cast<std::uint32_t>(topology.logicalProcessors.size());
    topology.physicalCoreCount = coreIndex;
    for (std::uint32_t core = 0; core < topology.physicalCoreCount; ++core) {
        const auto it = std::find_if(
            topology.logicalProcessors.begin(),
            topology.logicalProcessors.end(),
            [core](const LogicalProcessor& logical) { return logical.coreIndex == core; });
        if (it != topology.logicalProcessors.end() && it->efficiencyClass == maxEfficiencyClass) {
            ++topology.performanceCoreCount;
        }
    }
    topology.efficiencyCoreCount = topology.physicalCoreCount - topology.performanceCoreCount;
    return topology;
}

std::uint32_t recommended_worker_count(const CpuTopology& topology, std::uint32_t targetLogicalThreads) noexcept
{
    const std::uint32_t logical = std::max(1u, topology.logicalProcessorCount);
    return std::max(1u, std::min(logical, targetLogicalThreads));
}

std::wstring describe_cpu_topology(const CpuTopology& topology)
{
    std::wstringstream stream;
    stream << L"Logical processors: " << topology.logicalProcessorCount
           << L", physical cores: " << topology.physicalCoreCount
           << L", P-core class cores: " << topology.performanceCoreCount
           << L", E-core class cores: " << topology.efficiencyCoreCount
           << L", DEMO_WORKSTATION worker target: " << recommended_worker_count(topology, 20);
    return stream.str();
}

} // namespace talal::core
