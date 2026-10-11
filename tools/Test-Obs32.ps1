param([Parameter(Mandatory=$true)][string]$RuntimeRoot, [string]$PluginPath, [switch]$Callback)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$PluginPath){$PluginPath="$root\build_x64\RelWithDebInfo\elka-audiolink.dll"}
$PluginPath=(Resolve-Path -LiteralPath $PluginPath).Path
$runtime=Join-Path (Resolve-Path -LiteralPath $RuntimeRoot).Path 'bin\64bit'
$oldPath=$env:PATH;$oldPlugins=$env:QT_PLUGIN_PATH;$oldPlatform=$env:QT_QPA_PLATFORM
$env:PATH="$runtime;$root\.deps\obs-deps-2025-07-11-x64\bin;$env:PATH"
$env:QT_PLUGIN_PATH=$runtime;$env:QT_QPA_PLATFORM='windows'
try {
    $argsList=@('"'+$PluginPath+'"','"'+$root+'\data"','"'+$root+'\build_x64\plugin-preview32.png"')
    if($Callback){$argsList+='--callback'}
    $process=Start-Process -FilePath "$root\build_x64\RelWithDebInfo\plugin-smoke32.exe" -ArgumentList $argsList -WindowStyle Hidden -PassThru -RedirectStandardOutput "$root\build_x64\obs32-out.log" -RedirectStandardError "$root\build_x64\obs32-err.log"
    if(!$process.WaitForExit(30000)){$process.Kill();throw 'OBS 32 test timed out'}
    Get-Content "$root\build_x64\obs32-err.log" -Tail 12
    Get-Content "$root\build_x64\obs32-out.log" -Tail 4
    if($process.ExitCode){throw "OBS 32 test failed: $($process.ExitCode)"}
} finally {$env:PATH=$oldPath;$env:QT_PLUGIN_PATH=$oldPlugins;$env:QT_QPA_PLATFORM=$oldPlatform}
