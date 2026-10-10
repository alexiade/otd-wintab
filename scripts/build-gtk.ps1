<#
.SYNOPSIS
    Builds GTK 3 or 4 from a source checkout with MSYS2 CLANG64, for testing
    with scripts\test-gtk.ps1 / scripts\test-gtk4.ps1. A thin wrapper around
    scripts\build-gtk.sh; see docs\gtk-testing.md.

.EXAMPLE
    .\scripts\build-gtk.ps1 -Gtk 3 -Source S:\src\gtk-3 -Build S:\src\gtk-3-build
    .\scripts\build-gtk.ps1 -Gtk 4 -Source S:\src\gtk -Build S:\src\gtk-build
#>
param(
    [Parameter(Mandatory)][ValidateSet('3', '4')][string]$Gtk,
    [Parameter(Mandatory)][string]$Source,
    [Parameter(Mandatory)][string]$Build,
    [string]$Msys = 'C:\msys64'
)

$ErrorActionPreference = 'Stop'
$bash = Join-Path $Msys 'usr\bin\bash.exe'
if (-not (Test-Path $bash)) { throw "MSYS2 not found at $Msys; pass -Msys" }
$script = (Join-Path $PSScriptRoot 'build-gtk.sh') -replace '\\', '/'
& $bash -lc "'$script' $Gtk '$((Resolve-Path $Source).Path -replace '\\', '/')' '$($Build -replace '\\', '/')'"
exit $LASTEXITCODE
