#pragma once

#include "renderer/backend/dx12_common.h"

#include <dxcapi.h>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace talal::renderer {

struct ShaderDefine {
    std::wstring name;
    std::wstring value;
};

struct ShaderCompileDesc {
    std::filesystem::path sourcePath;
    std::wstring entryPoint;
    std::wstring profile;
    std::vector<ShaderDefine> defines;
    bool debug = false;
};

struct ShaderBlob {
    std::vector<std::byte> bytes;
    std::filesystem::path cachePath;
    std::wstring diagnostics;
};

class ShaderCompiler {
public:
    bool Initialize(std::filesystem::path shaderRoot, std::filesystem::path cacheRoot);
    void Shutdown() noexcept;

    ShaderBlob Compile(const ShaderCompileDesc& desc);
    void StartHotReloadWatcher();
    void StopHotReloadWatcher() noexcept;
    std::uint64_t CacheGeneration() const noexcept { return cacheGeneration_.load(std::memory_order_acquire); }

private:
    std::vector<std::byte> ReadFileBytes(const std::filesystem::path& path) const;
    std::wstring CacheKey(const ShaderCompileDesc& desc, std::span<const std::byte> sourceBytes) const;
    std::filesystem::path CachePathFor(const std::wstring& cacheKey) const;
    ShaderBlob CompileUncached(const ShaderCompileDesc& desc, std::span<const std::byte> sourceBytes, const std::filesystem::path& cachePath);
    void WatcherThreadMain() noexcept;

    std::filesystem::path shaderRoot_;
    std::filesystem::path cacheRoot_;
    ComPtr<IDxcUtils> utils_;
    ComPtr<IDxcCompiler3> compiler_;
    ComPtr<IDxcIncludeHandler> includeHandler_;
    mutable std::mutex mutex_;
    std::thread watcher_;
    std::atomic<bool> stopWatcher_ = false;
    std::atomic<std::uint64_t> cacheGeneration_ = 0;
};

} // namespace talal::renderer
