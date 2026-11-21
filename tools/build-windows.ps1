# The Windows package built on Windows, from PowerShell in the repo's folder:
#   powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1
# The first time it downloads MSYS2 (the compiler and GTK 2) into build\msys64, inside the repo: nothing is
# installed on the system, and deleting build\ removes it all. Then tools/build-windows-native.sh builds the
# engine, gdk-pixbuf and the game in it. The result is dist\denboku-windows\ (start Denboku.exe) and
# dist\denboku-windows.zip.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path -Parent $PSScriptRoot
$bash = Join-Path $root 'build\msys64\usr\bin\bash.exe'
$env:MSYSTEM = 'MINGW64'
$env:CHERE_INVOKING = '1' # bash starts in the repo's folder
if (-not (Test-Path $bash)) {
    Write-Host 'Downloading MSYS2 into build\msys64...'
    New-Item -ItemType Directory -Force (Join-Path $root 'build') | Out-Null
    $sfx = Join-Path $env:TEMP 'msys2-base-x86_64.sfx.exe'
    Invoke-WebRequest -UseBasicParsing -OutFile $sfx `
        'https://github.com/msys2/msys2-installer/releases/download/nightly-x86_64/msys2-base-x86_64-latest.sfx.exe'
    & $sfx -y "-o$(Join-Path $root 'build')" | Out-Null
    Remove-Item $sfx
    Push-Location $root
    & $bash -l -c 'pacman-key --init && pacman-key --populate msys2 && pacman -Syuu --noconfirm && pacman -Syuu --noconfirm'
    Pop-Location
    if ($LASTEXITCODE) { throw 'MSYS2 could not be set up' }
}
Push-Location $root
& $bash -l tools/build-windows-native.sh
$code = $LASTEXITCODE
Pop-Location
exit $code
