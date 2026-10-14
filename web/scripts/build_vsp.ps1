# Builds the headless OpenVSP Python API into build/ and installs it, plus vspweb, into web/.venv.
# Requires: Visual Studio 2022 (C++), CMake >= 3.24, SWIG, Python 3.10+, Node LTS.
$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot\..\.."
Set-Location $root

if (-not (Test-Path web\.venv)) { python -m venv web\.venv }
$py = "$root\web\.venv\Scripts\python.exe"
& $py -m pip install --upgrade pip numpy

$swig = (Get-Command swig -ErrorAction SilentlyContinue).Source
if (-not $swig) {
    $swig = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter swig.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $swig) { throw 'SWIG not found (winget install SWIG.SWIG)' }

# GUI build: the web GUI mirrors OpenVSP's own FLTK screens (see src/gui_and_draw/WebMirror.*).
cmake -A x64 -S Libraries -B build/libs-gui -DCMAKE_BUILD_TYPE=Release
cmake --build build/libs-gui --config Release --parallel

cmake -A x64 -S src -B build/vsp-gui -DCMAKE_BUILD_TYPE=Release "-DVSP_LIBRARY_PATH=$root/build/libs-gui" `
    -DVSP_NO_PYDOC=ON -DVSP_NO_HELP=ON -DVSP_NO_DOC=ON `
    "-DSWIG_EXECUTABLE=$swig" "-DPython3_EXECUTABLE=$py"
cmake --build build/vsp-gui --config Release --parallel --target pyvsp_g pyvsp
cmake --build build/vsp-gui --config Release --target copy_package

$pp = "$root\build\vsp-gui\python_pseudo"
& $py -m pip install -e "$pp\openvsp_config" -e "$pp\degen_geom" -e "$pp\utilities" -e "$pp\openvsp"
& $py -m pip install -e "$root\web\backend[dev]"

Push-Location web\frontend
npm install
npm run build
Pop-Location

& $py -c "import openvsp as vsp; print(vsp.GetVSPVersion())"
