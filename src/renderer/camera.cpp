#include "renderer/camera.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace talal::renderer {
namespace {

DirectX::XMVECTOR ForwardFromAngles(float yaw, float pitch) noexcept
{
    const float cp = std::cos(pitch);
    return DirectX::XMVector3Normalize(DirectX::XMVectorSet(std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp, 0.0f));
}

} // namespace

void Camera::SetPerspective(float aspectRatio, float fovDegrees, float nearPlane, float farPlane) noexcept
{
    aspectRatio_ = std::max(0.01f, aspectRatio);
    fovRadians_ = DirectX::XMConvertToRadians(fovDegrees);
    nearPlane_ = nearPlane;
    farPlane_ = farPlane;
    Rebuild();
}

void Camera::SetLookAt(DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 target, DirectX::XMFLOAT3 up) noexcept
{
    position_ = position;
    const DirectX::XMVECTOR eye = DirectX::XMLoadFloat3(&position);
    const DirectX::XMVECTOR at = DirectX::XMLoadFloat3(&target);
    const DirectX::XMVECTOR direction = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(at, eye));
    DirectX::XMFLOAT3 dir {};
    DirectX::XMStoreFloat3(&dir, direction);
    yaw_ = std::atan2(dir.x, dir.z);
    pitch_ = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
    (void)up;
    Rebuild();
}

void Camera::UpdateFlyControls(HWND hwnd, float deltaSeconds) noexcept
{
    if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0) {
        POINT cursor {};
        GetCursorPos(&cursor);
        ScreenToClient(hwnd, &cursor);
        static POINT last = cursor;
        const float dx = static_cast<float>(cursor.x - last.x);
        const float dy = static_cast<float>(cursor.y - last.y);
        yaw_ += dx * 0.0025f;
        pitch_ = std::clamp(pitch_ - dy * 0.0025f, -1.45f, 1.45f);
        last = cursor;
    }

    const DirectX::XMVECTOR forward = ForwardFromAngles(yaw_, pitch_);
    const DirectX::XMVECTOR right = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), forward));
    DirectX::XMVECTOR position = DirectX::XMLoadFloat3(&position_);
    const float speed = moveSpeed_ * deltaSeconds * ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 4.0f : 1.0f);
    if (GetAsyncKeyState('W') & 0x8000) {
        position = DirectX::XMVectorMultiplyAdd(forward, DirectX::XMVectorReplicate(speed), position);
    }
    if (GetAsyncKeyState('S') & 0x8000) {
        position = DirectX::XMVectorMultiplyAdd(forward, DirectX::XMVectorReplicate(-speed), position);
    }
    if (GetAsyncKeyState('A') & 0x8000) {
        position = DirectX::XMVectorMultiplyAdd(right, DirectX::XMVectorReplicate(-speed), position);
    }
    if (GetAsyncKeyState('D') & 0x8000) {
        position = DirectX::XMVectorMultiplyAdd(right, DirectX::XMVectorReplicate(speed), position);
    }
    DirectX::XMStoreFloat3(&position_, position);
    Rebuild();
}

CameraConstants Camera::Constants() const noexcept
{
    CameraConstants constants;
    constants.view = view_;
    constants.proj = proj_;
    constants.viewProj = viewProj_;
    constants.invViewProj = invViewProj_;
    constants.worldPos = position_;
    constants.nearPlane = nearPlane_;
    constants.farPlane = farPlane_;
    return constants;
}

D3D12_GPU_VIRTUAL_ADDRESS Camera::Upload(UploadRingBuffer& upload)
{
    void* mapped = upload.MapRegion(sizeof(CameraConstants), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    const CameraConstants constants = Constants();
    std::memcpy(mapped, &constants, sizeof(CameraConstants));
    return upload.GPUAddress(mapped);
}

void Camera::Rebuild() noexcept
{
    const DirectX::XMVECTOR eye = DirectX::XMLoadFloat3(&position_);
    const DirectX::XMVECTOR forward = ForwardFromAngles(yaw_, pitch_);
    const DirectX::XMVECTOR at = DirectX::XMVectorAdd(eye, forward);
    const DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eye, at, up);
    const DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(fovRadians_, aspectRatio_, nearPlane_, farPlane_);
    const DirectX::XMMATRIX viewProj = DirectX::XMMatrixMultiply(view, proj);
    const DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(nullptr, viewProj);
    DirectX::XMStoreFloat4x4(&view_, DirectX::XMMatrixTranspose(view));
    DirectX::XMStoreFloat4x4(&proj_, DirectX::XMMatrixTranspose(proj));
    DirectX::XMStoreFloat4x4(&viewProj_, DirectX::XMMatrixTranspose(viewProj));
    DirectX::XMStoreFloat4x4(&invViewProj_, DirectX::XMMatrixTranspose(invViewProj));
}

} // namespace talal::renderer
