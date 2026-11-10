# ClassicBoy - compilacion con MinGW-w64 y la SDL2 incluida.
#
# Uso:
#   .\build.ps1              Compila la aplicacion y las pruebas.
#   .\build.ps1 app          Compila solo la aplicacion.
#   .\build.ps1 tests        Compila solo las pruebas.
#   .\build.ps1 all -Clean   Limpia y vuelve a compilar todo.

[CmdletBinding()]
param(
    [ValidateSet("all", "app", "tests")]
    [string]$Target = "all",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

$BuildDir = Join-Path $Root "build"
$OutDir = Join-Path $Root "output"
$SdlDir = Join-Path $Root "third_party\SDL2-2.32.10\x86_64-w64-mingw32"
$SdlGcc = $SdlDir.Replace('\', '/')

if (-not (Get-Command gcc.exe -ErrorAction SilentlyContinue)) {
    throw "No se encontro gcc.exe. Instala MinGW-w64 y anade su carpeta bin al PATH."
}
if (-not (Test-Path (Join-Path $SdlDir "include\SDL2\SDL.h"))) {
    throw "No se encontro la SDL2 incluida en third_party\SDL2-2.32.10."
}

if ($Clean) {
    Write-Host "Limpiando artefactos de compilacion..."
    Remove-Item $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $OutDir "classicboy.exe") -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $OutDir "SDL2.dll") -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $OutDir "assets") -Recurse -Force -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Force $BuildDir | Out-Null
New-Item -ItemType Directory -Force $OutDir | Out-Null

$Cflags = @(
    "-std=c11",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Iinclude",
    "-Ifrontend",
    "-Ithird_party/stb",
    "-I$SdlGcc/include",
    "-I$SdlGcc/include/SDL2"
)

$CoreSources = @(
    "src/apu.c",
    "src/cart.c",
    "src/common.c",
    "src/cpu.c",
    "src/gb.c",
    "src/joypad.c",
    "src/mmu.c",
    "src/ppu.c",
    "src/timer.c"
)

$FrontendSources = @(
    "frontend/main.c",
    "frontend/pages.c",
    "frontend/roms.c",
    "frontend/ui.c",
    "frontend/font.c",
    "frontend/stb_truetype_impl.c"
)

$TestSources = @(
    "tests/test_main.c",
    "tests/test_util.c",
    "tests/test_cpu.c",
    "tests/test_memory.c",
    "tests/test_ppu.c",
    "tests/test_apu.c",
    "tests/test_cart.c",
    "tests/test_cgb.c",
    "tests/test_integration.c",
    "tests/test_stability.c"
)

function Invoke-Gcc {
    param([string[]]$GccArgs)
    & gcc.exe @GccArgs
    if ($LASTEXITCODE -ne 0) {
        throw "gcc fallo con el codigo de salida $LASTEXITCODE."
    }
}

if ($Target -eq "all" -or $Target -eq "app") {
    Write-Host "Compilando ClassicBoy..."
    $AppExe = (Join-Path $OutDir "classicboy.exe").Replace('\', '/')
    $AppArgs = $Cflags + $FrontendSources + $CoreSources + @(
        "-o", $AppExe,
        "-L$SdlGcc/lib",
        "-lmingw32",
        "-lSDL2main",
        "-lSDL2",
        "-lm",
        "-mwindows"
    )
    Invoke-Gcc $AppArgs

    Copy-Item (Join-Path $SdlDir "bin\SDL2.dll") $OutDir -Force
    $OutAssets = Join-Path $OutDir "assets"
    if (Test-Path $OutAssets) {
        Remove-Item $OutAssets -Recurse -Force
    }
    Copy-Item (Join-Path $Root "assets") $OutAssets -Recurse
    Write-Host "Aplicacion: output\classicboy.exe"
}

if ($Target -eq "all" -or $Target -eq "tests") {
    Write-Host "Compilando pruebas del nucleo..."
    $TestExe = (Join-Path $BuildDir "classicboy_tests.exe").Replace('\', '/')
    $TestArgs = $Cflags + $CoreSources + $TestSources + @(
        "-o", $TestExe,
        "-lm"
    )
    Invoke-Gcc $TestArgs
    Write-Host "Pruebas: build\classicboy_tests.exe"
}

Write-Host "Compilacion completada."
