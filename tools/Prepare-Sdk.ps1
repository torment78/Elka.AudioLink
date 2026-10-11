param([string]$SourceSdk=(Join-Path $PSScriptRoot '..\..\obs-vban-audio\.deps'))
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$source=(Resolve-Path -LiteralPath $SourceSdk).Path
$destination=Join-Path $root '.deps'
if(!(Test-Path -LiteralPath "$source\cmake\libobsConfig.cmake")){throw 'Source SDK must contain the prepared OBS CMake package.'}
New-Item -ItemType Directory -Force -Path $destination | Out-Null
foreach($folder in @('cmake','include','lib','bin','obs-deps-2025-07-11-x64','obs-deps-qt6-2025-07-11-x64')) {
    if(!(Test-Path -LiteralPath "$destination\$folder")){Copy-Item -LiteralPath "$source\$folder" -Destination "$destination\$folder" -Recurse}
}
if(!(Test-Path -LiteralPath "$destination\frontend31")) {
    Copy-Item -LiteralPath "$source\obs-studio-31.1.1\frontend\api" -Destination "$destination\frontend31" -Recurse
}
if((Test-Path -LiteralPath "$source\obs-frontend32") -and !(Test-Path -LiteralPath "$destination\frontend32")) {
    Copy-Item -LiteralPath "$source\obs-frontend32" -Destination "$destination\frontend32" -Recurse
}
Write-Output "SDK ready: $destination"
