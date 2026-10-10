<#
.SYNOPSIS
    GTK integration test: runs tests\gtk\gtkprobe.c on a real GTK 3 runtime
    (GIMP 3's), with otd-wintab injected the same way as into GIMP, and
    replays GIMP's input-device logic against scripted styli and mouse
    moves.

    Takes over the mouse pointer for about ten seconds; don't touch it
    while the test runs. The pointer is put back afterwards.

.EXAMPLE
    .\build.ps1; .\scripts\test-gtk.ps1
    .\scripts\test-gtk.ps1 -GtkBin 'D:\Apps\GIMP 3\bin' -Log
#>
param(
    [string]$GtkBin,
    [switch]$Log
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$zig = Join-Path $root 'tools\zig\zig.exe'
$build = Join-Path $root 'build'
$obj = Join-Path $build 'obj'
$out = Join-Path $build 'gtk-test'

if (-not $GtkBin) {
    $gimp = Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
                             'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*' -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -match '^GIMP 3' -and $_.InstallLocation } | Select-Object -First 1
    if (-not $gimp) { throw 'No GTK 3 runtime found (GIMP 3 not installed); pass -GtkBin' }
    $GtkBin = Join-Path $gimp.InstallLocation 'bin'
}
if (-not (Test-Path (Join-Path $GtkBin 'libgtk-3-0.dll'))) { throw "$GtkBin has no libgtk-3-0.dll" }
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
foreach ($line in Get-Content (Join-Path $root 'tests\gtk\gtk-imports.def')) {
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

Write-Host 'Building gtkprobe.exe'
Invoke-Native $zig (@('cc', '-target', 'x86_64-windows-gnu', '-std=gnu11', '-O2', '-Wall',
    '-I', "$root\src\wintab", '-I', "$root\src\wintab\compat", '-I', "$root\include",
    '-include', 'wine_compat.h', '-o', "$out\gtkprobe.exe",
    "$root\tests\gtk\gtkprobe.c", "$root\src\wintab\ring.c", "$root\src\wintab\log.c") + $libs + @('-luser32'))
Remove-Item "$out\*.pdb" -ErrorAction SilentlyContinue

# Same layout as GIMP: proxy and provider next to the executable
Copy-Item (Join-Path $build 'dwmapi.dll'), (Join-Path $build 'otd-wintab32.dll') $out -Force

Write-Host 'Running GTK integration test (hands off the mouse for ~10 s)'
$env:PATH = "$GtkBin;$env:PATH"
$env:OTD_WINTAB_INSTANCE = "gtkprobe-$PID"
Remove-Item Env:OTD_WINTAB_FAKE -ErrorAction SilentlyContinue
if ($Log) { $env:OTD_WINTAB_LOG = Join-Path $out 'gtkprobe-wintab.log' }
& "$out\gtkprobe.exe"
if ($LASTEXITCODE -ne 0) { throw 'GTK integration test failed' }
