# ClassicBoy

ClassicBoy es un emulador de **Game Boy Color (CGB)** escrito en C11, con una
interfaz SDL2 y compatibilidad con juegos clásicos de Game Boy (DMG). El
proyecto nació como ejercicio de aprendizaje para explorar cómo se conectan
la CPU, la memoria, los cartuchos y los dispositivos de audio y vídeo de una
consola portátil.

Los cartuchos DMG se ejecutan en modo de compatibilidad con una paleta de
cuatro tonos configurable. Los cartuchos CGB usan el hardware de color nativo:
paletas de 15 bits, dos bancos de VRAM, ocho bancos de WRAM, atributos por
tile, prioridades, volteos y modo de doble velocidad.

## Características

- CPU SM83 completa, interrupciones, temporizadores y joypad.
- PPU con salida RGB555 y soporte CGB:
  - dos bancos de VRAM (`VBK`);
  - ocho bancos de WRAM (`SVBK`);
  - paletas de fondo y sprites (`BCPS/BCPD`, `OCPS/OCPD`);
  - atributos por tile: banco, paleta, prioridad, volteo X/Y;
  - prioridad de sprites configurable (`OPRI`);
  - transferencias GDMA y HDMA por líneas (`FF51`-`FF55`);
  - conmutación de doble velocidad mediante `KEY1` y `STOP`.
- Modo de compatibilidad DMG con cuatro paletas de tinte.
- APU de cuatro canales con salida estéreo a 48 kHz.
- Cartuchos ROM, MBC1, MBC2, MBC3 (con RTC) y MBC5 (incluido rumble).
- Guardado de RAM de cartucho con batería (`.sav`) y RTC (`.rtc`).
- Interfaz SDL2 con biblioteca de ROMs, ajustes, controles, capturas,
  pantalla completa, avance rápido y soporte de mandos.

## Requisitos

Para compilar directamente con GCC en Windows:

- Windows 10/11 de 64 bits.
- MinGW-w64 con `gcc.exe` en el `PATH`. El proyecto se ha probado con
  TDM-GCC 10.3.0.
- PowerShell 5 o posterior.
- SDL2 2.32.10, ya incluida en
  `third_party/SDL2-2.32.10/x86_64-w64-mingw32`.

Para usar CMake hace falta además CMake 3.20 o posterior. Si se usa el
generador `MinGW Makefiles`, también debe estar disponible `mingw32-make`.

El núcleo no necesita SDL2; solo la interfaz gráfica lo utiliza.

## Estructura del proyecto

```text
include/classicboy/   Cabeceras del nucleo (CPU, PPU, MMU, APU, cartuchos...)
src/                  Implementacion del nucleo en C11
frontend/             Interfaz SDL2, biblioteca de ROMs y dibujo de la UI
tests/                Pruebas del nucleo, integracion y estabilidad
third_party/          SDL2 y stb_truetype incluidos
assets/fonts/         Tipografias Lato
output/               Aplicacion compilada, DLL y ROMs de prueba
build/                Pruebas y artefactos de compilacion
```

## Compilación directa con GCC

Abre PowerShell en la raíz del proyecto. Primero crea las carpetas de salida:

```powershell
New-Item -ItemType Directory -Force build, output | Out-Null
```

Guarda la ruta de la SDL2 incluida y compila la aplicación:

```powershell
$sdl = "third_party/SDL2-2.32.10/x86_64-w64-mingw32"

gcc -std=c11 -O2 -Wall -Wextra `
  -Iinclude -Ifrontend -Ithird_party/stb `
  -I"$sdl/include" -I"$sdl/include/SDL2" `
  frontend/main.c frontend/pages.c frontend/roms.c frontend/ui.c `
  frontend/font.c frontend/stb_truetype_impl.c `
  src/apu.c src/cart.c src/common.c src/cpu.c src/gb.c src/joypad.c `
  src/mmu.c src/ppu.c src/timer.c `
  -o output/classicboy.exe `
  -L"$sdl/lib" -lmingw32 -lSDL2main -lSDL2 -lm -mwindows
```

Copia la DLL y los recursos junto al ejecutable:

```powershell
Copy-Item "$sdl/bin/SDL2.dll" output\ -Force
if (Test-Path output\assets) { Remove-Item output\assets -Recurse -Force }
Copy-Item assets output\assets -Recurse
```

Para compilar las pruebas del núcleo:

```powershell
gcc -std=c11 -O2 -Wall -Wextra -Iinclude `
  src/apu.c src/cart.c src/common.c src/cpu.c src/gb.c src/joypad.c `
  src/mmu.c src/ppu.c src/timer.c `
  tests/test_main.c tests/test_util.c tests/test_cpu.c tests/test_memory.c `
  tests/test_ppu.c tests/test_apu.c tests/test_cart.c tests/test_cgb.c `
  tests/test_integration.c tests/test_stability.c `
  -o build/classicboy_tests.exe -lm
```

Ejecuta la suite con:

```powershell
.\build\classicboy_tests.exe
```

La opción `-mwindows` crea una aplicación sin ventana de consola. Si quitas
esa opción al compilar la aplicación, podrás ver en la consola la salida de
los modos de prueba (`--frames`, `--screenshot`, `--capture` y `--headless`).

## Compilación con `build.ps1`

El script `build.ps1` automatiza la compilación, copia `SDL2.dll` y los
recursos, y deja las pruebas en `build\`.

```powershell
# Compila la aplicacion y las pruebas
.\build.ps1 all

# Solo la aplicacion
.\build.ps1 app

# Solo las pruebas
.\build.ps1 tests

# Limpia los artefactos y recompila todo
.\build.ps1 all -Clean
```

Si PowerShell bloquea la ejecución de scripts:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 all
```

Resultados:

- Aplicación: `output\classicboy.exe`
- Pruebas: `build\classicboy_tests.exe`
- SDL2 y recursos: copiados automáticamente a `output\`

`-Clean` elimina `build\`, el ejecutable, la DLL y los recursos copiados en
`output\`, pero no borra `output\roms`, `output\screenshots` ni la
configuración.

## Compilación con CMake

El proyecto también incluye un `CMakeLists.txt` que usa la SDL2 incluida si no
encuentra una instalación del sistema.

```powershell
cmake -S . -B build-cmake -G "MinGW Makefiles"
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
```

El ejecutable queda en `build-cmake\bin\classicboy.exe`. CMake copia
`SDL2.dll` y la carpeta `assets` junto al ejecutable. Si prefieres Ninja,
cambia el generador por `-G Ninja`.

## Ejecución

Puedes abrir `output\classicboy.exe` desde el explorador o lanzarlo desde
PowerShell:

```powershell
.\output\classicboy.exe
```

La biblioteca busca ROMs en la carpeta `roms\` situada junto al ejecutable.
Con la compilación directa o con `build.ps1`, eso significa `output\roms\`.
También puedes abrir una ROM con el botón **Abrir ROM**, arrastrarla sobre la
ventana o indicarla por línea de comandos. Al iniciar un juego, la biblioteca
permanece en la ventana principal y la partida se muestra en una ventana
independiente que puedes redimensionar; usa **F11** para poner el juego en
pantalla completa y **F1** para volver a la biblioteca.

```powershell
.\output\classicboy.exe --rom "output\roms\juego.gbc"
```

Opciones disponibles:

```text
--rom RUTA       Carga una ROM al iniciar
--page NOMBRE    Pagina inicial: library, game, settings, controls o about
--paused         Inicia el juego en pausa
--frames N       Ejecuta N fotogramas y sale (modo de prueba)
--capture RUTA   Guarda la interfaz renderizada como BMP
--screenshot     Guarda una captura 4x del juego (modo de prueba)
--headless       Usa los controladores dummy de SDL (sin ventana)
--help           Muestra la ayuda
```

Las partidas de cartuchos con batería se guardan en un archivo `.sav` junto a
la ROM. Los cartuchos MBC3 con RTC guardan el reloj en un archivo `.rtc`.
También puedes forzar el guardado con `F5`.

## Controles

| Acción | Teclas |
| --- | --- |
| Direcciones | Flechas |
| A | Z o J |
| B | X o K |
| Start | Enter |
| Select | Shift derecho o Backspace |
| Pausa | Esc o P |
| Avance rápido | Espacio (mantener) |
| Captura del juego | F12 |
| Pantalla completa | F11 |
| Biblioteca | F1 |
| Guardar batería | F5 |

Con mando se usa la cruceta, los botones A/B, Start y Back. Los mandos
XInput y DirectInput se detectan automáticamente.

## Ajustes

- **Paleta DMG:** solo afecta a los juegos DMG. Los juegos CGB muestran sus
  propios colores de 15 bits y no usan esta paleta.
- **Escalado entero:** mantiene los píxeles nítidos al ampliar la pantalla.
- **Contador de FPS:** muestra la velocidad de la interfaz.
- **Volumen:** controla la salida de audio.
- La configuración se guarda automáticamente en `classicboy.cfg`.

## Pruebas

La suite actual contiene **53 suites** y **396 825 comprobaciones**. Cubre:

- CPU, ALU, rotaciones, pila, interrupciones y ciclos.
- Mapa de memoria, DMA, VRAM, OAM, temporizadores y joypad.
- PPU DMG y CGB: timing, fondo, ventana, sprites, paletas, atributos,
  volteos y prioridades.
- Bancos de WRAM y VRAM, paletas CGB, doble velocidad, GDMA y HDMA.
- Cartuchos ROM, MBC1, MBC2, MBC3 con RTC, MBC5 y MBC5 con rumble.
- Integración, audio, estabilidad, fuzz de CPU/ROM y fugas de memoria con
  canarios propios.

## Compatibilidad y limitaciones

El emulador reconoce cartuchos sin mapper (ROM), MBC1, MBC2, MBC3 (incluido
RTC) y MBC5 (incluido rumble). El tamaño máximo de ROM admitido es de 8 MB.

La emulación CGB incluye los registros y el comportamiento principal de la
consola, pero no pretende ser perfecta a nivel de ciclos. Algunos detalles
avanzados de temporización, audio o vídeo pueden diferir del hardware real.
Tampoco se emulan Super Game Boy, cable de enlace, infrarrojos, cámara,
sensor de inclinación ni la BIOS de arranque; los registros posteriores al
arranque se inicializan por software.

Los juegos que dependen de hardware no implementado o de una precisión de
temporización muy estricta pueden presentar fallos. Usa únicamente copias de
ROM de las que tengas derecho de uso. ClassicBoy no incluye ROMs ni BIOS.

## Cómo se hizo

La mayor parte del código fue escrita a mano. Se utilizó DeepSeek 4.1-Flash
como apoyo durante el desarrollo, por ejemplo para investigar conceptos,
comparar soluciones y revisar problemas; la implementación e integración del
emulador son principalmente trabajo propio.

El núcleo está separado de la interfaz para poder probarlo sin SDL2. Las
pruebas automatizadas viven en `tests/` y se ejecutan con el mismo compilador
que la aplicación.
