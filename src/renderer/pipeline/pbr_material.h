#pragma once

#include "core/containers/slot_map.h"
#include "renderer/backend/descriptor_heap.h"
#include "renderer/backend/gpu_resource_manager.h"

namespace talal::renderer {

struct MaterialAsset {
    ResourceHandle albedoTex;
    ResourceHandle normalTex;
    ResourceHandle mraoTex;
    float baseColorR = 1.0f;
    float baseColorG = 1.0f;
    float baseColorB = 1.0f;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float emissiveStrength = 0.0f;
    DescriptorHandle firstSrv;
};

class MaterialRegistry {
public:
    using Handle = talal::core::SlotMapHandle;

    Handle CreateDefaultMaterial();
    Handle CreateMaterial(MaterialAsset asset);
    MaterialAsset* Get(Handle handle) noexcept;
    const MaterialAsset* Get(Handle handle) const noexcept;
    void BindMaterial(ID3D12GraphicsCommandList7* cmd, Handle handle, UINT rootParameterIndex) const noexcept;

private:
    talal::core::SlotMap<MaterialAsset> materials_;
};

} // namespace talal::renderer
