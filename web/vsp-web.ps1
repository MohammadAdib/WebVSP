# One-click launcher for TitanVSP (the OpenVSP web GUI).
#   1. Kills anything holding the GUI ports (old vsp-web servers, Vite dev servers).
#   2. Rebuilds the frontend if any source file is newer than the last build.
#   3. Starts the server in this window and opens a browser tab at http://localhost:8765.
# Close this window (or Ctrl+C) to stop. Pass a .vsp3 path to open it.
param(
    [string]$File,
    [switch]$NoBrowser
)
$ErrorActionPreference = 'Stop'
$web = $PSScriptRoot
$port = 8765
$url = "http://localhost:$port/"
$Host.UI.RawUI.WindowTitle = "TitanVSP - $url"

function Stop-PortOwners([int[]]$ports) {
    foreach ($p in $ports) {
        $owners = Get-NetTCPConnection -LocalPort $p -State Listen -ErrorAction SilentlyContinue |
            Select-Object -ExpandProperty OwningProcess -Unique
        foreach ($procId in $owners) {
            if ($procId -and $procId -ne $PID) {
                $name = (Get-Process -Id $procId -ErrorAction SilentlyContinue).ProcessName
                Write-Host "Stopping $name (pid $procId) on port $p"
                # /T also takes down child processes (uvicorn workers, node children)
                taskkill /PID $procId /T /F *> $null
            }
        }
    }
    foreach ($p in $ports) {
        for ($i = 0; $i -lt 50 -and (Get-NetTCPConnection -LocalPort $p -State Listen -ErrorAction SilentlyContinue); $i++) {
            Start-Sleep -Milliseconds 100
        }
    }
}

Write-Host '== Stopping old servers'
Stop-PortOwners @($port, 5173)

Write-Host '== Checking OpenVSP Python module'
# A rebuilt _vsp.pyd can't be installed while a server has it loaded; now that the servers
# are stopped, copy in any newer build output.
$repo = Split-Path $web -Parent
$pkg = Join-Path $repo 'build\vsp-gui\python_pseudo\openvsp\openvsp'
$stale = $false
foreach ($mod in 'vsp', 'vsp_g') {
    $built = Join-Path $repo "build\vsp-gui\python_api\Release\_$mod.pyd"
    $installed = Join-Path $pkg "_$mod.pyd"
    if ((Test-Path $built) -and (Test-Path $installed) -and
        ((Get-Item $built).LastWriteTime -gt (Get-Item $installed).LastWriteTime)) {
        Copy-Item $built $installed -Force
        Copy-Item (Join-Path $repo "build\vsp-gui\python_api\$mod.py") (Join-Path $pkg "$mod.py") -Force
        $stale = $true
    }
}
if ($stale) { Write-Host '   installed newer build' } else { Write-Host '   up to date' }

Write-Host '== Checking frontend build'
$fe = Join-Path $web 'frontend'
$distIndex = Join-Path $fe 'dist\index.html'
$builtAt = if (Test-Path $distIndex) { (Get-Item $distIndex).LastWriteTime } else { [datetime]::MinValue }
$sources = @(Get-ChildItem (Join-Path $fe 'src') -Recurse -File) +
    @(Get-ChildItem $fe -File | Where-Object { $_.Name -in 'index.html', 'package.json', 'vite.config.ts' }) +
    @(Get-ChildItem (Join-Path $fe 'public') -Recurse -File -ErrorAction SilentlyContinue)
$stale = $sources | Where-Object { $_.LastWriteTime -gt $builtAt } | Select-Object -First 1
if ($stale) {
    Write-Host "   rebuilding (changed: $($stale.Name))"
    $nodeDir = 'C:\Program Files\nodejs'
    if ((Test-Path $nodeDir) -and -not ($env:Path -split ';' -contains $nodeDir)) { $env:Path = "$nodeDir;$env:Path" }
    Push-Location $fe
    try {
        if (-not (Test-Path node_modules)) { npm install --no-audit --no-fund }
        npm run build
        if ($LASTEXITCODE -ne 0) { throw 'Frontend build failed (see output above).' }
    } finally { Pop-Location }
} else {
    Write-Host '   up to date'
}

Write-Host "== Starting TitanVSP at $url"
$exe = Join-Path $web '.venv\Scripts\vsp-web.exe'
if (-not (Test-Path $exe)) { throw "Backend not installed. Run web\scripts\build_vsp.ps1 first." }
$vspArgs = @('--port', $port)
if ($NoBrowser) { $vspArgs += '--no-browser' }
if ($File) { $vspArgs += (Resolve-Path $File).Path }
& $exe @vspArgs
