<#
.SYNOPSIS
    Installs (or removes) otd-wintab into a GIMP 3 installation, for GIMP
    only. Copies two files into GIMP's bin directory:

      dwmapi.dll        proxy that redirects GTK's Wintab load
      otd-wintab32.dll  the Wintab provider

    Nothing outside GIMP's directory is changed. GIMP must not be running.

.EXAMPLE
    .\scripts\install-gimp.ps1
    .\scripts\install-gimp.ps1 -Uninstall
    .\scripts\install-gimp.ps1 -GimpDir 'D:\Apps\GIMP 3'
#>
param(
    [string]$GimpDir,
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$marker = 'otd-wintab dwmapi proxy'

function Find-Gimp {
    $keys = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
    $found = Get-ItemProperty $keys -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -match '^GIMP 3' -and $_.InstallLocation } |
        Sort-Object { [version]($_.DisplayVersion -replace '[^0-9.].*$', '') } -Descending |
        Select-Object -First 1
    if (-not $found) { throw 'GIMP 3 not found; pass -GimpDir' }
    $found.InstallLocation
}

function Test-OurProxy([string]$path) {
    if (-not (Test-Path $path)) { return $false }
    [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($path)).Contains($marker)
}

if (-not $GimpDir) { $GimpDir = Find-Gimp }
$bin = Join-Path $GimpDir 'bin'
if (-not (Test-Path (Join-Path $bin 'libgdk-3-0.dll'))) { throw "$bin does not look like a GIMP 3 bin directory" }

$running = Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.Path -and $_.Path.StartsWith($bin, [StringComparison]::OrdinalIgnoreCase) }
if ($running) { throw "Close GIMP first (running: $($running.Name -join ', '))" }

$proxy = Join-Path $bin 'dwmapi.dll'
$provider = Join-Path $bin 'otd-wintab32.dll'

if ((Test-Path $proxy) -and -not (Test-OurProxy $proxy)) {
    throw "$proxy exists and is not ours; refusing to touch it"
}

if ($Uninstall) {
    Remove-Item $proxy, $provider -ErrorAction SilentlyContinue
    Write-Host "Removed otd-wintab from $bin"
    return
}

foreach ($f in 'dwmapi.dll', 'otd-wintab32.dll') {
    if (-not (Test-Path (Join-Path $build $f))) { throw "build\$f missing; run .\build.ps1 first" }
}
Copy-Item (Join-Path $build 'dwmapi.dll') $proxy -Force
Copy-Item (Join-Path $build 'otd-wintab32.dll') $provider -Force
Write-Host "Installed otd-wintab into $bin"
Write-Host 'In GIMP: Edit > Preferences > Input Devices > Pointer Input API = Wintab, then restart GIMP.'
