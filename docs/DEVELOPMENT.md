# Desarrollo de tg

Este documento recoge las premisas de trabajo del proyecto, de forma que todo
el trabajo se haga de manera consistente y verificable.

## Premisas de trabajo

1. **Cada feature se desarrolla en su propia rama.**
   - Toda rama feature parte de `master`.
   - Convención de nombre: `feature/<nombre>` (p. ej. `feature/recording-offline`).
2. **Commits pequeños y frecuentes**, cada uno con un propósito claro y
   autónomo. Mensaje de commit descriptivo (ver convención abajo).
3. **TDD siempre que sea posible**: escribir el test antes que la
   implementación y verificar que falla, luego implementar y verificar que pasa.
4. **Verificación obligatoria antes de integrar**:
   - `make test` (smoke test) y la suite de tests disponible.
   - Compilar sin warnings en la medida de lo posible (`-Wall -Wextra` ya
     activos en `configure.ac`).
   - Si aplica, `make valgrind` para detectar fugas.
5. **Integración a `master`** una vez verificada la rama, mediante merge.
   La rama feature se elimina tras integrarse.
6. **Push a `origin`** solo tras la integración en `master`.
7. **Documentación**: cualquier cambio relevante se refleja en `docs/`.

## Convención de mensajes de commit

Se sigue el estilo ya presente en el historial del proyecto:

- `Version x.y.z` — cortes de versión.
- Descripciones concretas del cambio (p. ej. `Add input device selection`,
  `Fix segfault in handle_center_trace`).
- Sin sufijos tipo `[skip ci]` salvo necesidad real.

## Flujo típico por feature

```sh
git checkout master
git pull origin master
git checkout -b feature/<nombre>
# ... implementar con TDD, commits pequeños ...
make test          # verificar
git checkout master
git merge feature/<nombre>
git push origin master
git branch -d feature/<nombre>
```

## Estados del repo

- La rama por defecto del proyecto es `master` (no existe `main`).
- `origin` apunta al fork propio
  (`git@github.com:jnarvaezp/tg-mod.git`) — ahí se hace el push.
- `upstream` apunta al repositorio público de tg
  (`git@github.com:vacaboja/tg.git`) — de ahí se hace el pull/fetch.

## Mapa de módulos (src/)

| Módulo | Responsabilidad | Dependencias |
|---|---|---|
| `algo.c` | DSP: filtros, autocorrelación, periodo, rate/be/amp, calibración | FFTW |
| `audio.c` | Captura PortAudio, ring buffer, file source (offline), grabación WAV, nivel | PortAudio, wav |
| `computer.c` | Hilo de cómputo, snapshots, eventos | — |
| `interface.c` | UI GTK, menús, modos, CLI (`debug`, `--help`, `analyze`, `test`) | GTK |
| `output_panel.c` | Paneles Cairo: lectura, paperstrip, formas de onda, espectro, tendencia | Cairo |
| `config.c` | Configuración INI | GLib |
| `serializer.c` | Formato `.tgj` | — |
| `offline.c` | Análisis headless (`analyze_audio_file`) | algo, wav |
| `wav.c` | Lector/escritor WAV (sin dependencias) | — |
| `session.c` | Session log: anillo de ciclos + raw (JSON/CSV/raw) | pthread |
| `stats.c` | Anillo de estadísticas en vivo + resumen por posición | pthread |
| `report.c` | Informe por posición (CSV + PDF Cairo) | stats, cairo |

## Guías

- `docs/USAGE.md` — guía de uso completa (teoría e interpretación de valores).
- `docs/USAGE.en.md` — misma guía en inglés.
- `docs/superpowers/plans/` — planes de implementación por feature.
- `docs/superpowers/specs/` — especificaciones de diseño.
