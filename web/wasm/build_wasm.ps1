# Builds OpenVSP's third-party libraries for WebAssembly (experimental in-browser engine).
#
# Needs the Emscripten SDK (emsdk) and Ninja. Defaults assume both sit next to the repo:
#   <parent>\emsdk                         git clone https://github.com/emscripten-core/emsdk; emsdk install/activate latest
#   <parent>\tools\ninja\bin\ninja.exe     pip install ninja --target <parent>\tools\ninja
#
# STEPcode is skipped: its build runs a schema code generator, which would itself be
# compiled to WebAssembly and can't run during the build (STEP export is unavailable).
param(
    [ValidateSet('libs', 'fltk', 'vsp', 'all')]
    [string]$Stage = 'all',
    [string]$Emsdk = "$PSScriptRoot\..\..\..\emsdk",
    [string]$NinjaDir = "$PSScriptRoot\..\..\..\tools\ninja\bin",
    [string]$BuildDir = "$PSScriptRoot\..\..\build\libs-wasm",
    [string]$VspBuildDir = "$PSScriptRoot\..\..\build\vsp-wasm",
    # Targets of the OpenVSP build (default: the engine module the web UI loads).
    [string[]]$Targets = @('vsp_wasm', 'vspaero', 'vsploads'),
    # Where the web UI serves the engine module from.
    [string]$PublicDir = "$PSScriptRoot\..\frontend\public\wasm"
)
$root = Resolve-Path "$PSScriptRoot\..\.."

$env:EMSDK_QUIET = '1'
& (Join-Path (Resolve-Path $Emsdk) 'emsdk_env.ps1') *> $null
if (-not $env:EMSDK) { throw "emsdk not found at $Emsdk" }
$env:Path = "$(Resolve-Path $NinjaDir);$env:Path"
# Through the environment so every ExternalProject sub-build uses it too (CMake >= 3.21).
$env:CMAKE_TOOLCHAIN_FILE = (Resolve-Path "$PSScriptRoot\toolchain.cmake").Path
$env:CMAKE_GENERATOR = 'Ninja'

if ($Stage -in 'libs', 'all') {
    # Graphics on (the GUI is mirrored to the page): exprparse and GLM build as usual; GLEW
    # comes with Emscripten; FLTK is built separately for the browser platform (build_fltk).
    cmake -S "$root\Libraries" -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release `
        -DVSP_NO_GRAPHICS=OFF -DVSP_USE_SYSTEM_STEPCODE=ON -DVSP_USE_SYSTEM_GLEW=ON -DVSP_USE_SYSTEM_FLTK=ON
    if ($LASTEXITCODE -ne 0) { throw 'libraries: configure failed' }
    cmake --build $BuildDir
    if ($LASTEXITCODE -ne 0) { throw 'libraries: build failed' }
}

if ($Stage -in 'fltk', 'all') {
    # FLTK with the "headless" platform (web/wasm/fltk): windows exist and lay out but are never
    # displayed; OpenVSP's GUI is mirrored to the page instead.
    $fltkBuild = "$root\build\fltk-wasm"
    if (Test-Path $fltkBuild) { Remove-Item -Recurse -Force $fltkBuild }
    New-Item -ItemType Directory -Force $fltkBuild | Out-Null
    Expand-Archive (Get-Item "$root\Libraries\fltk-*.zip").FullName -DestinationPath $fltkBuild
    $fltkSrc = (Get-ChildItem $fltkBuild -Directory | Select-Object -First 1).FullName
    & $env:EMSDK_PYTHON "$PSScriptRoot\fltk\patch_fltk.py" $fltkSrc
    if ($LASTEXITCODE -ne 0) { throw 'FLTK: patch failed' }
    $fltkPrefix = (Join-Path (Resolve-Path $BuildDir) 'FLTK-prefix') -replace '\\', '/'
    cmake -S $fltkSrc -B "$fltkBuild\build" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$fltkPrefix" `
        -DFLTK_BACKEND_HEADLESS=ON -DFLTK_BACKEND_X11=OFF -DFLTK_BACKEND_WAYLAND=OFF `
        -DFLTK_BUILD_GL=ON -DFLTK_OPTION_SVG=ON -DFLTK_OPTION_PRINT_SUPPORT=OFF `
        -DFLTK_BUILD_TEST=OFF -DFLTK_BUILD_FLUID=OFF -DFLTK_BUILD_FLTK_OPTIONS=OFF -DFLTK_BUILD_EXAMPLES=OFF `
        -DFLTK_USE_SYSTEM_LIBJPEG=OFF -DFLTK_USE_SYSTEM_ZLIB=OFF -DFLTK_USE_SYSTEM_LIBPNG=OFF `
        -DFL_ABI_VERSION=10403
    if ($LASTEXITCODE -ne 0) { throw 'FLTK: configure failed' }
    cmake --build "$fltkBuild\build" -- -k 0
    if ($LASTEXITCODE -ne 0) { throw 'FLTK: build failed' }
    cmake --install "$fltkBuild\build"
    if ($LASTEXITCODE -ne 0) { throw 'FLTK: install failed' }
}

if ($Stage -in 'vsp', 'all') {
    $libs = (Resolve-Path $BuildDir).Path -replace '\\', '/'
    cmake -S "$root\src" -B $VspBuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release "-DVSP_LIBRARY_PATH=$libs" `
        -DVSP_NO_GRAPHICS=OFF "-DFLTK_INSTALL_DIR=$libs/FLTK-prefix" `
        -DVSP_NO_API_WRAPPERS=ON -DVSP_NO_VSPAERO=OFF -DVSP_NO_VSPAERO_OPT=ON -DVSP_NO_HELP=ON -DVSP_NO_DOC=ON `
        -DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=ON # VSPAERO single-threaded (Emscripten's OpenMP pulls in debug libraries)
    if ($LASTEXITCODE -ne 0) { throw 'OpenVSP: configure failed' }
    cmake --build $VspBuildDir --target @Targets -- -k 0
    if ($LASTEXITCODE -ne 0) { throw 'OpenVSP: build failed' }
    if ($Targets -contains 'vsp_wasm') {
        New-Item -ItemType Directory -Force $PublicDir | Out-Null
        Copy-Item "$VspBuildDir\wasm_engine\vsp.js", "$VspBuildDir\wasm_engine\vsp.wasm" $PublicDir -Force
        "engine module copied to $(Resolve-Path $PublicDir)"
    }
    # VSPAERO's programs (run by the page in workers, see src/util/WebProcess.h).
    $programs = @{ vspaero = 'vsp_aero\Solver'; vsploads = 'vsp_aero\Adb2Load' }
    foreach ($name in $programs.Keys) {
        if ($Targets -contains $name) {
            New-Item -ItemType Directory -Force $PublicDir | Out-Null
            $dir = Join-Path $VspBuildDir $programs[$name]
            Copy-Item "$dir\$name.js", "$dir\$name.wasm" $PublicDir -Force
            "$name copied to $(Resolve-Path $PublicDir)"
        }
    }
}
