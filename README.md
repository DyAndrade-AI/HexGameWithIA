# Hex con Monte Carlo

Implementación en C del juego Hex con un oponente controlado por simulaciones Monte Carlo y una interfaz dual: consola y GUI en Raylib. El jugador humano usa fichas `X` (conecta izquierda‑derecha) y la IA usa `O` (conecta arriba‑abajo). El programa reparte miles de simulaciones entre varios procesos hijo para evaluar cada jugada disponible.

## Características
- Motor de Hex que valida movimientos, calcula el estado del tablero y detecta ganadores.
- Oponente computacional basado en Monte Carlo con distribución adaptativa de simulaciones por movimiento.
- Ejecución en paralelo con procesos independientes para acelerar las estadísticas (`main.c`).
- Interfaz gráfica opcional en Raylib con tablero hexagonal y prompts interactivos (`ui.c`).
- Modo solo texto disponible en la terminal (`--no-gui`).

## Dependencias
- **Compilador C** compatible con C11 (Clang o GCC).
- **Raylib 4.x** para la interfaz gráfica.
  - macOS (Homebrew): `brew install raylib`
  - Linux: instala desde el gestor de paquetes o compila Raylib siguiendo la [documentación oficial](https://www.raylib.com/).
- **make** (opcional) si deseas crear un script de construcción; actualmente se compila con un comando manual.

El proyecto incluye `pcg_basic.c`/`.h` para la generación de números aleatorios, por lo que no necesitas dependencias extra para el motor de simulación.

## Compilación
El proyecto consta de los siguientes archivos fuente: `main.c`, `hex.c`, `ui.c` y `pcg_basic.c`. Compila todos apuntando a Raylib y las librerías del sistema correspondientes.

### macOS (Clang/Homebrew)
```bash
clang main.c hex.c ui.c pcg_basic.c -o hex \
  -I/usr/local/include -L/usr/local/lib \
  -lraylib -lm -lpthread -ldl \
  -framework OpenGL -framework Cocoa -framework IOKit
```

### Linux (GCC)
```bash
gcc main.c hex.c ui.c pcg_basic.c -o hex \
  -lraylib -lm -lpthread -ldl -lrt -lX11
```

Si Raylib está instalado en rutas no estándar, ajusta los flags `-I`/`-L`. Para depuración puedes añadir `-g -O0` y para optimización `-O2`.

## Ejecución
```bash
./hex [--gui | --no-gui]
```

- `--gui` fuerza el modo gráfico (es el valor por defecto si Raylib se inicializa correctamente).
- `--no-gui` ejecuta solo la interfaz de texto, útil si no tienes Raylib o trabajas por SSH.

Al iniciar, el programa solicita:
1. Tamaño del tablero (7–26).
2. Número total de simulaciones (100–1,000,000).
3. Cantidad de procesos de simulación (1–32) o auto‑detección.

Durante la partida:
- En GUI haz clic en una celda vacía para jugar `X`. Presiona `Q` o `Esc` para salir.
- En consola ingresa movimientos con formato `A1`, `C7`, etc. Usa `Q` para terminar.

El estado del juego se muestra tras cada jugada y al finalizar se indica quién conectó sus bordes.
