# Packages the WebVSP engine as a portable Windows zip: no install, no Python, no build
# tools needed on the user's machine. Unzip and double-click "WebVSP Engine.cmd".
#
# Layout of the zip:
#   WebVSP Engine.cmd        launcher (starts the engine; opens the hosted UI unless a tab is already open)
#   python\                    embedded CPython + site-packages (fastapi, uvicorn, numpy, openvsp)
#   app\backend\vspweb\        the engine
#   app\frontend\dist\         the UI, also served locally (works offline)
#
# Requires a GUI build of OpenVSP's Python module (build_vsp.ps1 / the engine workflow) and a
# Python of the same minor version as the embedded one (used only to pip-install packages).
param(
    [string]$BuildDir = "$PSScriptRoot\..\..\build\vsp-gui",
    [string]$PythonExe = "$PSScriptRoot\..\.venv\Scripts\python.exe",
    [string]$EmbedVersion = '3.11.9',
    [string]$OutDir = "$PSScriptRoot\..\dist-engine",
    [string]$HostedUrl = 'https://mohammadadib.github.io/WebVSP/?engine=local'
)
$ErrorActionPreference = 'Stop'
$web = Resolve-Path "$PSScriptRoot\.."
$pseudo = Join-Path (Resolve-Path $BuildDir) 'python_pseudo'
if (-not (Test-Path "$pseudo\openvsp\openvsp\_vsp_g.pyd")) { throw "No GUI build of the OpenVSP module in $pseudo" }

$stage = Join-Path $OutDir 'WebVSP-Engine'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
$py = Join-Path $stage 'python'

Write-Host "== Embedded Python $EmbedVersion"
$embedZip = Join-Path $OutDir "python-$EmbedVersion-embed-amd64.zip"
if (-not (Test-Path $embedZip)) {
    Invoke-WebRequest "https://www.python.org/ftp/python/$EmbedVersion/python-$EmbedVersion-embed-amd64.zip" -OutFile $embedZip
}
Expand-Archive $embedZip -DestinationPath $py
$tag = ($EmbedVersion -split '\.')[0..1] -join ''
# Search path: stdlib zip, site-packages, and the engine package.
@("python$tag.zip", '.', 'Lib\site-packages', '..\app\backend', 'import site') |
    Set-Content -Encoding ascii (Join-Path $py "python$tag._pth")

Write-Host '== Python packages'
$site = Join-Path $py 'Lib\site-packages'
& $PythonExe -m pip install --quiet --no-compile --target $site fastapi 'uvicorn[standard]' numpy
if ($LASTEXITCODE -ne 0) { throw 'pip install failed' }

Write-Host '== OpenVSP module'
# Only what the engine imports (openvsp_config._IGNORE_IMPORTS skips the heavy helpers).
Copy-Item -Recurse "$pseudo\openvsp_config\openvsp_config" (Join-Path $site 'openvsp_config')
$ovsp = Join-Path $site 'openvsp'
New-Item -ItemType Directory -Force $ovsp | Out-Null
Get-ChildItem "$pseudo\openvsp\openvsp" -File | Where-Object { $_.Extension -in '.py', '.pyd' } |
    Copy-Item -Destination $ovsp
foreach ($exe in 'vspaero.exe', 'vspviewer.exe', 'vsploads.exe') {
    $found = Get-ChildItem (Resolve-Path $BuildDir) -Recurse -Filter $exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { Copy-Item $found.FullName $ovsp }
}

Write-Host '== MSVC runtime'
# The extension module needs the VC++ runtime; ship it so users don't need the redistributable.
foreach ($dll in 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'concrt140.dll', 'vcomp140.dll') {
    $src = Join-Path $env:WINDIR "System32\$dll"
    if (Test-Path $src) { Copy-Item $src $py -Force }
}

Write-Host '== Engine and UI'
$app = Join-Path $stage 'app'
New-Item -ItemType Directory -Force "$app\backend", "$app\frontend" | Out-Null
Copy-Item -Recurse (Join-Path $web 'backend\vspweb') "$app\backend\vspweb"
Get-ChildItem "$app\backend\vspweb" -Recurse -Directory -Filter __pycache__ | Remove-Item -Recurse -Force
$dist = Join-Path $web 'frontend\dist'
if (-not (Test-Path "$dist\index.html")) { throw "Build the UI first (npm run build in web\frontend)" }
Copy-Item -Recurse $dist "$app\frontend\dist"

Write-Host '== Launcher'
@"
@echo off
rem WebVSP engine: runs OpenVSP on this computer for the WebVSP web app.
rem Leave this window open while you use WebVSP; close it to stop the engine.
title WebVSP Engine
cd /d "%~dp0"
"%~dp0python\python.exe" -m vspweb --open $HostedUrl %*
if errorlevel 1 pause
"@ | Set-Content -Encoding ascii (Join-Path $stage 'WebVSP Engine.cmd')

@"
WebVSP Engine
===============

Runs OpenVSP on this computer for the WebVSP web app ($HostedUrl).

1. Double-click "WebVSP Engine.cmd". Leave its window open while you work.
2. An open WebVSP tab connects by itself; if none is open, your browser opens one.
   If the browser asks to allow access to devices on your local network, choose Allow.

Windows may warn that the file came from the internet: choose "More info" > "Run anyway".
No installation is needed; delete this folder to remove it.
The UI also works offline at http://localhost:8765 while the engine runs.
"@ | Set-Content -Encoding ascii (Join-Path $stage 'README.txt')

Write-Host '== Zip'
$zip = Join-Path $OutDir 'WebVSP-Engine-win64.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
'{0} ({1:N0} MB)' -f $zip, ((Get-Item $zip).Length / 1MB)
