# Watch Database + Session History — Design Spec

Fecha: 2026-08-24. Rama de implementación prevista: `feature/watch-db` (almacenamiento) y `feature/watch-panel` (UI).

## Motivación

Hoy las mediciones "flotan": el panel muestra stats en vivo y el informe exporta por posición, pero no hay una estructura que agrupe las mediciones por reloj ni un historial que muestre la evolución de la regulación en el tiempo. El export vacío del informe (ciclos sin posición etiquetada descartados) fue el síntoma. Este feature añade el **cuaderno del relojero**: registrar relojes, grabar sesiones de medición con su configuración, y consultar/exportar el historial de cada reloj.

## Modelo de datos

```
~/tg-data/
  watches.json                     lista de relojes
  <watch-id>/
    sessions.json                  índice de sesiones del reloj
    <session-id>.json              resumen de una sesión
```

### watches.json
```json
{
  "watches": [
    { "id": "w1", "name": "Omega 565", "model": "Cal. 565",
      "notes": "rueda de balance dañada", "created_ms": 1724535312123 }
  ]
}
```

### <session-id>.json
```json
{
  "id": "s17",
  "watch_id": "w1",
  "position": 1,
  "note": "antes de regular",
  "start_ms": 1724535312123,
  "end_ms": 1724535372123,
  "n": 82,
  "mean": 385.882, "sigma": 14.290, "min": 343.167, "max": 400.647,
  "mean_be": 3.971, "mean_amp": 187.2,
  "config": { "bph": 21600, "lift_angle": 52, "cal": 0, "gain": 0.5, "cutoff": 3000 }
}
```

### Posiciones
Reutiliza `POSITION_*` de `src/stats.h` (0 = none, 1-6 = DU/DD/CU/CD/CL/CR) y `position_name()`.

## Módulos

### `src/json.c/.h` (nuevo, sin dependencias, con tests)
- Escritor JSON (objetos/arrays con ints, doubles, strings escapados).
- Lector JSON mínimo para nuestro esquema (parser recursivo limitado a objetos/arrays/valores; ~200 líneas). Tests en `tests/test_json.c`.

### `src/watchdb.c/.h` (nuevo, con tests)
- `watchdb_load(path)` / `watchdb_save(path)`.
- CRUD de relojes: `watchdb_add_watch`, `watchdb_rename_watch`, `watchdb_delete_watch`, `watchdb_list_watches`.
- CRUD de sesiones: `watchdb_add_session`, `watchdb_list_sessions(watch_id)`, `watchdb_delete_session`, `watchdb_session_summary(watch_id, session_id)`.
- Guardado atómico (escribir temp + rename). Tests en `tests/test_watchdb.c` (round-trip, CRUD, corrupción de archivo).

### Captura de sesión (interfaz con stats)
Al **finalizar** una sesión, el resumen se calcula con `stats_summary(0, &s)` (n, mean, sigma, min, max, mean_be, mean_amp) y la configuración actual (`w->bph, w->la, w->cal, w->gain, w->filter_cutoff`) se guarda en `config`.

## UI (panel izquierdo — `feature/watch-panel`)

- **Lista de relojes** (GtkListBox): crear ("New watch..."), renombrar, eliminar (con confirmación).
- **Detalles del reloj**: nombre, modelo, notas (editables).
- **Historial de sesiones** (GtkTreeView): columnas fecha, posición, n, media, σ, min, max, be, amp. Seleccionar una sesión muestra sus detalles.
- **Botones de sesión**: "Start session" / "Finish & save" (manual, con confirmación). La sesión viva se indica en la UI (label/estado).
- Al iniciar sesión con un reloj seleccionado y una posición del combo "pos", los ciclos en vivo se etiquetan con esa posición (ya existe `sp.position = w->position`).
- **Export historial**: CSV/PDF por reloj (reutiliza `report.c` con filas por sesión en vez de por posición).
- **Gráfico de evolución**: rate medio por sesión (línea) para el reloj seleccionado (patrón del trend chart existente).

## Integración con lo existente

- `stats` (anillo en vivo) → fuente del resumen de sesión.
- `report.c` → export CSV/PDF (extender para filas de sesión).
- `session.c` → no cambia (el session log sigue siendo el registro crudo de debug).
- `interface.c` → el panel izquierdo se añade en la ventana principal (GtkPaned: izquierda lista de relojes/sesiones, derecha el panel actual).

## Decisión de orden

1. Fix del informe vacío (fila "none" + Total) — rama `feature/report-fix` (pequeña).
2. `feature/watch-db` — json.c + watchdb.c + tests (sin UI).
3. `feature/watch-panel` — panel izquierdo + ciclo de sesión + historial/export.

## Fuera de alcance (v1)

- Múltiples dispositivos/posiciones simultáneos.
- Edición retroactiva de sesiones.
- Sincronización/backup en la nube.