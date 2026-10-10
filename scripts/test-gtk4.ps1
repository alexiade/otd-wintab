<#
.SYNOPSIS
    GTK 4 integration test: runs tests\gtk4\gtk4probe.c on a real GTK 4
    runtime with otd-wintab injected through the dwmapi proxy, and checks
    what a GTK 4 drawing application sees from scripted styli and mouse
    moves. GTK 4 is tested as it is; otd-wintab's GTK workarounds only
    exist for GTK 3.

    Takes over the mouse pointer for about ten seconds; don't touch it
    while the test runs. The pointer is put back afterwards.

.EXAMPLE
    # MSYS2's GTK 4
    .\build.ps1; .\scripts\test-gtk4.ps1

    # a GTK 4 build in front of the MSYS2 CLANG64 runtime
    .\scripts\test-gtk4.ps1 -GtkOverride S:\repos\gtk4-build\gtk
#>
param(
    [string]$GtkBin = 'C:\msys64\clang64\bin',
    [string[]]$GtkOverride = @(),   # directories searched before GtkBin (a GTK build)
    [switch]$Log
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$zig = Join-Path $root 'tools\zig\zig.exe'
$build = Join-Path $root 'build'
$obj = Join-Path $build 'obj4'
$out = Join-Path $build 'gtk4-test'

if (-not (Test-Path (Join-Path $GtkBin 'libgtk-4-1.dll'))) { throw "$GtkBin has no libgtk-4-1.dll" }
foreach ($f in 'dwmapi.dll', 'otd-wintab32.dll') {
    if (-not (Test-Path (Join-Path $build $f))) { throw "build\$f missing; run .\build.ps1 first" }
}
New-Item -ItemType Directory -Force $obj, $out | Out-Null

function Invoke-Native([string]$exe, [string[]]$a) {
    $ErrorActionPreference = 'Continue'
    & $exe @a 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "$exe $($a[0]) failed" }
}

# Import libraries for the few GTK/GLib functions the probe uses
$libs = @()
$section = $null
$sections = [ordered]@{}
foreach ($line in Get-Content (Join-Path $root 'tests\gtk4\gtk4-imports.def')) {
    if ($line -match '^\s*;' -or -not $line.Trim()) { continue }
    if ($line -match '^\[(.+)\]$') { $section = $Matches[1]; $sections[$section] = @(); continue }
    $sections[$section] += $line.Trim()
}
foreach ($dll in $sections.Keys) {
    $def = Join-Path $obj "$dll.def"
    Set-Content $def (@("LIBRARY $dll", 'EXPORTS') + $sections[$dll]) -Encoding ascii
    $lib = Join-Path $obj "$dll.a"
    Invoke-Native $zig @('dlltool', '-m', 'i386:x86-64', '-d', $def, '-l', $lib)
    $libs += $lib
}

Write-Host 'Building gtk4probe.exe'
Invoke-Native $zig (@('cc', '-target', 'x86_64-windows-gnu', '-std=gnu11', '-O2', '-Wall',
    '-I', "$root\src\wintab", '-I', "$root\src\wintab\compat", '-I', "$root\include",
    '-include', 'wine_compat.h', '-o', "$out\gtk4probe.exe",
    "$root\tests\gtk4\gtk4probe.c", "$root\src\wintab\ring.c", "$root\src\wintab\log.c") + $libs + @('-luser32'))
Remove-Item "$out\*.pdb" -ErrorAction SilentlyContinue

# Same layout as GIMP: proxy and provider next to the executable
Copy-Item (Join-Path $build 'dwmapi.dll'), (Join-Path $build 'otd-wintab32.dll') $out -Force

Write-Host 'Running GTK 4 integration test (hands off the mouse for ~10 s)'
$env:PATH = (@($GtkOverride) + $GtkBin + $env:PATH) -join ';'
$env:OTD_WINTAB_INSTANCE = "gtk4probe-$PID"
Remove-Item Env:OTD_WINTAB_FAKE -ErrorAction SilentlyContinue
foreach ($d in @($GtkOverride) + $GtkBin) {
    $gtk = Join-Path $d 'libgtk-4-1.dll'
    if (Test-Path $gtk) { Write-Host "GTK: $gtk ($((Get-Item $gtk).LastWriteTime))"; break }
}
if ($Log) { $env:OTD_WINTAB_LOG = Join-Path $out 'gtk4probe-wintab.log' }
& "$out\gtk4probe.exe"
exit $LASTEXITCODE
