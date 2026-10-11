$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$ctest=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$previousPath=$env:PATH;$previousPlugins=$env:QT_PLUGIN_PATH
$env:PATH="$root\.deps\bin\64bit;$root\.deps\obs-deps-2025-07-11-x64\bin;$root\.deps\obs-deps-qt6-2025-07-11-x64\bin;$env:PATH"
$env:QT_PLUGIN_PATH="$root\.deps\obs-deps-qt6-2025-07-11-x64\plugins"
Push-Location $root
try { & $ctest --preset windows-x64; if($LASTEXITCODE){throw 'Integration tests failed.'} } finally {Pop-Location;$env:PATH=$previousPath;$env:QT_PLUGIN_PATH=$previousPlugins}
