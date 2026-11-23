<#
.SYNOPSIS
  One-command Windows x64 build of TGeshka: configure + Release build + portable zip in out\.

.DESCRIPTION
  Layout (as upstream docs/building-win.md expects):
    <BuildPath>\tdesktop    - this repository (any folder name)
    <BuildPath>\Libraries   - prepared dependencies (prepare.py)
    <BuildPath>\ThirdParty  - build tools (msys64, jom, python venv, rust)

  API credentials are read from %USERPROFILE%\.config\tg-fork\api.txt:
    api_id=<digits>
    api_hash=<hex>
  They go to CMake through an initial-cache file in out\ (never into the repo, never printed).
  -TestApi builds with the upstream test credentials instead (development only).

  The default gateway address is not in the sources. It comes from -GatewayUrl, the
  TGESHKA_GATEWAY_URL environment variable or %USERPROFILE%\.config\tg-fork\gateway.txt
  (one line, wss://...) and goes to CMake as TGESHKA_GATEWAY_URL. Without it the application
  asks for the gateway address or a tgeshka://gateway link on the first launch.

  -Version (or the -Release tag without "v") is the TGeshka version, 1.2.3 or 1.2.3-beta.1: it goes
  to CMake as TGESHKA_VERSION (over the default in tgeshka_brand.h), into the zip as
  tgeshka-version.txt and into the zip name; the zip is then the update package (docs/updates.md).

  Autoupdate and crash reports are always disabled (DESKTOP_APP_DISABLE_AUTOUPDATE / _CRASH_REPORTS):
  the fork must not talk to Telegram Desktop update/crash servers directly.

  Only one build runs at a time: <BuildPath>\build.lock and an optional lock shared between
  build trees (-SharedLock, default %TGESHKA_BUILD_LOCK%) exist only while a build holds them.

.EXAMPLE
  .\build.ps1                 # incremental build + zip
  .\build.ps1 -Prepare        # (re)build dependencies first (first run, or after an upstream update)
  .\build.ps1 -Ref origin/main                    # checkout a ref (clean tree required) and build
  .\build.ps1 -Release v0.1.1                     # build, then publish zip as a GitHub Release with this tag
  .\build.ps1 -Version 0.1.2-beta                 # build with this TGeshka version
  .\build.ps1 -Generator Ninja -CompilerLauncher sccache -NoPch -NoDebugInfo   # CI compile cache

  -Generator Ninja builds with Ninja Multi-Config instead of Visual Studio/MSBuild (one -Jobs
  pool for all targets). -CompilerLauncher (sccache/ccache) needs Ninja; a cached compile cannot
  use a precompiled header (/Yu), hence -NoPch, and -NoDebugInfo keeps the cached objects small.
#>
[CmdletBinding()]
param(
    [switch]$Prepare,
    [switch]$PrepareOnly,
    [switch]$Reconfigure,
    [switch]$TestApi,
    [switch]$NoZip,
    [string]$Ref,
    [string]$Release,
    [string]$Version,
    [int]$Jobs = [Environment]::ProcessorCount,
    [string]$ApiFile = (Join-Path $env:USERPROFILE '.config\tg-fork\api.txt'),
    [string]$GatewayUrl = $env:TGESHKA_GATEWAY_URL,
    [string]$GatewayFile = (Join-Path $env:USERPROFILE '.config\tg-fork\gateway.txt'),
    [string]$SharedLock = $env:TGESHKA_BUILD_LOCK,
    [int]$LockWaitMinutes = 240,
    [int]$MinFreeGB = 20,
    [ValidateSet('VS', 'Ninja')]
    [string]$Generator = 'VS',
    [string]$CompilerLauncher,
    [switch]$NoPch,
    [switch]$NoDebugInfo
)

$ErrorActionPreference = 'Stop'
$Repo = $PSScriptRoot
$Root = Split-Path $Repo -Parent
$Out = Join-Path $Repo 'out'
$Started = Get-Date

function Step($text) { Write-Host ("[{0:HH:mm:ss}] {1}" -f (Get-Date), $text) -ForegroundColor Cyan }
function Fail($text) { Write-Host "[ERROR] $text" -ForegroundColor Red; exit 1 }
function Invoke-Native([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { Fail "$exe exited with code $LASTEXITCODE" }
}

# --- locks ------------------------------------------------------------------
# A lock is a file that exists only while a build holds it (created exclusively,
# deleted on close, also when the process is killed). Both the build-tree lock and
# the lock shared between agents (-SharedLock) are taken.
$lockPaths = @((Join-Path $Root 'build.lock'))
if ($SharedLock -and (Test-Path (Split-Path $SharedLock -Parent))) { $lockPaths += $SharedLock }
$locks = @()
function Release-Locks { foreach ($l in $script:locks) { $l.Dispose() }; $script:locks = @() }
$deadline = (Get-Date).AddMinutes($LockWaitMinutes)
foreach ($lockPath in $lockPaths) {
    while ($true) {
        try {
            $l = New-Object IO.FileStream($lockPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite,
                [IO.FileShare]::Read, 4096, [IO.FileOptions]::DeleteOnClose)
            $info = [Text.Encoding]::UTF8.GetBytes("pid=$PID`nhost=$env:COMPUTERNAME`nrepo=$Repo`nstarted=$(Get-Date -Format s)`n")
            $l.Write($info, 0, $info.Length); $l.Flush()
            $locks += $l
            break
        } catch {
            if ((Get-Date) -gt $deadline) { Release-Locks; Fail "build lock $lockPath is held by another build" }
            Write-Host "Waiting for build lock $lockPath ..."
            Start-Sleep -Seconds 30
        }
    }
}

try {
    # --- Visual Studio environment (x64, toolset 14.44 as upstream requires) ---
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsPath = & $vswhere -latest -products * -version '[17.0,19.0)' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { Fail 'Visual Studio 2022/2026 with C++ x64 tools not found' }
    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    $toolset = if ($env:TG_VCVARS_VER) { $env:TG_VCVARS_VER } else { '14.44' }
    Step "MSVC environment: $vsPath (toolset $toolset)"
    $envDump = cmd /c "`"$vcvars`" -vcvars_ver=$toolset >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0) { Fail "vcvars64.bat -vcvars_ver=$toolset failed" }
    foreach ($line in $envDump) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2]) }
    }
    if ($env:Platform -ne 'x64') { Fail 'vcvars did not set Platform=x64' }
    $env:NUMBER_OF_PROCESSORS = "$Jobs"
    # Reused MSBuild nodes outlive the build and keep its log/console handles open.
    $env:MSBUILDDISABLENODEREUSE = '1'

    # --- optional checkout ----------------------------------------------------
    if ($Ref) {
        Push-Location $Repo
        if (git status --porcelain --untracked-files=no) { Fail 'working tree has changes, refusing to checkout' }
        Step "Checkout $Ref"
        Invoke-Native git @('fetch', '--no-tags', 'origin')
        Invoke-Native git @('checkout', '--detach', $Ref)
        Invoke-Native git @('submodule', 'update', '--init', '--recursive', '--jobs', '8')
        Pop-Location
    }

    # --- dependencies -----------------------------------------------------------
    New-Item -ItemType Directory -Force (Join-Path $Root 'Libraries'), (Join-Path $Root 'ThirdParty') | Out-Null
    $libsReady = Test-Path (Join-Path $Root 'Libraries\win64\cache_keys\tdesktop_rust')
    if ($Prepare -or $PrepareOnly -or -not $libsReady) {
        Step "Preparing dependencies in $Root\Libraries (jobs: $Jobs)"
        $t = Get-Date
        Push-Location $Root
        # prepare.py calls Windows PowerShell 5.1 (Expand-Archive); an inherited PowerShell 7
        # PSModulePath makes it load incompatible modules, so hide it from child processes.
        $psModulePath = $env:PSModulePath
        $env:PSModulePath = $null
        try {
            # FFmpeg configure detects MSVC by an English banner (VSLANG=1033). Without the English
            # language pack of Visual Studio the banner is localized, so relax that check.
            # Crash reports are disabled, so breakpad/crashpad (which also need the ATL component of
            # Visual Studio) are not built: run a generated copy of prepare.py without those stages.
            $prepareDir = Join-Path $Repo 'Telegram\build\prepare'
            $prepareSrc = [IO.File]::ReadAllText((Join-Path $prepareDir 'prepare.py'))
            $prepareGw = $prepareSrc.Replace("stages = []", "stages = []`ndef skip_stage(*args, **kwargs):`n    pass")
            foreach ($skip in 'breakpad', 'crashpad') {
                if (-not $prepareGw.Contains("stage('$skip',")) { Fail "prepare.py: stage $skip not found" }
                $prepareGw = $prepareGw.Replace("stage('$skip',", "skip_stage('$skip',")
            }
            $preparePy = Join-Path $prepareDir 'prepare_gw.py'
            [IO.File]::WriteAllText($preparePy, $prepareGw)

            $ffmpegScript = Join-Path $Root 'Libraries\win64\patches\build_ffmpeg_win.sh'
            if (-not (Test-Path $ffmpegScript)) {
                Invoke-Native python @($preparePy, 'silent', 'patches')
            }
            $script = [IO.File]::ReadAllText($ffmpegScript)
            $marker = '# tgeshka: localized MSVC banner'
            if (-not $script.Contains($marker)) {
                $sed = "sed -i 's/grep -q ^Microsoft/grep -q Microsoft/g; s/grep ^Microsoft/grep Microsoft/g' configure"
                $script = [regex]::Replace($script, '(cd \$FullScriptPath/\.\./ffmpeg)(\r?\n)',
                    { param($m) $m.Groups[1].Value + $m.Groups[2].Value + $marker + $m.Groups[2].Value + $sed + $m.Groups[2].Value })
                if (-not $script.Contains($marker)) { Fail "cannot patch $ffmpegScript" }
                [IO.File]::WriteAllText($ffmpegScript, $script)
            }
            Invoke-Native python @($preparePy, 'silent')
        } finally {
            $env:PSModulePath = $psModulePath
            if ($preparePy -and (Test-Path $preparePy)) { Remove-Item -Force $preparePy }
        }
        Pop-Location
        Step ("Dependencies ready in {0:hh\:mm\:ss}" -f ((Get-Date) - $t))
    }

    if ($PrepareOnly) {
        Step ("Total {0:hh\:mm\:ss}" -f ((Get-Date) - $Started))
        return
    }

    # --- configure --------------------------------------------------------------
    New-Item -ItemType Directory -Force $Out | Out-Null
    $apiCache = Join-Path $Out 'api-credentials.cmake'
    if ($TestApi) {
        $apiContent = "set(TDESKTOP_API_TEST ON CACHE BOOL `"`" FORCE)`n"
    } else {
        if (-not (Test-Path $ApiFile)) { Fail "API credentials file not found: $ApiFile (or use -TestApi)" }
        $kv = @{}
        foreach ($line in Get-Content $ApiFile) {
            if ($line -match '^\s*(api_id|api_hash)\s*=\s*(\S+)\s*$') { $kv[$Matches[1]] = $Matches[2] }
        }
        if ($kv['api_id'] -notmatch '^\d+$') { Fail "api_id missing or not numeric in $ApiFile" }
        if ($kv['api_hash'] -notmatch '^[0-9a-fA-F]{32}$') { Fail "api_hash missing or malformed in $ApiFile" }
        $apiContent = "set(TDESKTOP_API_TEST OFF CACHE BOOL `"`" FORCE)`n" +
            "set(TDESKTOP_API_ID `"$($kv['api_id'])`" CACHE STRING `"`" FORCE)`n" +
            "set(TDESKTOP_API_HASH `"$($kv['api_hash'])`" CACHE STRING `"`" FORCE)`n"
    }
    # Default gateway address: build-time only, never in the sources.
    if (-not $GatewayUrl -and $GatewayFile -and (Test-Path $GatewayFile)) {
        $GatewayUrl = Get-Content $GatewayFile -TotalCount 1
    }
    $GatewayUrl = "$GatewayUrl".Trim()
    if ($GatewayUrl -and $GatewayUrl -notmatch '^wss://[^"\\\s]+$') { Fail 'gateway address must be wss://...' }
    Write-Host ('Default gateway address: ' + $(if ($GatewayUrl) { 'set' } else { 'none, asked on the first launch' }))
    $apiContent += "set(TGESHKA_GATEWAY_URL `"$GatewayUrl`" CACHE STRING `"`" FORCE)`n"
    if (-not $Version -and $Release -match '^v(.+)$') { $Version = $Matches[1] }
    if ($Version -and $Version -notmatch '^\d{1,6}(\.\d{1,6}){1,3}(-[0-9A-Za-z.]{1,16})?$') { Fail "bad -Version $Version" }
    Write-Host ('TGeshka version: ' + $(if ($Version) { $Version } else { 'default from tgeshka_brand.h' }))
    $apiContent += "set(TGESHKA_VERSION `"$Version`" CACHE STRING `"`" FORCE)`n"
    # The fork must not reach Telegram Desktop update and crash-report servers directly.
    $apiContent += "set(DESKTOP_APP_DISABLE_AUTOUPDATE ON CACHE BOOL `"`" FORCE)`n" +
        "set(DESKTOP_APP_DISABLE_CRASH_REPORTS ON CACHE BOOL `"`" FORCE)`n"
    # The application must use the same MSVC toolset as the prepared libraries (vcvars one):
    # plain "-T v143" lets MSBuild pick its default (older) toolset and the link fails.
    $vsMajor = ((& $vswhere -path $vsPath -property installationVersion) -split '\.')[0]
    if ($Generator -eq 'Ninja') {
        # cl.exe of the vcvars environment above, the same toolset as the prepared libraries.
        $cmakeGenerator = 'Ninja Multi-Config'
        $toolsetArg = "cl $($env:VCToolsVersion.TrimEnd('\'))"
    } else {
        $cmakeGenerator = switch ($vsMajor) { '17' { 'Visual Studio 17 2022' } '18' { 'Visual Studio 18 2026' } default { Fail "unsupported Visual Studio $vsMajor" } }
        $toolsetArg = "v143,version=$($env:VCToolsVersion.TrimEnd('\'))"
    }
    if ($CompilerLauncher -and $Generator -ne 'Ninja') { Fail '-CompilerLauncher needs -Generator Ninja (MSBuild ignores launchers)' }
    $apiContent += "# generator: $cmakeGenerator, toolset: $toolsetArg`n"
    if ($CompilerLauncher) {
        $launcher = (Get-Command $CompilerLauncher -ErrorAction Stop).Source -replace '\\', '/'
        $apiContent += "set(CMAKE_C_COMPILER_LAUNCHER `"$launcher`" CACHE STRING `"`" FORCE)`n" +
            "set(CMAKE_CXX_COMPILER_LAUNCHER `"$launcher`" CACHE STRING `"`" FORCE)`n"
    }
    if ($NoPch) { $apiContent += "set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON CACHE BOOL `"`" FORCE)`n" }
    if ($NoDebugInfo) { $apiContent += "set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT `"`" CACHE STRING `"`" FORCE)`n" }
    $previous = if (Test-Path $apiCache) { [IO.File]::ReadAllText($apiCache) } else { '' }
    $apiChanged = $previous -ne $apiContent
    if ($apiChanged) { [IO.File]::WriteAllText($apiCache, $apiContent) }

    $cacheFile = Join-Path $Out 'CMakeCache.txt'
    $configured = Join-Path $Out 'configured.stamp'
    if ($Reconfigure -or $apiChanged -or -not (Test-Path $cacheFile) -or -not (Test-Path $configured)) {
        Step "Configuring ($cmakeGenerator, x64, $toolsetArg)"
        # Fresh cache: generator toolset cannot change in an existing one; values come from $apiCache.
        Remove-Item -Force -ErrorAction SilentlyContinue $cacheFile, $configured
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $Out 'CMakeFiles')
        Push-Location (Join-Path $Repo 'Telegram')
        if ($Generator -eq 'Ninja') {
            Invoke-Native python @('configure.py', '-G', $cmakeGenerator, '-C', $apiCache)
        } else {
            Invoke-Native python @('configure.py', '-G', $cmakeGenerator, '-Ax64', '-T', $toolsetArg, '-C', $apiCache)
        }
        Pop-Location
        Set-Content -Path $configured -Value (Get-Date -Format s)
    }

    # --- build ------------------------------------------------------------------
    $freeGB = [math]::Floor((Get-PSDrive ((Split-Path $Out -Qualifier).TrimEnd(':'))).Free / 1GB)
    if ($freeGB -lt $MinFreeGB) { Fail "only $freeGB GB free on the build disk, need at least $MinFreeGB GB (-MinFreeGB)" }
    # Concurrent cl.exe = projects (/m) x files per project (CL_MPCount). Each one maps the large
    # precompiled header; 16 x 16 exhausted virtual memory (C3859/C1076) and grew the page file.
    $projects = 2
    $perProject = [math]::Max(1, [math]::Floor($Jobs * 3 / 4))
    $msbuildArgs = @('/nologo', '/v:minimal', "/p:CL_MPCount=$perProject")

    # Upstream needs C++/WinRT projections newer than an older Windows SDK 10.0.26100 servicing
    # has (e.g. KeyCredentialManager::RequestCreateForWindowAsync). If the installed SDK lacks
    # them, generate the projections from this Windows' own metadata and put them first.
    $sdkWinrt = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Include\$($env:WindowsSDKVersion.TrimEnd('\'))\cppwinrt\winrt\Windows.Security.Credentials.h"
    if ((Test-Path $sdkWinrt) -and -not (Select-String -Path $sdkWinrt -Pattern 'RequestCreateForWindowAsync' -Quiet)) {
        $winrtVersion = ((Select-String -Path (Join-Path (Split-Path $sdkWinrt) 'base.h') -Pattern 'CPPWINRT_VERSION "([^"]+)"').Matches[0].Groups[1].Value)
        $winrtRoot = Join-Path $Root "winrt-local\$winrtVersion"
        $winrtInclude = Join-Path $winrtRoot 'include'
        $winrtCheck = Join-Path $winrtInclude 'winrt\Windows.Security.Credentials.h'
        if (-not ((Test-Path $winrtCheck) -and (Select-String -Path $winrtCheck -Pattern 'RequestCreateForWindowAsync' -Quiet))) {
            Step "Generating C++/WinRT $winrtVersion projections from local metadata"
            Invoke-Native (Join-Path $Root 'ThirdParty\NuGet\nuget.exe') @('install', 'Microsoft.Windows.CppWinRT', '-Version', $winrtVersion, '-OutputDirectory', (Join-Path $winrtRoot 'pkg'), '-NonInteractive')
            Invoke-Native (Join-Path $winrtRoot "pkg\Microsoft.Windows.CppWinRT.$winrtVersion\bin\cppwinrt.exe") @('-input', 'local', '-output', $winrtInclude)
        }
        Write-Host "Using local C++/WinRT projections: $winrtInclude"
        $msbuildArgs += '/p:IncludePath=' + (("$winrtInclude;$env:INCLUDE").TrimEnd(';') -replace ';', '%3B')
        $env:INCLUDE = "$winrtInclude;$env:INCLUDE"
    }

    $t = Get-Date
    if ($Generator -eq 'Ninja') {
        Step "Building Release (Ninja, $Jobs jobs, $freeGB GB free)"
        Invoke-Native cmake @('--build', $Out, '--config', 'Release', '--target', 'Telegram', '--parallel', "$Jobs")
    } else {
        Step "Building Release ($projects projects x $perProject files, $freeGB GB free)"
        Invoke-Native cmake (@('--build', $Out, '--config', 'Release', '--target', 'Telegram', '--parallel', "$projects", '--') + $msbuildArgs)
    }
    Step ("Build done in {0:hh\:mm\:ss}" -f ((Get-Date) - $t))

    # Executable / package names come from the brand header when the branch has one.
    $appFile = 'Telegram'; $appName = 'Telegram'; $appVersion = $null
    $brand = Join-Path $Repo 'Telegram\SourceFiles\tgeshka\tgeshka_brand.h'
    if (Test-Path $brand) {
        $text = [IO.File]::ReadAllText($brand)
        if ($text -match '#define\s+TGESHKA_APP_FILE\s+"([^"]+)"') { $appFile = $Matches[1] }
        if ($text -match '#define\s+TGESHKA_APP_NAME\s+"([^"]+)"') { $appName = $Matches[1] }
        if ($text -match '#define\s+TGESHKA_VERSION\s+"([^"]+)"') { $appVersion = $Matches[1] }
    }
    $exe = Join-Path $Out "Release\$appFile.exe"
    if (-not (Test-Path $exe)) { Fail "$exe not found after build" }

    # --- package ----------------------------------------------------------------
    $zip = $null
    if (-not $NoZip) {
        $version = if ($Version) { $Version } elseif ($appVersion) { $appVersion } else { (Select-String -Path (Join-Path $Repo 'Telegram\build\version') -Pattern '^AppVersionStr\s+(\S+)').Matches[0].Groups[1].Value }
        $sha = (git -C $Repo rev-parse --short HEAD).Trim()
        $name = if ($Version) { "$appName-$Version-win64" } else { "$appName-$version-$sha-win64-portable" }
        $stage = Join-Path $Out "package\$name"
        $appDir = Join-Path $stage $appName
        if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
        New-Item -ItemType Directory -Force $appDir | Out-Null
        Copy-Item $exe $appDir
        $updater = Join-Path $Out 'Release\Updater.exe'
        if (Test-Path $updater) { Copy-Item $updater $appDir }
        $modules = Join-Path $Out 'Release\modules'
        if (Test-Path $modules) { Copy-Item -Recurse $modules $appDir }
        # The update module checks it against the manifest version.
        [IO.File]::WriteAllText((Join-Path $appDir 'tgeshka-version.txt'), $version)
        $zip = Join-Path $Out "$name.zip"
        if (Test-Path $zip) { Remove-Item -Force $zip }
        Compress-Archive -Path $appDir -DestinationPath $zip -CompressionLevel Optimal
        Step "Package: $zip"
    }

    # --- release ----------------------------------------------------------------
    if ($Release) {
        if (-not $zip) { Fail '-Release requires the zip (drop -NoZip)' }
        if ($TestApi) { Fail '-Release with -TestApi is not allowed' }
        $commit = (git -C $Repo rev-parse HEAD).Trim()
        Step "Publishing GitHub Release $Release at $commit"
        Invoke-Native gh @('release', 'create', $Release, $zip,
            '--target', $commit, '--title', $Release, '--notes', "Windows x64 portable build of $commit")
    }

    Step ("Total {0:hh\:mm\:ss}" -f ((Get-Date) - $Started))
}
finally {
    Release-Locks
}
