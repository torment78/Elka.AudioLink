param([string]$Configuration='RelWithDebInfo')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if(!(Test-Path -LiteralPath $cmake)){throw 'Visual Studio CMake not found.'}
Push-Location $root
try { & $cmake --preset windows-x64; if($LASTEXITCODE){throw 'Configure failed'}; & $cmake --build --preset windows-x64 --config $Configuration --parallel; if($LASTEXITCODE){throw 'Build failed'} } finally {Pop-Location}
