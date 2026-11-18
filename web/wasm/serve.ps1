# Serve WebVSP for local testing:
#   web\wasm\build_wasm.ps1     (once, and after C++ changes: builds web\frontend\public\wasm)
#   web\wasm\serve.ps1          then open http://localhost:4173/
# Builds the UI and serves it with Vite's preview server (which sends the cross-origin
# isolation headers the engine's threads need).
param([int]$Port = 4173, [switch]$NoBuild, [switch]$Open)
$ErrorActionPreference = 'Stop'
$frontend = Resolve-Path "$PSScriptRoot\..\frontend"
if (-not (Test-Path "$frontend\public\wasm\vsp.wasm")) {
    throw "No engine module in $frontend\public\wasm: run web\wasm\build_wasm.ps1 first"
}
if (-not (Get-Command npm -ErrorAction SilentlyContinue)) { $env:Path = "C:\Program Files\nodejs;$env:Path" }
Push-Location $frontend
try {
    if (-not $NoBuild) {
        if (-not (Test-Path node_modules)) { npm ci }
        npm run build
        if ($LASTEXITCODE -ne 0) { throw 'UI build failed' }
    }
    $url = "http://localhost:$Port/"
    Write-Host "== WebVSP: $url"
    if ($Open) { Start-Process $url }
    npx vite preview --port $Port --strictPort
} finally {
    Pop-Location
}
