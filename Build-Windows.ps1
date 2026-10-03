param(
    [string]$BuildDir = "_Build/windows",
    [string]$QtDir = "C:/Qt/6.10.3/msvc2022_64",
    [string]$VsDir = "C:\Program Files\Microsoft Visual Studio\2022\Community",
    [switch]$Fresh,
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$vcvars = Join-Path $VsDir 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw "vcvars64.bat not found: $vcvars" }
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}

if (-not $env:VULKAN_SDK) {
    $env:VULKAN_SDK = [Environment]::GetEnvironmentVariable('VULKAN_SDK', 'Machine')
}
if (-not $env:VULKAN_SDK) {
    $sdk = Get-ChildItem C:\VulkanSDK -Directory -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
    if ($sdk) { $env:VULKAN_SDK = $sdk.FullName }
}
if ($env:VULKAN_SDK) { $env:PATH = "$env:VULKAN_SDK\Bin;$env:PATH" }

foreach ($tool in 'cmake', 'ninja', 'clang-cl', 'glslangValidator') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool not found on PATH" }
}
if (-not (Test-Path -LiteralPath "$QtDir/lib/cmake/Qt6")) { throw "Qt not found: $QtDir" }

git submodule update --init --recursive
if ($LASTEXITCODE) { throw "submodule update failed" }

# Every build gets the next "UFC Dev NNN" number (shown in the window titles); a failed
# build hands its number back so the sequence has no gaps.
$iterationFile = Join-Path $PSScriptRoot 'build-iteration.txt'
$previousIteration = if (Test-Path -LiteralPath $iterationFile) { [int](Get-Content -LiteralPath $iterationFile -TotalCount 1) } else { 0 }
$iteration = $previousIteration + 1
Set-Content -LiteralPath $iterationFile -Value $iteration -Encoding ascii
$iterationLabel = 'UFC Dev {0:D3}' -f $iteration
Write-Host "Building $iterationLabel"

try {
    $configure = @('-S', '.', '-B', $BuildDir, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_C_COMPILER=clang-cl', '-DCMAKE_CXX_COMPILER=clang-cl', "-DCMAKE_PREFIX_PATH=$QtDir")
    if ($Fresh) { $configure = @('--fresh') + $configure }
    cmake @configure
    if ($LASTEXITCODE) { throw "configure failed" }

    cmake --build $BuildDir --target launcher
    if ($LASTEXITCODE) { throw "launcher build failed" }
    # A crashed emulator or antivirus scan can briefly lock the installed executables.
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        cmake --install $BuildDir --prefix "$BuildDir/install"
        if (-not $LASTEXITCODE) { break }
        if ($attempt -eq 5) { throw "install failed" }
        Get-Process kyty_emulator -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 3
    }
} catch {
    Set-Content -LiteralPath $iterationFile -Value $previousIteration -Encoding ascii
    throw
}

if (-not $SkipTests) {
    cmake --build $BuildDir --target kyty_tests
    if ($LASTEXITCODE) { throw "test build failed" }
    ctest --test-dir $BuildDir --output-on-failure
    if ($LASTEXITCODE) { throw "tests failed" }
}
Write-Host "Build OK ($iterationLabel): $BuildDir/install/kyty_emulator.exe"
