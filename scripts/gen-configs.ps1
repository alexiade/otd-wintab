<#
.SYNOPSIS
    Regenerates configurations\: OpenTabletDriver's own tablet
    configurations for the Wacom Intuos3 family, with the report parser
    switched to otd-wintab's (OtdWintab.Parsers.*). Everything else is kept
    byte-for-byte, so the files override the built-in ones by name.
#>
param([string]$OtdTag = 'v0.6.7')

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'configurations'
New-Item -ItemType Directory -Force $out | Out-Null

$parsers = @{
    'OpenTabletDriver.Configurations.Parsers.Wacom.Intuos3.Intuos3ReportParser' = 'OtdWintab.Parsers.WacomIntuos3ReportParser'
    'OpenTabletDriver.Configurations.Parsers.Wacom.Intuos3.WacomDriverIntuos3ReportParser' = 'OtdWintab.Parsers.WacomDriverIntuos3ReportParser'
}

$tree = Invoke-RestMethod "https://api.github.com/repos/OpenTabletDriver/OpenTabletDriver/git/trees/$OtdTag`?recursive=1"
$files = $tree.tree.path | Where-Object { $_ -match '^OpenTabletDriver\.Configurations/Configurations/Wacom/PTZ-[^/]+\.json$' }
foreach ($f in $files) {
    $text = (Invoke-WebRequest "https://raw.githubusercontent.com/OpenTabletDriver/OpenTabletDriver/$OtdTag/$f" -UseBasicParsing).Content
    $changed = $false
    foreach ($p in $parsers.Keys) {
        if ($text.Contains("`"$p`"")) { $text = $text.Replace("`"$p`"", "`"$($parsers[$p])`""); $changed = $true }
    }
    if (-not $changed) { Write-Warning "$f uses no Intuos3 parser; skipped"; continue }
    $name = [IO.Path]::GetFileName($f)
    [IO.File]::WriteAllText((Join-Path $out $name), $text, (New-Object Text.UTF8Encoding $false))
    Write-Host "configurations\$name"
}
