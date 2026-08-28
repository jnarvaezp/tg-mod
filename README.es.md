# tg — Programa para cronometrar relojes mecánicos

[![Build Status](https://travis-ci.org/vacaboja/tg.svg?branch=master)](https://travis-ci.org/vacaboja/tg)

**Tg** es un cronocomparador (timegrapher) para relojes mecánicos: escucha el
ruido del mecanismo con un micrófono y produce lecturas en tiempo real del
**rate** (precisión), **beat error**, **amplitud** y **frecuencia (BPH)**, con
cinta de papel (paperstrip), formas de onda, espectro en vivo, estadísticas e
informe por posición.

## Créditos y licencia

El programa se distribuye bajo la licencia GNU GPL versión 2 (ver
[LICENSE](LICENSE)). El código fuente completo y su copyright pertenecen a
los respectivos contribuidores.

- **Proyecto original:** [vacaboja/tg](https://github.com/vacaboja/tg), de
  **Marcello Mamino** y contribuidores. Esta fork conserva intactos todo el
  código y los créditos originales.
- **Mejoras algorítmicas portadas de:**
  [xyzzy42/tg](https://github.com/xyzzy42/tg) por **Trent Piepho** (GPL-2):
  soporte de BPH bajos (8100+), fix de sigma, paperstrip v2 (zoom,
  timestamps, colores, overlay de amplitud) y el seguimiento tic/toc de
  eventos.
- **Fork mantenida por:** Jonathan Narvaez (jnarvaezp) —
  [github.com/jnarvaezp/tg-mod](https://github.com/jnarvaezp/tg-mod).

Parte de la información original se encuentra en este
[hilo de WUS](http://forums.watchuseek.com/f6/open-source-timing-software-2542874.html);
en particular el procedimiento de calibración está descrito en
[este mensaje](http://forums.watchuseek.com/f6/open-source-timing-software-2542874-post29970370.html).

## Documentación

- **docs/USAGE.md** — guía completa (español): teoría de los valores medidos,
  cómo interpretar los resultados y cómo configurar el micrófono.
- **docs/USAGE.en.md** — la misma guía en inglés.
- **README.md** — este readme en inglés.
- **docs/ROADMAP.md** — hoja de ruta de desarrollo de la fork.
- **docs/tg-timer.1** — página de manual.

## Características

### Originales

- Mediciones en tiempo real: rate (s/d), beat error (ms), amplitud (grados),
  BPH.
- Paperstrip, formas de onda tic/toc y periodo, calibración automática
  (~15 min), snapshots en pestañas, guardado/carga `.tgj`, configuración INI.

### Añadidas en esta fork

- **Grabación y análisis offline**: graba el micrófono a un WAV
  (*Record to file...*) y analiza WAV existentes sin audio en vivo
  (*Open recording...*); headless `tg-timer-dbg analyze archivo.wav`.
- **Session log**: *Save session log* escribe los ciclos de cómputo en
  `~/tg-logs/tg-session-<timestamp>.{json,csv,raw}`; `tg-timer debug` /
  `debug full` activan la salida verbosa de consola en cualquier build.
- **Calidad de señal**: control de ganancia 0.1–100 (atenuación y
  amplificación), medidor de nivel en vivo con indicador **CLIP**, **botón Auto** de ganancia y SNR del tic en dB, selección
  robusta de dispositivo (ignora el sufijo ALSA `(hw:X,Y)`, botón de refresco,
  aviso visible de fallback).
- **Estadísticas en vivo**: gráfico de tendencia del rate (últimos ~5 min)
  con media, desviación, mín y máx en s/d.
- **Posiciones e informe**: selector "pos" para etiquetar las mediciones
  (dial up/down, crown up/down/left/right); *Export report...* escribe un
  resumen por posición en `~/tg-logs/tg-report-<timestamp>.{csv,pdf}`.
- **Base de datos de relojes**: cuaderno del relojero — registro persistente
  de relojes e historial de sesiones en SQLite (`~/tg-data/tg.db`). El panel
  izquierdo permite crear relojes, grabar sesiones de medición (etiquetadas
  con posición y configuración), consultar el historial por reloj y borrar
  sesiones individuales.
- **Suite de pruebas**: `make check` ejecuta 7 módulos de test (WAV, session
  log, regresión DSP, estadísticas, informe, JSON, base de datos).
- **Suite de pruebas**: `make check` (módulo WAV, session log, regresión DSP
  con clips sintéticos, módulos de estadísticas e informe).

## Inicio rápido

1. Ejecuta `tg-timer`.
2. Selecciona tu dispositivo de entrada en el selector **mic** (usa el botón
   ↻ para refrescar la lista tras conectar un micrófono USB).
3. Apoya el reloj sobre el micrófono del cronocomparador (la caja o la corona
   contra la cavidad del sensor) y asegúrate de que el reloj tenga cuerda.
4. Ajusta **gain** para que la barra de nivel muestre un pico ~0.6–0.9 sin que
   aparezca el indicador **CLIP**.
5. Selecciona el **BPH** (o "guess"), el **lift angle** y la posición
   (selector **pos**) para la medición.
6. Lee los resultados: rate (s/d), beat error (ms), amplitud (grados). Ver
   docs/USAGE.md para interpretarlos.

## Línea de comandos

| Comando | Efecto |
|---|---|
| `tg-timer` | GUI |
| `tg-timer debug` | GUI + resumen por ciclo en consola |
| `tg-timer debug full` | GUI + diagnóstico DSP completo |
| `tg-timer -h` / `--help` | Ayuda |
| `tg-timer-dbg analyze <archivo.wav>` | Análisis headless (build debug) |
| `tg-timer-dbg test` | Smoke test GUI de 3 s (build debug) |

## Instalación

Tg funciona en Microsoft Windows, OS X y Linux, y debería poder compilarse en
la mayoría de sistemas tipo UNIX. Ver las sub-secciones siguientes.

### Windows

Binarios en https://tg.ciovil.li

### Macintosh

El usuario de GitHub [dmnc](https://github.com/dmnc) preparó una fórmula para
Homebrew. Primero instala Homebrew (instrucciones en http://brew.sh).

Después ejecuta lo siguiente para comprobar que todo está configurado:

	brew doctor

Para instalar tg:

	brew install dmnc/horology/tg

Puedes lanzar tg escribiendo:

	tg-timer &

### Debian o derivados (Mint, Ubuntu...)

Paquetes .deb en https://tg.ciovil.li

## Compilación desde el código fuente

El código de tg puede compilarse con casi cualquier compilador C99; solo se
han probado gcc y clang. Necesitas las siguientes bibliotecas: gtk+3,
portaudio2, fftw3, sqlite3 (todas de código abierto).

Build de release:
```sh
git clone https://github.com/vacaboja/tg.git
cd tg
./autogen.sh
./configure
make
```

Build de debug:
```sh
make tg-timer-dbg
```

### Compilación en Windows

Se recomienda la plataforma msys2. Primero instala msys2 según las
instrucciones de [http://www.msys2.org](http://www.msys2.org). Después:

```sh
pacman -S mingw-w64-x86_64-gcc make pkg-config mingw-w64-x86_64-gtk3 mingw-w64-x86_64-portaudio mingw-w64-x86_64-fftw mingw-w64-x86_64-sqlite3 git autoconf automake libtool
git clone https://github.com/vacaboja/tg.git
cd tg
./autogen.sh
./configure
make
```

### Compilación en Debian

```sh
sudo apt-get install libgtk-3-dev libjack-jackd2-dev portaudio19-dev libfftw3-dev libsqlite3-dev git autoconf automake libtool
git clone https://github.com/vacaboja/tg.git
cd tg
./autogen.sh
./configure
make
```

El paquete libjack-jackd2-dev no es necesario; solo evita un bug conocido
(https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=718221).

### Compilación en Fedora

```sh
sudo dnf install fftw-devel portaudio-devel gtk3-devel sqlite-devel autoconf automake libtool
git clone https://github.com/vacaboja/tg.git
cd tg
./autogen.sh
./configure
make
```

## Pruebas

- `make check` ejecuta los tests: módulo WAV, session log, regresión DSP
  (clips sintéticos a 12000–36000 BPH, con/sin ruido, beat error controlado),
  módulos de estadísticas e informe. Deja grabaciones reales en
  `tests/fixtures/*.wav` para incluirlas en la suite de regresión DSP (deben
  al menos detectar señal).