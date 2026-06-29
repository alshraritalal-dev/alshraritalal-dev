#pragma once

#include "renderer/backend/upload_ring_buffer.h"

#include <DirectXMath.h>

namespace talal::renderer {

struct CameraConstants {
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 proj;
    DirectX::XMFLOAT4X4 viewProj;
    DirectX::XMFLOAT4X4 invViewProj;
    DirectX::XMFLOAT3 worldPos;
    float nearPlane = 0.1f;
    float farPlane = 10000.0f;
    float pad[3] {};
};

class Camera {
public:
    void SetPerspective(float aspectRatio, float fovDegrees = 75.0f, float nearPlane = 0.1f, float farPlane = 10000.0f) noexcept;
    void SetLookAt(DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 target, DirectX::XMFLOAT3 up = { 0.0f, 1.0f, 0.0f }) noexcept;
    void UpdateFlyControls(HWND hwnd, float deltaSeconds) noexcept;
    CameraConstants Constants() const noexcept;
    D3D12_GPU_VIRTUAL_ADDRESS Upload(UploadRingBuffer& upload);

private:
    void Rebuild() noexcept;

    DirectX::XMFLOAT3 position_ { 0.0f, 2.0f, -5.0f };
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float fovRadians_ = DirectX::XMConvertToRadians(75.0f);
    float aspectRatio_ = 16.0f / 9.0f;
    float nearPlane_ = 0.1f;
    float farPlane_ = 10000.0f;
    float moveSpeed_ = 5.0f;
    DirectX::XMFLOAT4X4 view_ {};
    DirectX::XMFLOAT4X4 proj_ {};
    DirectX::XMFLOAT4X4 viewProj_ {};
    DirectX::XMFLOAT4X4 invViewProj_ {};
};

} // namespace talal::renderer
