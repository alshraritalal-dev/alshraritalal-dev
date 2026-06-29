#include "renderer/pipeline/pbr_material.h"

namespace talal::renderer {

MaterialRegistry::Handle MaterialRegistry::CreateDefaultMaterial()
{
    MaterialAsset material;
    material.baseColorR = 1.0f;
    material.baseColorG = 1.0f;
    material.baseColorB = 1.0f;
    material.metallic = 0.0f;
    material.roughness = 0.5f;
    material.emissiveStrength = 0.0f;
    return materials_.insert(material);
}

MaterialRegistry::Handle MaterialRegistry::CreateMaterial(MaterialAsset asset)
{
    return materials_.insert(asset);
}

MaterialAsset* MaterialRegistry::Get(Handle handle) noexcept
{
    return materials_.get(handle);
}

const MaterialAsset* MaterialRegistry::Get(Handle handle) const noexcept
{
    return materials_.get(handle);
}

void MaterialRegistry::BindMaterial(ID3D12GraphicsCommandList7* cmd, Handle handle, UINT rootParameterIndex) const noexcept
{
    const MaterialAsset* material = materials_.get(handle);
    if (!cmd || !material || !material->firstSrv.valid()) {
        return;
    }
    cmd->SetGraphicsRootDescriptorTable(rootParameterIndex, material->firstSrv.gpu);
}

} // namespace talal::renderer
