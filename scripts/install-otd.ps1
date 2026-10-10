<#
.SYNOPSIS
    Installs (or removes) the otd-wintab OpenTabletDriver plugin, plus the
    Intuos3 configuration overrides that use its parser.

      %LOCALAPPDATA%\OpenTabletDriver\Plugins\otd-wintab\OtdWintab.dll
      %LOCALAPPDATA%\OpenTabletDriver\Configurations\PTZ-*.json

    Restart the OpenTabletDriver daemon afterwards, then pick
    "Wintab Absolute Mode" as the tablet's output mode.

.EXAMPLE
    .\scripts\install-otd.ps1
    .\scripts\install-otd.ps1 -NoConfigurations   # plugin only, keep the built-in parser
    .\scripts\install-otd.ps1 -Uninstall
#>
param(
    [switch]$Uninstall,
    [switch]$NoConfigurations
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$appData = Join-Path $env:LOCALAPPDATA 'OpenTabletDriver'
$pluginDir = Join-Path $appData 'Plugins\otd-wintab'
$configDir = Join-Path $appData 'Configurations'
$ours = Get-ChildItem (Join-Path $root 'configurations') -Filter '*.json'

if ($Uninstall) {
    Remove-Item $pluginDir -Recurse -ErrorAction SilentlyContinue
    foreach ($c in $ours) {
        $target = Join-Path $configDir $c.Name
        # only remove files that are still ours
        if ((Test-Path $target) -and (Get-Content $target -Raw).Contains('OtdWintab.Parsers.')) {
            Remove-Item $target
        }
    }
    Write-Host 'Removed otd-wintab from OpenTabletDriver. Restart the daemon.'
    return
}

$dll = Join-Path $root 'build\otd-plugin\OtdWintab.dll'
if (-not (Test-Path $dll)) { throw 'build\otd-plugin\OtdWintab.dll missing; run .\build.ps1 first' }

New-Item -ItemType Directory -Force $pluginDir | Out-Null
Copy-Item $dll $pluginDir -Force
Write-Host "Plugin installed into $pluginDir"

if (-not $NoConfigurations) {
    New-Item -ItemType Directory -Force $configDir | Out-Null
    foreach ($c in $ours) {
        $target = Join-Path $configDir $c.Name
        if ((Test-Path $target) -and -not (Get-Content $target -Raw).Contains('OtdWintab.Parsers.')) {
            Write-Warning "$target exists and is not ours; left alone"
            continue
        }
        Copy-Item $c.FullName $target -Force
    }
    Write-Host "Intuos3 configuration overrides installed into $configDir"
}
Write-Host 'Restart the OpenTabletDriver daemon, then choose "Wintab Absolute Mode" as the output mode.'
