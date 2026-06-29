#include "renderer/frame_graph.h"

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace talal::renderer {

FrameGraphPassBuilder& FrameGraphPassBuilder::Read(FrameGraphResource resource)
{
    reads_.push_back(resource);
    return *this;
}

FrameGraphPassBuilder& FrameGraphPassBuilder::Write(FrameGraphResource resource)
{
    writes_.push_back(resource);
    return *this;
}

FrameGraphPassBuilder& FrameGraphPassBuilder::SideEffect() noexcept
{
    sideEffect_ = true;
    return *this;
}

FrameGraphResource FrameGraph::DeclareTexture(std::wstring name, ResourceHandle physical, D3D12_RESOURCE_STATES initialState)
{
    std::scoped_lock lock(mutex_);
    const std::uint32_t id = static_cast<std::uint32_t>(resources_.size());
    resources_.push_back(ResourceNode { std::move(name), physical, initialState, true });
    dirty_ = true;
    return FrameGraphResource { id };
}

FrameGraphResource FrameGraph::DeclareBuffer(std::wstring name, ResourceHandle physical, D3D12_RESOURCE_STATES initialState)
{
    std::scoped_lock lock(mutex_);
    const std::uint32_t id = static_cast<std::uint32_t>(resources_.size());
    resources_.push_back(ResourceNode { std::move(name), physical, initialState, false });
    dirty_ = true;
    return FrameGraphResource { id };
}

void FrameGraph::AddPass(std::wstring name, FrameGraphPassBuilder builder, ExecuteCallback execute)
{
    std::scoped_lock lock(mutex_);
    passes_.push_back(PassNode {
        std::move(name),
        std::move(builder.reads_),
        std::move(builder.writes_),
        std::move(execute),
        builder.sideEffect_,
        false
    });
    dirty_ = true;
}

void FrameGraph::Clear()
{
    std::scoped_lock lock(mutex_);
    resources_.clear();
    passes_.clear();
    sortedPasses_.clear();
    dirty_ = true;
}

void FrameGraph::Compile()
{
    std::scoped_lock lock(mutex_);
    CullUnreferencedPasses();
    TopologicalSort();
    dirty_ = false;
}

void FrameGraph::Execute(ID3D12GraphicsCommandList7& commandList, GPUResourceManager& resources)
{
    std::scoped_lock lock(mutex_);
    if (dirty_) {
        CullUnreferencedPasses();
        TopologicalSort();
        dirty_ = false;
    }

    for (std::uint32_t passIndex : sortedPasses_) {
        PassNode& pass = passes_[passIndex];
        if (pass.culled) {
            continue;
        }

        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        auto transition = [&](FrameGraphResource resource, D3D12_RESOURCE_STATES target) {
            if (resource.id >= resources_.size()) {
                return;
            }
            ResourceNode& node = resources_[resource.id];
            if (node.state == target) {
                return;
            }
            ID3D12Resource* physical = resources.Get(node.physical);
            if (!physical) {
                return;
            }
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = physical;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = node.state;
            barrier.Transition.StateAfter = target;
            barriers.push_back(barrier);
            node.state = target;
        };

        for (FrameGraphResource read : pass.reads) {
            transition(read, DesiredReadState(read));
        }
        for (FrameGraphResource write : pass.writes) {
            transition(write, DesiredWriteState(write));
        }
        if (!barriers.empty()) {
            commandList.ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        }
        pass.execute(commandList);
    }
}

void FrameGraph::MarkDirty() noexcept
{
    dirty_ = true;
}

D3D12_RESOURCE_STATES FrameGraph::DesiredReadState(FrameGraphResource resource) const noexcept
{
    if (resource.id >= resources_.size()) {
        return D3D12_RESOURCE_STATE_COMMON;
    }
    return resources_[resource.id].texture ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
}

D3D12_RESOURCE_STATES FrameGraph::DesiredWriteState(FrameGraphResource resource) const noexcept
{
    if (resource.id >= resources_.size()) {
        return D3D12_RESOURCE_STATE_COMMON;
    }
    const ResourceNode& node = resources_[resource.id];
    if (node.texture && node.name.find(L"Depth") != std::wstring::npos) {
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    return node.texture ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

void FrameGraph::TopologicalSort()
{
    sortedPasses_.clear();
    const std::size_t passCount = passes_.size();
    std::vector<std::vector<std::uint32_t>> edges(passCount);
    std::vector<std::uint32_t> indegree(passCount, 0);
    std::unordered_map<std::uint32_t, std::uint32_t> lastWriter;

    for (std::uint32_t i = 0; i < passCount; ++i) {
        if (passes_[i].culled) {
            continue;
        }
        auto addEdge = [&](std::uint32_t from, std::uint32_t to) {
            if (from == to) {
                return;
            }
            edges[from].push_back(to);
            ++indegree[to];
        };
        for (FrameGraphResource read : passes_[i].reads) {
            if (auto it = lastWriter.find(read.id); it != lastWriter.end()) {
                addEdge(it->second, i);
            }
        }
        for (FrameGraphResource write : passes_[i].writes) {
            if (auto it = lastWriter.find(write.id); it != lastWriter.end()) {
                addEdge(it->second, i);
            }
            lastWriter[write.id] = i;
        }
    }

    std::queue<std::uint32_t> ready;
    for (std::uint32_t i = 0; i < passCount; ++i) {
        if (!passes_[i].culled && indegree[i] == 0) {
            ready.push(i);
        }
    }

    while (!ready.empty()) {
        const std::uint32_t pass = ready.front();
        ready.pop();
        sortedPasses_.push_back(pass);
        for (std::uint32_t target : edges[pass]) {
            if (--indegree[target] == 0) {
                ready.push(target);
            }
        }
    }

    if (sortedPasses_.empty()) {
        for (std::uint32_t i = 0; i < passCount; ++i) {
            if (!passes_[i].culled) {
                sortedPasses_.push_back(i);
            }
        }
    }
}

void FrameGraph::CullUnreferencedPasses()
{
    std::unordered_set<std::uint32_t> referencedResources;
    for (const PassNode& pass : passes_) {
        for (FrameGraphResource read : pass.reads) {
            referencedResources.insert(read.id);
        }
    }

    for (PassNode& pass : passes_) {
        if (pass.sideEffect) {
            pass.culled = false;
            continue;
        }
        bool hasReferencedWrite = false;
        for (FrameGraphResource write : pass.writes) {
            hasReferencedWrite = hasReferencedWrite || referencedResources.contains(write.id);
        }
        pass.culled = !hasReferencedWrite;
    }
}

} // namespace talal::renderer
