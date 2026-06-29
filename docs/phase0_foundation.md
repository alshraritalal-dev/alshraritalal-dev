# Phase 0 Foundation

## Scope

Phase 0 establishes the repository layout, dependency manifest, CMake policy, and local setup script for DEMO_WORKSTATION. It does not add engine runtime modules.

## Machine Policy

- Target host: DEMO_WORKSTATION
- CPU: Intel Core i7-12700F
- Compiler baseline: C++23, AVX2, 16 compile jobs
- Shader compile budget: 12 jobs
- Asset processing budget: 8 jobs
- GPU: RTX 4070 12 GB
- Storage: C: Kingston NVMe for source, build products, package cache, shader cache, and asset cache

## SDK Policy

Open-source and redistributable packages are declared in `vcpkg.json`. Vendor SDKs that require separate licenses or account downloads are kept under `vendor_sdks/` and are disabled by default through `TALAL_ENABLE_VENDOR_SDKS=OFF`.

## Build Profiles

| Profile | CMake build type | Compiler policy |
|---|---|---|
| Debug | Debug | Symbols, runtime checks, no optimization |
| Profile | RelWithDebInfo | Optimized with symbols and profiling definitions |
| Release | Release | AVX2, LTCG, reference folding, identical COMDAT folding |
