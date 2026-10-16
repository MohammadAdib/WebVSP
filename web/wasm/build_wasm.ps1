# Builds WebVSP's engine: OpenVSP, its third-party libraries, FLTK and VSPAERO for WebAssembly.
# Runs on Windows and, with PowerShell 7, on Linux (the Pages workflow builds it this way).
#
# Needs the Emscripten SDK (emsdk) and Ninja:
#   - emsdk: an environment already set up by emsdk_env (EMSDK set), or -Emsdk <dir>
#     (default <parent of repo>/emsdk: git clone https://github.com/emscripten-core/emsdk;
#     emsdk install 6.0.12; emsdk activate 6.0.12);
#   - Ninja: on PATH, or -NinjaDir <dir> (default <parent of repo>/tools/ninja/bin).
# With ccache on PATH, compiles go through it (set CCACHE_COMPILERCHECK so a reinstalled emsdk
# of the same version still hits; the workflow does).
#
# STEPcode is skipped: its build runs a schema code generator, which would itself be
# compiled to WebAssembly and can't run during the build (STEP export is unavailable).
param(
    [ValidateSet('libs', 'fltk', 'vsp', 'all')]
    [string]$Stage = 'all',
    [string]$Emsdk = "$PSScriptRoot/../../../emsdk",
    [string]$NinjaDir = "$PSScriptRoot/../../../tools/ninja/bin",
    [string]$BuildDir = "$PSScriptRoot/../../build/libs-wasm",
    [string]$VspBuildDir = "$PSScriptRoot/../../build/vsp-wasm",
    # Targets of the OpenVSP build: the engine module the page loads, and VSPAERO's programs.
    [string[]]$Targets = @('vsp_wasm', 'vspaero', 'vsploads'),
    # Where the page serves the engine from.
    [string]$PublicDir = "$PSScriptRoot/../frontend/public/wasm"
)
$root = (Resolve-Path "$PSScriptRoot/../..").Path -replace '\\', '/'
$sep = [IO.Path]::PathSeparator

if (-not $env:EMSDK) {
    $env:EMSDK_QUIET = '1'
    & (Join-Path (Resolve-Path $Emsdk) 'emsdk_env.ps1') *> $null
    if (-not $env:EMSDK) { throw "emsdk not found at $Emsdk (or set up its environment first)" }
}
if (Test-Path $NinjaDir) { $env:PATH = "$((Resolve-Path $NinjaDir).Path)$sep$env:PATH" }
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) { throw 'ninja not found: put it on PATH or pass -NinjaDir' }
# Through the environment so every ExternalProject sub-build uses it too (CMake >= 3.21).
$env:CMAKE_TOOLCHAIN_FILE = (Resolve-Path "$PSScriptRoot/toolchain.cmake").Path -replace '\\', '/'
$env:CMAKE_GENERATOR = 'Ninja'
$launcher = @()
if (Get-Command ccache -ErrorAction SilentlyContinue) {
    $launcher = @('-DCMAKE_C_COMPILER_LAUNCHER=ccache', '-DCMAKE_CXX_COMPILER_LAUNCHER=ccache')
}
$python = if ($env:EMSDK_PYTHON) { $env:EMSDK_PYTHON } elseif (Get-Command python3 -ErrorAction SilentlyContinue) { 'python3' } else { 'python' }

if ($Stage -in 'libs', 'all') {
    # Graphics on (the GUI is mirrored to the page): exprparse and GLM build as usual; GLEW
    # comes with Emscripten; FLTK is built separately for the browser platform (below).
    cmake -S "$root/Libraries" -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release `
        -DVSP_NO_GRAPHICS=OFF -DVSP_USE_SYSTEM_STEPCODE=ON -DVSP_USE_SYSTEM_GLEW=ON -DVSP_USE_SYSTEM_FLTK=ON
    if ($LASTEXITCODE -ne 0) { throw 'libraries: configure failed' }
    cmake --build $BuildDir
    if ($LASTEXITCODE -ne 0) { throw 'libraries: build failed' }
}

if ($Stage -in 'fltk', 'all') {
    # FLTK with the "headless" platform (web/wasm/fltk): windows exist and lay out but are never
    # displayed; OpenVSP's GUI is mirrored to the page instead.
    $fltkBuild = "$root/build/fltk-wasm"
    if (Test-Path $fltkBuild) { Remove-Item -Recurse -Force $fltkBuild }
    New-Item -ItemType Directory -Force $fltkBuild | Out-Null
    Expand-Archive (Get-Item "$root/Libraries/fltk-*.zip").FullName -DestinationPath $fltkBuild
    $fltkSrc = (Get-ChildItem $fltkBuild -Directory | Select-Object -First 1).FullName -replace '\\', '/'
    & $python "$PSScriptRoot/fltk/patch_fltk.py" $fltkSrc
    if ($LASTEXITCODE -ne 0) { throw 'FLTK: patch failed' }
    $fltkPrefix = (Join-Path (Resolve-Path $BuildDir) 'FLTK-prefix') -replace '\\', '/'
    cmake -S $fltkSrc -B "$fltkBuild/build" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$fltkPrefix" `
        -DFLTK_BACKEND_HEADLESS=ON -DFLTK_BACKEND_X11=OFF -DFLTK_BACKEND_WAYLAND=OFF `
        -DFLTK_BUILD_GL=ON -DFLTK_OPTION_SVG=ON -DFLTK_OPTION_PRINT_SUPPORT=OFF `
        -DFLTK_BUILD_TEST=OFF -DFLTK_BUILD_FLUID=OFF -DFLTK_BUILD_FLTK_OPTIONS=OFF -DFLTK_BUILD_EXAMPLES=OFF `
        -DFLTK_USE_SYSTEM_LIBJPEG=OFF -DFLTK_USE_SYSTEM_ZLIB=OFF -DFLTK_USE_SYSTEM_LIBPNG=OFF `
        -DFL_ABI_VERSION=10403
    if ($LASTEXITCODE -ne 0) { throw 'FLTK: configure failed' }
    cmake --build "$fltkBuild/build" -- -k 0
    if ($LASTEXITCODE -ne 0) { throw 'FLTK: build failed' }
    cmake --install "$fltkBuild/build"
    if ($LASTEXITCODE -ne 0) { throw 'FLTK: install failed' }
}

if ($Stage -in 'vsp', 'all') {
    $libs = (Resolve-Path $BuildDir).Path -replace '\\', '/'
    cmake -S "$root/src" -B $VspBuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release "-DVSP_LIBRARY_PATH=$libs" `
        -DVSP_NO_GRAPHICS=OFF "-DFLTK_INSTALL_DIR=$libs/FLTK-prefix" `
        -DVSP_NO_API_WRAPPERS=ON -DVSP_NO_VSPAERO=OFF -DVSP_NO_VSPAERO_OPT=ON -DVSP_NO_HELP=ON -DVSP_NO_DOC=ON `
        -DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=ON @launcher # VSPAERO single-threaded (Emscripten's OpenMP pulls in debug libraries)
    if ($LASTEXITCODE -ne 0) { throw 'OpenVSP: configure failed' }
    cmake --build $VspBuildDir --target @Targets -- -k 0
    if ($LASTEXITCODE -ne 0) { throw 'OpenVSP: build failed' }
    New-Item -ItemType Directory -Force $PublicDir | Out-Null
    # The engine module, and VSPAERO's programs (run by the page in workers, see src/util/WebProcess.h).
    $outputs = @{ vsp_wasm = @('wasm_engine', 'vsp'); vspaero = @('vsp_aero/Solver', 'vspaero'); vsploads = @('vsp_aero/Adb2Load', 'vsploads') }
    foreach ($t in $Targets) {
        if (-not $outputs.ContainsKey($t)) { continue }
        $dir, $name = $outputs[$t]
        Copy-Item "$VspBuildDir/$dir/$name.js", "$VspBuildDir/$dir/$name.wasm" $PublicDir -Force
        "$name copied to $((Resolve-Path $PublicDir).Path)"
    }
}
