#pragma once

#include "renderer/backend/gpu_resource_manager.h"

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace talal::renderer {

struct FrameGraphResource {
    std::uint32_t id = UINT32_MAX;
    friend bool operator==(const FrameGraphResource&, const FrameGraphResource&) = default;
};

class FrameGraphPassBuilder {
public:
    FrameGraphPassBuilder& Read(FrameGraphResource resource);
    FrameGraphPassBuilder& Write(FrameGraphResource resource);
    FrameGraphPassBuilder& SideEffect() noexcept;

private:
    friend class FrameGraph;
    std::vector<FrameGraphResource> reads_;
    std::vector<FrameGraphResource> writes_;
    bool sideEffect_ = false;
};

class FrameGraph {
public:
    using ExecuteCallback = std::function<void(ID3D12GraphicsCommandList7&)>;

    FrameGraphResource DeclareTexture(std::wstring name, ResourceHandle physical, D3D12_RESOURCE_STATES initialState);
    FrameGraphResource DeclareBuffer(std::wstring name, ResourceHandle physical, D3D12_RESOURCE_STATES initialState);
    void AddPass(std::wstring name, FrameGraphPassBuilder builder, ExecuteCallback execute);
    void Clear();
    void Compile();
    void Execute(ID3D12GraphicsCommandList7& commandList, GPUResourceManager& resources);
    void MarkDirty() noexcept;
    bool Dirty() const noexcept { return dirty_; }

private:
    struct ResourceNode {
        std::wstring name;
        ResourceHandle physical;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        bool texture = false;
    };

    struct PassNode {
        std::wstring name;
        std::vector<FrameGraphResource> reads;
        std::vector<FrameGraphResource> writes;
        ExecuteCallback execute;
        bool sideEffect = false;
        bool culled = false;
    };

    D3D12_RESOURCE_STATES DesiredReadState(FrameGraphResource resource) const noexcept;
    D3D12_RESOURCE_STATES DesiredWriteState(FrameGraphResource resource) const noexcept;
    void TopologicalSort();
    void CullUnreferencedPasses();

    mutable std::mutex mutex_;
    std::vector<ResourceNode> resources_;
    std::vector<PassNode> passes_;
    std::vector<std::uint32_t> sortedPasses_;
    bool dirty_ = true;
};

} // namespace talal::renderer
