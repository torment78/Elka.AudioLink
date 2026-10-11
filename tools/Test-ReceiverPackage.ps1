param([string]$Version='0.2.1')
$ErrorActionPreference='Stop'
if($Version -notmatch '^\d+\.\d+\.\d+$'){throw 'Expected a numeric release version.'}
$root=Split-Path $PSScriptRoot -Parent
$staged=Join-Path $root "build_package\$Version\receiver"
$verification=Join-Path $root ('build_package\verify-'+[guid]::NewGuid().ToString('N'))
$installed=Join-Path $verification 'installed'
New-Item -ItemType Directory -Path $verification | Out-Null
$setup=Join-Path $root "dist\elka-audiolink-receiver-$Version-windows-x64-setup.exe"
$zip=Join-Path $root "dist\elka-audiolink-$Version-windows-x64-portable.zip"
$process=Start-Process -FilePath $setup -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/ISOLATED=1',('/DIR="'+$installed+'"'),('/LOG="'+$verification+'\install.log"')) -WindowStyle Hidden -PassThru
if(!$process.WaitForExit(30000)){$process.Kill();throw 'Isolated installer timed out.'}
if($process.ExitCode){throw "Installer exit: $($process.ExitCode)"}
Expand-Archive -LiteralPath $zip -DestinationPath "$verification\portable"
$count=0
foreach($file in Get-ChildItem -LiteralPath $staged -Recurse -File){
    $relative=[IO.Path]::GetRelativePath($staged,$file.FullName)
    $expected=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    foreach($destination in @($installed,"$verification\portable\receiver")){
        $actual=(Get-FileHash -LiteralPath (Join-Path $destination $relative) -Algorithm SHA256).Hash
        if($actual -ne $expected){throw "Payload differs: $relative"}
    }
    $count++
}
$oldPath=$env:PATH; $oldPlugins=$env:QT_PLUGIN_PATH; $oldPlatform=$env:QT_QPA_PLATFORM
try {
    $env:PATH="$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_PLUGIN_PATH=$installed; $env:QT_QPA_PLATFORM='windows'
    $preview=Join-Path $verification 'installed-preview.png'
    $process=Start-Process -FilePath "$installed\ElkaAudioLink.exe" -ArgumentList @('--preview',('"'+$preview+'"')) -WindowStyle Hidden -PassThru -RedirectStandardError "$verification\preview.log"
    if(!$process.WaitForExit(30000)){$process.Kill();throw 'Packaged receiver preview timed out.'}
    if($process.ExitCode -or !(Test-Path -LiteralPath $preview)){throw 'Packaged receiver did not render.'}
} finally {$env:PATH=$oldPath; $env:QT_PLUGIN_PATH=$oldPlugins; $env:QT_QPA_PLATFORM=$oldPlatform}
$signature=(Get-AuthenticodeSignature -LiteralPath $setup).Status
Write-Output "PASS: $count payload files match installer and portable ZIP; receiver launches with packaged dependencies. Signature: $signature"
Write-Output "Preview: $preview"
# /ISOLATED suppresses installation registration and shortcuts. Uninstall only this
# generated checkout-local installation; user settings were isolated by --preview.
if(!([IO.Path]::GetFullPath($installed).StartsWith([IO.Path]::GetFullPath("$root\build_package\"),[StringComparison]::OrdinalIgnoreCase))){throw 'Unexpected isolated installation path.'}
$process=Start-Process -FilePath "$installed\unins000.exe" -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -PassThru
if(!$process.WaitForExit(30000)){$process.Kill();throw 'Isolated uninstaller timed out.'}
if($process.ExitCode -or (Test-Path -LiteralPath "$installed\ElkaAudioLink.exe")){throw 'Isolated uninstall failed.'}
Write-Output 'PASS: isolated uninstall removed its receiver executable.'
# An OBS-shaped fixture exercises destination layout without touching installed OBS.
# The entrypoint files are presence markers only; no OBS executable is launched here.
$obs=Join-Path $verification 'obs-fixture'
New-Item -ItemType Directory -Path "$obs\bin\64bit" | Out-Null
foreach($name in @('obs64.exe','Qt6Network.dll')){[IO.File]::WriteAllBytes("$obs\bin\64bit\$name",[byte[]]@())}
$obsSetup=Join-Path $root "dist\elka-audiolink-obs-$Version-windows-x64-setup.exe"
$process=Start-Process -FilePath $obsSetup -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/ISOLATED=1','/CURRENTUSER',('/DIR="'+$obs+'"'),('/LOG="'+$verification+'\obs-install.log"')) -WindowStyle Hidden -PassThru
if(!$process.WaitForExit(30000)){$process.Kill();throw 'Isolated sender installer timed out.'}
if($process.ExitCode){throw "Sender installer exit: $($process.ExitCode)"}
$pluginStaged=Join-Path $root "build_package\$Version\plugin"
$count=0
foreach($file in Get-ChildItem -LiteralPath $pluginStaged -Recurse -File){
    $relative=[IO.Path]::GetRelativePath($pluginStaged,$file.FullName)
    $expected=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    foreach($destination in @($obs,"$verification\portable\plugin")){
        $actual=(Get-FileHash -LiteralPath (Join-Path $destination $relative) -Algorithm SHA256).Hash
        if($actual -ne $expected){throw "Sender payload differs: $relative"}
    }
    $count++
}
Write-Output "PASS: all $count sender payload files match installer and combined portable ZIP. Signature: $((Get-AuthenticodeSignature -LiteralPath $obsSetup).Status)"
if(!([IO.Path]::GetFullPath($obs).StartsWith([IO.Path]::GetFullPath("$root\build_package\"),[StringComparison]::OrdinalIgnoreCase))){throw 'Unexpected isolated OBS fixture path.'}
$process=Start-Process -FilePath "$obs\data\obs-plugins\elka-audiolink\unins000.exe" -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -PassThru
if(!$process.WaitForExit(30000)){$process.Kill();throw 'Isolated sender uninstaller timed out.'}
if($process.ExitCode -or (Test-Path -LiteralPath "$obs\obs-plugins\64bit\elka-audiolink.dll")){throw 'Isolated sender uninstall failed.'}
foreach($name in @('obs64.exe','Qt6Network.dll')){if(!(Test-Path -LiteralPath "$obs\bin\64bit\$name")){throw 'Sender uninstall removed an OBS fixture file.'}}
Write-Output 'PASS: sender uninstall removes only its own plugin; OBS fixture files remain.'
