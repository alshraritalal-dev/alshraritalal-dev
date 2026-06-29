#include "tools/shader_compiler.h"

#include <dxcapi.h>
#include <bcrypt.h>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <array>
#include <cstring>

#pragma comment(lib, "bcrypt.lib")

namespace talal::renderer {
namespace {

std::wstring BytesToHex(std::span<const std::byte> bytes)
{
    std::wstringstream stream;
    stream << std::hex << std::setfill(L'0');
    for (std::byte byte : bytes) {
        stream << std::setw(2) << static_cast<unsigned int>(std::to_integer<unsigned char>(byte));
    }
    return stream.str();
}

std::vector<std::byte> Sha256(std::span<const std::byte> bytes)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD dataLength = 0;
    std::vector<std::byte> object;
    std::vector<std::byte> digest(32);

    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) {
        HR_CHECK(HRESULT_FROM_NT(status), L"BCryptOpenAlgorithmProvider SHA256");
    }
    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &dataLength, 0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        HR_CHECK(HRESULT_FROM_NT(status), L"BCryptGetProperty BCRYPT_OBJECT_LENGTH");
    }
    object.resize(objectLength);
    status = BCryptCreateHash(algorithm, &hash, reinterpret_cast<PUCHAR>(object.data()), objectLength, nullptr, 0, 0);
    if (status < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        HR_CHECK(HRESULT_FROM_NT(status), L"BCryptCreateHash");
    }
    status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())), static_cast<ULONG>(bytes.size()), 0);
    if (status < 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        HR_CHECK(HRESULT_FROM_NT(status), L"BCryptHashData");
    }
    status = BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size()), 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) {
        HR_CHECK(HRESULT_FROM_NT(status), L"BCryptFinishHash");
    }
    return digest;
}

std::wstring JoinDefines(const std::vector<ShaderDefine>& defines)
{
    std::wstring joined;
    for (const ShaderDefine& define : defines) {
        joined.append(define.name);
        joined.push_back(L'=');
        joined.append(define.value);
        joined.push_back(L';');
    }
    return joined;
}

} // namespace

bool ShaderCompiler::Initialize(std::filesystem::path shaderRoot, std::filesystem::path cacheRoot)
{
    try {
        shaderRoot_ = std::move(shaderRoot);
        cacheRoot_ = std::move(cacheRoot);
        std::filesystem::create_directories(cacheRoot_);
        HR_CHECK(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils_)), L"DxcCreateInstance DxcUtils");
        HR_CHECK(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler_)), L"DxcCreateInstance DxcCompiler");
        HR_CHECK(utils_->CreateDefaultIncludeHandler(&includeHandler_), L"IDxcUtils::CreateDefaultIncludeHandler");
        return true;
    } catch (const Dx12Exception&) {
        Shutdown();
        return false;
    }
}

void ShaderCompiler::Shutdown() noexcept
{
    StopHotReloadWatcher();
    includeHandler_.Reset();
    compiler_.Reset();
    utils_.Reset();
}

ShaderBlob ShaderCompiler::Compile(const ShaderCompileDesc& desc)
{
    std::scoped_lock lock(mutex_);
    const std::vector<std::byte> source = ReadFileBytes(desc.sourcePath);
    const std::wstring key = CacheKey(desc, source);
    const std::filesystem::path cachePath = CachePathFor(key);
    if (std::filesystem::exists(cachePath)) {
        ShaderBlob blob;
        blob.cachePath = cachePath;
        blob.bytes = ReadFileBytes(cachePath);
        return blob;
    }
    return CompileUncached(desc, source, cachePath);
}

void ShaderCompiler::StartHotReloadWatcher()
{
    if (watcher_.joinable()) {
        return;
    }
    stopWatcher_.store(false, std::memory_order_release);
    watcher_ = std::thread([this] { WatcherThreadMain(); });
}

void ShaderCompiler::StopHotReloadWatcher() noexcept
{
    stopWatcher_.store(true, std::memory_order_release);
    if (watcher_.joinable()) {
        CancelSynchronousIo(watcher_.native_handle());
        watcher_.join();
    }
}

std::vector<std::byte> ShaderCompiler::ReadFileBytes(const std::filesystem::path& path) const
{
    std::filesystem::path resolved = path;
    if (resolved.is_relative() && !std::filesystem::exists(resolved)) {
        const std::filesystem::path byFilename = shaderRoot_ / resolved.filename();
        if (std::filesystem::exists(byFilename)) {
            resolved = byFilename;
        }
    }
    std::ifstream file(resolved, std::ios::binary);
    if (!file) {
        HR_CHECK(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), L"Open shader file");
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(static_cast<std::size_t>(std::max<std::streamoff>(0, size)));
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return bytes;
}

std::wstring ShaderCompiler::CacheKey(const ShaderCompileDesc& desc, std::span<const std::byte> sourceBytes) const
{
    std::vector<std::byte> material;
    auto appendWide = [&material](std::wstring_view text) {
        const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
        material.insert(material.end(), bytes, bytes + text.size() * sizeof(wchar_t));
    };
    material.insert(material.end(), sourceBytes.begin(), sourceBytes.end());
    appendWide(desc.sourcePath.wstring());
    appendWide(desc.entryPoint);
    appendWide(desc.profile);
    appendWide(JoinDefines(desc.defines));
    appendWide(desc.debug ? L"debug" : L"release");
    return BytesToHex(Sha256(material));
}

std::filesystem::path ShaderCompiler::CachePathFor(const std::wstring& cacheKey) const
{
    return cacheRoot_ / (cacheKey + L".dxil");
}

ShaderBlob ShaderCompiler::CompileUncached(const ShaderCompileDesc& desc, std::span<const std::byte> sourceBytes, const std::filesystem::path& cachePath)
{
    ShaderBlob blob;
    blob.cachePath = cachePath;

    DxcBuffer sourceBuffer = {};
    sourceBuffer.Ptr = sourceBytes.data();
    sourceBuffer.Size = sourceBytes.size();
    sourceBuffer.Encoding = DXC_CP_UTF8;

    std::vector<std::wstring> ownedArgs;
    ownedArgs.push_back(desc.sourcePath.wstring());
    ownedArgs.push_back(L"-E");
    ownedArgs.push_back(desc.entryPoint);
    ownedArgs.push_back(L"-T");
    ownedArgs.push_back(desc.profile);
    ownedArgs.push_back(L"-I");
    ownedArgs.push_back(shaderRoot_.wstring());
    if (desc.debug) {
        ownedArgs.push_back(L"-Zi");
        ownedArgs.push_back(L"-Od");
        ownedArgs.push_back(L"-Qembed_debug");
    } else {
        ownedArgs.push_back(L"-O3");
        ownedArgs.push_back(L"-Qstrip_debug");
        ownedArgs.push_back(L"-Qstrip_reflect");
    }
    for (const ShaderDefine& define : desc.defines) {
        ownedArgs.push_back(L"-D");
        ownedArgs.push_back(define.name + L"=" + define.value);
    }

    std::vector<LPCWSTR> args;
    args.reserve(ownedArgs.size());
    for (const std::wstring& arg : ownedArgs) {
        args.push_back(arg.c_str());
    }

    ComPtr<IDxcResult> result;
    HR_CHECK(compiler_->Compile(&sourceBuffer, args.data(), static_cast<UINT32>(args.size()), includeHandler_.Get(), IID_PPV_ARGS(&result)), L"IDxcCompiler3::Compile");

    ComPtr<IDxcBlobUtf8> errors;
    HRESULT errorHr = result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (SUCCEEDED(errorHr) && errors && errors->GetStringLength() > 0) {
        blob.diagnostics = Widen({ errors->GetStringPointer(), errors->GetStringLength() });
        LogInfo(blob.diagnostics);
    }

    HRESULT status = S_OK;
    HR_CHECK(result->GetStatus(&status), L"IDxcResult::GetStatus");
    HR_CHECK(status, L"DXC shader compilation");

    ComPtr<IDxcBlob> object;
    HR_CHECK(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr), L"IDxcResult::GetOutput DXC_OUT_OBJECT");
    blob.bytes.resize(object->GetBufferSize());
    std::memcpy(blob.bytes.data(), object->GetBufferPointer(), object->GetBufferSize());

    std::filesystem::create_directories(cachePath.parent_path());
    std::ofstream cacheFile(cachePath, std::ios::binary | std::ios::trunc);
    cacheFile.write(reinterpret_cast<const char*>(blob.bytes.data()), static_cast<std::streamsize>(blob.bytes.size()));
    return blob;
}

void ShaderCompiler::WatcherThreadMain() noexcept
{
    SetThreadDescription(GetCurrentThread(), L"TalalShaderHotReload");
    HANDLE directory = CreateFileW(
        shaderRoot_.wstring().c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
        LogError(L"CreateFile shader hot reload directory", HRESULT_FROM_WIN32(GetLastError()));
        return;
    }

    std::array<std::byte, 4096> buffer {};
    while (!stopWatcher_.load(std::memory_order_acquire)) {
        DWORD bytesReturned = 0;
        const BOOL ok = ReadDirectoryChangesW(
            directory,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            TRUE,
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
            &bytesReturned,
            nullptr,
            nullptr);
        if (!ok) {
            LogError(L"ReadDirectoryChangesW shader hot reload", HRESULT_FROM_WIN32(GetLastError()));
            break;
        }
        if (bytesReturned > 0) {
            cacheGeneration_.fetch_add(1, std::memory_order_acq_rel);
        }
    }
    CloseHandle(directory);
}

} // namespace talal::renderer
