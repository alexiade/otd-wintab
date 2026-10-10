<#
.SYNOPSIS
    Builds otd-wintab with a portable Zig toolchain (fetched into tools\zig
    on first use) and optionally runs the hardware-free test suite.

.EXAMPLE
    .\build.ps1          # build
    .\build.ps1 -Test    # build and run tests
#>
param(
    [switch]$Test,
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$zigVersion = '0.17.0'
$zigSha256 = 'b5663f69581dcf391293fbf16c06cb80d81d806545ce618b4d0bab7f0eb8c428'
$zigDir = Join-Path $root 'tools\zig'
$zig = Join-Path $zigDir 'zig.exe'
$out = Join-Path $root 'build'

function Get-Zig {
    if (Test-Path $zig) { return }
    Write-Host "Fetching Zig $zigVersion..."
    $zip = Join-Path $env:TEMP "zig-$zigVersion.zip"
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest "https://ziglang.org/download/$zigVersion/zig-x86_64-windows-$zigVersion.zip" -OutFile $zip -UseBasicParsing
    $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
    if ($hash -ne $zigSha256) { throw "Zig download checksum mismatch: $hash" }
    $tmp = Join-Path $root 'tools\zig-extract'
    Expand-Archive $zip -DestinationPath $tmp -Force
    Move-Item (Join-Path $tmp "zig-x86_64-windows-$zigVersion") $zigDir
    Remove-Item $tmp -Recurse
}

function Invoke-Zig([string[]]$zigArgs) {
    # Windows PowerShell turns native stderr into errors; let diagnostics through as text
    $ErrorActionPreference = 'Continue'
    & $zig @zigArgs 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "zig $($zigArgs[0]) failed" }
}

Get-Zig
New-Item -ItemType Directory -Force $out | Out-Null

$opt = if ($Config -eq 'Debug') { @('-O0', '-g') } else { @('-O2') }
$common = @('-target', 'x86_64-windows-gnu', '-std=gnu11', '-Wall', '-Wno-unused-function') + $opt
$wintabIncludes = @(
    '-I', "$root\src\wintab",
    '-I', "$root\src\wintab\compat",
    '-I', "$root\third_party\wine\include",
    '-I', "$root\include",
    '-include', 'wine_compat.h'
)

# Wintab provider: the Wine-derived core plus our backend
$wintabSources = @(
    "$root\src\wintab\wintab32.c",
    "$root\src\wintab\context.c",
    "$root\third_party\wine\dlls\wintab32\manager.c",
    "$root\src\wintab\backend_otd.c",
    "$root\src\wintab\fake_source.c",
    "$root\src\wintab\gtk_glue.c",
    "$root\src\wintab\iat.c",
    "$root\src\wintab\ring.c",
    "$root\src\wintab\log.c"
)
Write-Host 'Building otd-wintab32.dll'
Invoke-Zig (@('cc') + $common + $wintabIncludes + @('-shared', '-o', "$out\otd-wintab32.dll") +
    $wintabSources + @("$root\src\wintab\wintab32.def", '-luser32'))

# Test harness (talks to the DLL only through its exports and the ring)
Write-Host 'Building selftest.exe'
Invoke-Zig (@('cc') + $common + $wintabIncludes + @('-o', "$out\selftest.exe",
    "$root\tests\harness.c", "$root\tests\test_cases.c", "$root\tests\test_gtk_glue.c",
    "$root\src\wintab\ring.c", "$root\src\wintab\gtk_glue.c", "$root\src\wintab\iat.c",
    "$root\src\wintab\log.c", '-luser32'))

# Injection proxy: dwmapi.dll forwarding to the system copy
Write-Host 'Building dwmapi.dll (injection proxy)'
$obj = Join-Path $out 'obj'
New-Item -ItemType Directory -Force $obj | Out-Null
$systemDwmapi = Join-Path ([Environment]::GetFolderPath('System')) 'dwmapi.dll'
& "$root\scripts\gen-forward-def.ps1" -Dll $systemDwmapi -Out "$obj\dwmapi.def" `
    -LibraryName dwmapi -Extra 'otdwt_proxy_marker DATA'
Invoke-Zig (@('cc') + $common + @('-shared', '-o', "$out\dwmapi.dll",
    "$root\src\inject\dwmapi_proxy.c", "$obj\dwmapi.def"))

# Injection test: a mock libgdk + host laid out like GIMP's bin directory
Write-Host 'Building injection test'
$injectDir = Join-Path $out 'inject-test'
New-Item -ItemType Directory -Force $injectDir | Out-Null
Invoke-Zig (@('cc') + $common + @('-shared', '-o', "$injectDir\libgdk-3-0.dll",
    "-Wl,--out-implib,$obj\libgdk-3-0.dll.a", "$root\tests\inject\mock_gdk.c", '-ldwmapi'))
Invoke-Zig (@('cc') + $common + @('-o', "$injectDir\inject_host.exe",
    "$root\tests\inject\host.c", "$obj\libgdk-3-0.dll.a", '-ldwmapi'))
Copy-Item "$out\dwmapi.dll", "$out\otd-wintab32.dll" $injectDir

# Diagnostic: prints live ring events
Invoke-Zig (@('cc') + $common + $wintabIncludes + @('-o', "$out\ringwatch.exe", "$root\tools\ringwatch.c",
    "$root\src\wintab\ring.c", "$root\src\wintab\log.c"))

# Layout probe, so the C# side can be checked against the C header
Invoke-Zig (@('cc') + $common + @('-I', "$root\include", '-o', "$out\ringlayout.exe", "$root\tests\ringlayout.c"))

Remove-Item "$out\*.pdb", "$out\*.lib", "$injectDir\*.pdb", "$injectDir\*.lib" -ErrorAction SilentlyContinue

# OpenTabletDriver plugin
Write-Host 'Building OpenTabletDriver plugin'
$dotnetConfig = if ($Config -eq 'Debug') { 'Debug' } else { 'Release' }
& dotnet build "$root\src\otd-plugin\OtdWintab.csproj" -c $dotnetConfig -o "$out\otd-plugin" --nologo -v quiet
if ($LASTEXITCODE -ne 0) { throw 'plugin build failed' }

if ($Test) {
    Write-Host 'Running provider tests'
    & "$out\selftest.exe" "$out\otd-wintab32.dll"
    if ($LASTEXITCODE -ne 0) { throw 'provider tests failed' }

    Write-Host 'Running injection tests'
    & "$injectDir\inject_host.exe"
    if ($LASTEXITCODE -ne 0) { throw 'injection test failed' }
    $env:OTD_WINTAB_DISABLE = '1'
    try { & "$injectDir\inject_host.exe" disabled } finally { Remove-Item Env:OTD_WINTAB_DISABLE }
    if ($LASTEXITCODE -ne 0) { throw 'injection test (disabled) failed' }

    Write-Host 'Running plugin tests'
    & dotnet test "$root\tests\otd-plugin\OtdWintab.Tests.csproj" -c $dotnetConfig --nologo -v quiet
    if ($LASTEXITCODE -ne 0) { throw 'plugin tests failed' }
}
