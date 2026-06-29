#Requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Profile', 'Release')]
    [string]$BuildType = 'Debug',

    [ValidateSet('Ninja')]
    [string]$Generator = 'Ninja',

    [string]$Triplet = 'x64-windows',
    [int]$CompileJobs = 16,
    [int]$ShaderJobs = 12,
    [int]$AssetJobs = 8,
    [string]$VcpkgRoot = '',
    [switch]$BootstrapVcpkg,
    [switch]$InstallTools,
    [switch]$SkipConfigure,
    [switch]$Build,
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ProjectState = Join-Path $RepoRoot 'project_core_state'
$CompilerFaultLog = Join-Path $ProjectState 'compiler_fault.log'

function Write-Step {
    param([string]$Message)
    Write-Host "[phase0] $Message"
}

function Ensure-DirectoryLayout {
    $dirs = @(
        'project_core_state',
        'cmake',
        'scripts',
        'docs',
        'apps',
        'tools',
        'tests',
        'third_party',
        'vendor_sdks',
        '.deps',
        '.cache',
        '.cache/vcpkg',
        'out',
        'out/build',
        'out/install',
        'src/core/memory',
        'src/core/threading',
        'src/core/platform',
        'src/core/math',
        'src/core/containers',
        'src/renderer/backend',
        'src/renderer/pipeline',
        'src/renderer/postprocess',
        'src/renderer/culling',
        'src/world/ecs',
        'src/world/streaming',
        'src/world/map',
        'src/world/time_weather',
        'src/physics/collision',
        'src/physics/dynamics',
        'src/physics/ragdoll',
        'src/animation/skeleton',
        'src/animation/state_machine',
        'src/animation/motion_matching',
        'src/ai/navigation',
        'src/ai/behavior',
        'src/ai/npc_types',
        'src/ai/traffic',
        'src/gameplay/character',
        'src/gameplay/combat',
        'src/gameplay/vehicles',
        'src/gameplay/missions/scripts/main_story',
        'src/gameplay/missions/scripts/side_missions',
        'src/gameplay/missions/scripts/ambient_events',
        'src/gameplay/economy',
        'src/gameplay/social',
        'src/audio/engine',
        'src/audio/sfx',
        'src/audio/music',
        'src/network/transport',
        'src/network/protocol',
        'src/network/session',
        'src/narrative/characters',
        'src/narrative/story_acts',
        'src/ui/hud',
        'src/ui/menus',
        'src/ui/framework',
        'shaders/hlsl',
        'shaders/glsl',
        'assets/meshes',
        'assets/textures',
        'assets/audio',
        'assets/animations',
        'assets/fonts',
        'config'
    )

    foreach ($dir in $dirs) {
        New-Item -ItemType Directory -Path (Join-Path $RepoRoot $dir) -Force | Out-Null
    }
}

function Get-CommandPath {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        return $null
    }
    return $cmd.Source
}

function Invoke-UserScopeWingetInstall {
    param(
        [string]$PackageId,
        [string]$CommandName
    )

    if (-not $InstallTools) {
        throw "Missing $CommandName. Install it or re-run with -InstallTools."
    }

    $winget = Get-CommandPath 'winget'
    if ($null -eq $winget) {
        throw "winget is not available, so $PackageId cannot be installed automatically."
    }

    Write-Step "Installing $PackageId through winget"
    & $winget install --id $PackageId --scope user --silent --accept-source-agreements --accept-package-agreements
    if ($LASTEXITCODE -ne 0) {
        throw "winget failed while installing $PackageId."
    }
}

function Find-VsDevCmd {
    $programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        return $null
    }

    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installPath)) {
        return $null
    }

    $devCmd = Join-Path $installPath 'Common7\Tools\VsDevCmd.bat'
    if (Test-Path -LiteralPath $devCmd) {
        return $devCmd
    }
    return $null
}

function Import-VsDevEnvironment {
    if ((Get-CommandPath 'cl.exe') -and (Get-CommandPath 'rc.exe') -and (Get-CommandPath 'mt.exe')) {
        return
    }

    $devCmd = Find-VsDevCmd
    if ($null -eq $devCmd) {
        throw "MSVC was not found. Install Visual Studio 2022 Build Tools with the Desktop development with C++ workload."
    }

    Write-Step "Importing Visual Studio x64 developer environment"
    $envDump = cmd /s /c "`"$devCmd`" -arch=x64 -host_arch=x64 && set"
    if ($LASTEXITCODE -ne 0) {
        throw "VsDevCmd failed to initialize the x64 compiler environment."
    }

    foreach ($line in $envDump) {
        $split = $line.IndexOf('=')
        if ($split -gt 0) {
            $name = $line.Substring(0, $split)
            $value = $line.Substring($split + 1)
            [Environment]::SetEnvironmentVariable($name, $value, 'Process')
        }
    }
}

function Assert-MachineProfile {
    Write-Step "Checking DEMO_WORKSTATION hardware profile"
    $system = Get-CimInstance Win32_ComputerSystem
    $cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
    $gpu = Get-CimInstance Win32_VideoController | Where-Object { $_.Name -like '*RTX 4070*' } | Select-Object -First 1
    $memoryGb = [math]::Round($system.TotalPhysicalMemory / 1GB, 2)
    $nvme = Get-PhysicalDisk | Where-Object { $_.BusType -eq 'NVMe' -and $_.FriendlyName -like '*KINGSTON*' } | Select-Object -First 1

    if ($env:COMPUTERNAME -ne 'DEMO_WORKSTATION') {
        Write-Warning "Computer name is $env:COMPUTERNAME, expected DEMO_WORKSTATION."
    }
    if ($cpu.Name -notlike '*i7-12700F*') {
        Write-Warning "CPU is $($cpu.Name), expected Intel Core i7-12700F."
    }
    if ($null -eq $gpu) {
        Write-Warning "RTX 4070 adapter was not found through WMI."
    }
    if ($memoryGb -lt 15.0) {
        Write-Warning "Visible RAM is $memoryGb GB. Phase 0 policy expects about 16 GB."
    }
    if ($null -eq $nvme) {
        Write-Warning "Kingston NVMe disk was not found through Get-PhysicalDisk."
    }

    Write-Step "Machine=$env:COMPUTERNAME CPU=$($cpu.Name) RAM=${memoryGb}GB"
}

function Ensure-ToolchainCommands {
    $git = Get-CommandPath 'git.exe'
    if ($null -eq $git) {
        Invoke-UserScopeWingetInstall -PackageId 'Git.Git' -CommandName 'git.exe'
    }

    $cmake = Get-CommandPath 'cmake.exe'
    if ($null -eq $cmake) {
        Invoke-UserScopeWingetInstall -PackageId 'Kitware.CMake' -CommandName 'cmake.exe'
    }

    $ninja = Get-CommandPath 'ninja.exe'
    if ($null -eq $ninja) {
        Invoke-UserScopeWingetInstall -PackageId 'Ninja-build.Ninja' -CommandName 'ninja.exe'
    }

    Import-VsDevEnvironment

    foreach ($tool in @('git.exe', 'cmake.exe', 'ninja.exe', 'cl.exe', 'rc.exe', 'mt.exe')) {
        if ($null -eq (Get-CommandPath $tool)) {
            throw "$tool is still unavailable after setup checks."
        }
    }
}

function Ensure-Vcpkg {
    if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
        $script:VcpkgRoot = Join-Path $RepoRoot '.deps\vcpkg'
    }

    $vcpkgExe = Join-Path $VcpkgRoot 'vcpkg.exe'
    if (-not (Test-Path -LiteralPath $vcpkgExe)) {
        if (-not $BootstrapVcpkg) {
            throw "vcpkg is missing at $VcpkgRoot. Re-run with -BootstrapVcpkg or pass -VcpkgRoot."
        }

        if (-not (Test-Path -LiteralPath $VcpkgRoot)) {
            Write-Step "Cloning vcpkg into $VcpkgRoot"
            & git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
            if ($LASTEXITCODE -ne 0) {
                throw "git clone for vcpkg failed."
            }
        }

        Write-Step "Bootstrapping vcpkg"
        & (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
        if ($LASTEXITCODE -ne 0) {
            throw "vcpkg bootstrap failed."
        }
    }

    [Environment]::SetEnvironmentVariable('VCPKG_ROOT', $VcpkgRoot, 'Process')
    [Environment]::SetEnvironmentVariable('VCPKG_FEATURE_FLAGS', 'manifests,binarycaching', 'Process')
    [Environment]::SetEnvironmentVariable('VCPKG_BINARY_SOURCES', "clear;files,$(Join-Path $RepoRoot '.cache\vcpkg'),readwrite", 'Process')
    return $vcpkgExe
}

function Convert-BuildTypeToCMake {
    switch ($BuildType) {
        'Debug' { return 'Debug' }
        'Profile' { return 'RelWithDebInfo' }
        'Release' { return 'Release' }
    }
}

function Invoke-CMakeConfigure {
    $cmakeBuildType = Convert-BuildTypeToCMake
    $presetName = "demo-workstation-$($BuildType.ToLowerInvariant())"
    $buildDir = Join-Path $RepoRoot "out\build\$presetName"
    $toolchain = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
    $vcpkgInstalledDir = Join-Path $VcpkgRoot "installed-$presetName"
    $rcCompiler = Get-Command 'rc.exe' -ErrorAction Stop
    $manifestTool = Get-Command 'mt.exe' -ErrorAction Stop

    $args = @(
        '-S', $RepoRoot,
        '-B', $buildDir,
        '-G', $Generator,
        "-DCMAKE_MAKE_PROGRAM=$((Get-Command 'ninja.exe' -ErrorAction SilentlyContinue).Source)",
        "-DCMAKE_RC_COMPILER=$($rcCompiler.Source)",
        "-DCMAKE_MT=$($manifestTool.Source)",
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        "-DVCPKG_TARGET_TRIPLET=$Triplet",
        "-DVCPKG_INSTALLED_DIR=$vcpkgInstalledDir",
        "-DCMAKE_BUILD_TYPE=$cmakeBuildType",
        '-DTALAL_TARGET_MACHINE=DEMO_WORKSTATION',
        '-DTALAL_CPU_BASELINE=AVX2',
        "-DTALAL_LOCAL_COMPILE_JOBS=$CompileJobs",
        "-DTALAL_LOCAL_SHADER_JOBS=$ShaderJobs",
        "-DTALAL_LOCAL_ASSET_JOBS=$AssetJobs",
        '-DTALAL_REQUIRE_DEPENDENCIES=OFF',
        '-DTALAL_ENABLE_VENDOR_SDKS=OFF'
    )

    Write-Step "Configuring CMake: $presetName"
    & cmake @args
    if ($LASTEXITCODE -ne 0) {
        Add-Content -LiteralPath $CompilerFaultLog -Value "CMake configure failed for $presetName at $(Get-Date -Format o)."
        throw "CMake configure failed. See $CompilerFaultLog."
    }

    if ($Build) {
        Write-Step "Building $presetName"
        & cmake --build $buildDir --parallel $CompileJobs
        if ($LASTEXITCODE -ne 0) {
            Add-Content -LiteralPath $CompilerFaultLog -Value "CMake build failed for $presetName at $(Get-Date -Format o)."
            throw "CMake build failed. See $CompilerFaultLog."
        }
    }
}

function Write-StateHash {
    Write-Step "Updating state_hash.bin"
    $rootFull = (Resolve-Path $RepoRoot).Path.TrimEnd('\', '/')
    $hashInput = New-Object System.Text.StringBuilder

    $files = Get-ChildItem -LiteralPath $RepoRoot -File -Recurse -Force | Where-Object {
        $relative = $_.FullName.Substring($rootFull.Length).TrimStart('\', '/') -replace '\\', '/'
        -not (
            $relative -like '.git/*' -or
            $relative -like '.deps/*' -or
            $relative -like '.cache/*' -or
            $relative -like 'out/*' -or
            $relative -like 'build/debug/*' -or
            $relative -like 'build/release/*' -or
            $relative -like 'build/tools/*' -or
            $relative -like 'project_core_state/setup_full_*.log' -or
            $relative -like 'project_core_state/install_build_tools*.log' -or
            $relative -like 'project_core_state/*.transcript.txt' -or
            $relative -eq 'project_core_state/state_hash.bin'
        )
    } | Sort-Object FullName

    foreach ($file in $files) {
        $relative = $file.FullName.Substring($rootFull.Length).TrimStart('\', '/') -replace '\\', '/'
        $fileHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        [void]$hashInput.AppendLine("$relative=$fileHash")
    }

    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($hashInput.ToString())
        $digest = $sha.ComputeHash($bytes)
        [System.IO.File]::WriteAllBytes((Join-Path $ProjectState 'state_hash.bin'), $digest)
    }
    finally {
        $sha.Dispose()
    }
}

Ensure-DirectoryLayout
Assert-MachineProfile
Write-StateHash

if ($ValidateOnly) {
    Write-Step "Validation complete. Configure step skipped by -ValidateOnly."
    return
}

Ensure-ToolchainCommands
$null = Ensure-Vcpkg

if (-not $SkipConfigure) {
    Invoke-CMakeConfigure
}
else {
    Write-Step "Configure step skipped by -SkipConfigure."
}
