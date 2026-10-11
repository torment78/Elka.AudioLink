param([string]$Version='0.2.0')
$ErrorActionPreference='Stop'
if($Version -ne '0.2.0'){throw 'Update the receiver version before packaging another version.'}
$root=Split-Path $PSScriptRoot -Parent
$qt=Join-Path $root '.deps\obs-deps-qt6-2025-07-11-x64'
$deps=Join-Path $root '.deps\obs-deps-2025-07-11-x64'
$output=Join-Path $root 'dist'
$payload=Join-Path $root "build_package\$Version"
$helper=Join-Path $payload 'receiver'
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs=& $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$crt=Get-ChildItem -LiteralPath "$vs\VC\Redist\MSVC" -Directory | Where-Object {$_.Name -match '^14\.'} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
$crt=Join-Path $crt.FullName 'x64\Microsoft.VC145.CRT'
New-Item -ItemType Directory -Force -Path $output,$helper,"$helper\platforms","$helper\licenses" | Out-Null
Copy-Item -LiteralPath "$root\build_x64\RelWithDebInfo\ElkaAudioLink.exe" -Destination $helper
foreach($module in @('Core','Gui','Widgets','Network')){Copy-Item -LiteralPath "$qt\bin\Qt6$module.dll" -Destination $helper}
Copy-Item -LiteralPath "$qt\plugins\platforms\qwindows.dll" -Destination "$helper\platforms"
Copy-Item -Path "$crt\*.dll" -Destination $helper
function Copy-Tree([string]$Source,[string]$Destination) {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    foreach($item in Get-ChildItem -LiteralPath $Source -Recurse -File) {
        $relative=[IO.Path]::GetRelativePath($Source,$item.FullName)
        $target=Join-Path $Destination $relative
        New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
        Copy-Item -LiteralPath $item.FullName -Destination $target -Force
    }
}
foreach($file in @('README.md','LICENSE','NOTICE.md')){Copy-Item -LiteralPath "$root\$file" -Destination $helper}
Copy-Tree "$qt\licenses\qt6" "$helper\licenses\qt6"
Copy-Tree "$deps\licenses\mbedtls" "$helper\licenses\mbedtls"
Copy-Tree "$deps\licenses\speexdsp" "$helper\licenses\speexdsp"
Copy-Tree "$root\docs" "$helper\docs"
$iscc="${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
& $iscc "/DPayloadDir=$payload" "/DOutputPath=$output" "$root\installer\receiver.iss"
if($LASTEXITCODE){throw 'Receiver installer failed'}
# This update changes the receiver only. Existing OBS plugins use the same protocol.
Compress-Archive -Path "$payload\receiver" -DestinationPath "$output\elka-audiolink-receiver-$Version-windows-x64-portable.zip" -Force
$hashes=Get-ChildItem -LiteralPath $output -File | Where-Object {$_.Extension -in '.exe','.zip'} | Sort-Object Name | ForEach-Object {"$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower())  $($_.Name)"}
[IO.File]::WriteAllLines("$output\SHA256SUMS.txt",$hashes,[Text.UTF8Encoding]::new($false))
$releaseHashes=$hashes | Where-Object {$_ -match ('  elka-audiolink-(obs-|receiver-)?'+[regex]::Escape($Version)+'-windows-x64-')}
[IO.File]::WriteAllLines("$output\SHA256SUMS-v$Version.txt",$releaseHashes,[Text.UTF8Encoding]::new($false))
Write-Output "Packages ready: $output"
