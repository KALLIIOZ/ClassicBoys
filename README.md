# ClassicBoys 🎮

**ClassicBoys** es un emulador ligero y modular de Nintendo Game Boy (DMG/CGB) escrito en **C / C++** utilizando **CMake** como sistema de construcción y la librería **SDL2** para la renderización de video, procesamiento de audio e interfaz de usuario.

El proyecto está diseñado enfocado en la precisión de emulación a bajo nivel, modularidad del código fuente y facilidad de compilación multiplataforma.

---

## 🚀 Características

- **Emulación de CPU**: Implementación completa del procesador Sharp LR35902 (opcodes, flags, registros e interrupciones).
- **Procesamiento de Video (PPU)**: Renderizado por ciclos/líneas de comandos para emulación precisa de gráficos.
- **Unidad de Sonido (APU)**: Soporte para síntesis de audio de canales de onda/pulso.
- **Gestión de Memoria (MMU & Cartridge)**: Manejo del mapa de memoria (`0x0000 - 0xFFFF`), HRAM, VRAM, WRAM y soporte para Mappers/MBCs de cartuchos.
- **Interfaz de Usuario (Frontend)**: 
  - Selección de ROMs e interfaz gráfica limpia.
  - Gestión de controles y mapeo de Gamepad/Teclado.
  - Menú de pausa y configuración gráfica.

---

## 🛠️ Requisitos del Sistema

Para compilar e ejecutar el proyecto necesitas:

- **Compilador C/C++**: GCC, Clang o MSVC (Soporte para C11 / C++17).
- **CMake**: Versión 3.10 o superior.
- **SDL2**: (Incluido en `third_party/SDL2` o instalado en el sistema).

---

## 🔨 Compilación e Instalación

### En Windows (PowerShell)

Puedes utilizar el script de compilación automatizado incluido en la raíz del proyecto:

```powershell
.\build.ps1
O compilar manualmente usando CMake:

PowerShell
mkdir build
cd build
cmake ..
cmake --build . --config Release
En Linux / macOS
Bash
mkdir build && cd build
cmake ..
make -j$(nproc)
El ejecutable generado se encontrará en la carpeta build/ u output/.

📁 Estructura del Proyecto
Plaintext
ClassicBoys/
├── assets/          # Fuentes (Lato), iconos y recursos gráficos
├── frontend/        # Interfaz de usuario (UI, páginas, fuentes, captura de eventos)
├── include/         # Cabeceras de los módulos centrales del emulador
│   └── classicboy/  # cpu.h, ppu.h, apu.h, mmu.h, cart.h, timer.h, joypad.h
├── src/             # Implementación del núcleo del emulador (C/C++)
├── tests/           # Batería de pruebas unitarias y de integración
├── third_party/     # Librerías externas (SDL2)
├── CMakeLists.txt   # Configuración principal de CMake
└── build.ps1        # Script auxiliar de compilación para Windows
🧪 Pruebas Unitarias
El proyecto cuenta con un conjunto de pruebas para validar la estabilidad y la precisión del procesador y componentes:

Bash
cd build
ctest --output-on-failure