# Arquitectura de Ana-Trans

## Propósito

Ana-Trans transcribe audio y video a texto. El pipeline es siempre el mismo:

```
entrada (audio/video)
      │  ffmpeg        (conversión)
      ▼
WAV PCM16 mono 16 kHz   (carpeta temporal, se borra al terminar)
      │  whisper.cpp   (transcripción)
      ▼
TranscriptDocument      (segmentos: texto + tiempos)
      │  FormatWriter  (txt / markdown / latex / docx / pdf)
      ▼
<nombre>_transcripcion.<ext>   (en la carpeta de salida)
```

Se usa igual desde la **GUI GTK** y desde la **CLI**; cambia solo quién
reúne los archivos de entrada y quién muestra el resultado.

## Mapa de módulos

```
                   Entrada (carpeta / archivos sueltos)
                               │
                               ▼
   ┌──────────────────────────────────────────────────────────┐
   │  main.c (arranque)                                       │
   │   • sin argumentos → GUI (gui.c)                         │
   │   • con rutas      → CLI (batch.c + process.c)           │
   └───────────────────────────────┬──────────────────────────┘
                                   │
   ┌───────────────────────────────▼──────────────────────────┐
   │  gui.c — ventana GTK4, pestañas y widgets                │
   │   • construye el ControllerJob con lo elegido            │
   │   • pinta los eventos del motor (progreso, log)          │
   │   • NO ejecuta ffmpeg ni whisper                         │
   └───────────────────────────────┬──────────────────────────┘
                                   │ controller_start (hilo aparte)
   ┌───────────────────────────────▼──────────────────────────┐
   │  controller.c — motor asíncrono (sin dependencia de GTK) │
   │   • lanza process_run_batch en un GThread                │
   │   • devuelve eventos a la GUI vía g_idle_add             │
   │   • también lanza la descarga de modelos                 │
   └───────────────────────────────┬──────────────────────────┘
                                   │ process_run_batch
   ┌───────────────────────────────▼──────────────────────────┐
   │  process.c — el lote, archivo por archivo:               │
   │   1) audio.c: ffmpeg → WAV (en dir temporal de cleanup)  │
   │   2) transcribe.c: whisper → TranscriptDocument          │
   │   3) fmt_*.c: escribe el archivo de salida               │
   └──────────────────────────────────────────────────────────┘
```

## Módulos de apoyo

| Módulo | Responsabilidad | Lo usa |
|---|---|---|
| `batch.c` | Enumerar/filtrar/ordenar archivos de audio/video | `main.c`, `gui.c` |
| `config.c` | Preferencias: env > `config.ini` > valor por defecto | casi todos |
| `audio.c` | Conversión a WAV con ffmpeg | `process.c` |
| `transcribe.c` | Carga el modelo whisper y transcribe un WAV | `process.c` |
| `formats.c` | Registro de formatos y escritura de archivos | todos |
| `fmt_*.c` | Un escritor de formato cada uno | `formats.c` |
| `models.c` | Catálogo de modelos del modo sencillo + descarga | `gui.c`, `controller.c`, `config.c` |
| `cleanup.c` | Carpeta temporal de WAV + limpieza ante señales | `process.c` |
| `i18n.c` | Internacionalización (gettext): idioma activo, cambio en caliente, RTL | `gui.c` |

## Modos de modelo

- **Modo personalizado** (`model-mode=custom`): el usuario indica la ruta a un
  `.bin` de whisper.cpp (campo de la GUI o `WHISPER_MODEL`).
- **Modo sencillo** (`model-mode=easy`, el **valor por defecto**): hay 3
  modelos conocidos en `models.c` (Preciso/Equilibrado/Rápido). Se descargan
  automáticamente los que falten (guarda en `g_get_user_data_dir()/anatrans/models`)
  y la GUI muestra su estado.

## Configuración

`config.c` resuelve cada valor en este orden:

1. **Variable de entorno** (override explícito, útil en CLI).
2. **Archivo de preferencias** del usuario
   (`$XDG_CONFIG_HOME/anatrans/config.ini`, p. ej. `~/.config/anatrans/`).
3. **Valor por defecto** del código.

La GUI guarda con *debounce*: al cambiar una preferencia se programa un
`g_timeout_add` (400 ms) que llama a `config_save()` si no se vuelve a
cambiar. No hay botón "Guardar".

## Concurrencia

- El **motor** (`controller.c`) corre en un `GThread`. Nunca toca widgets:
  solo publica eventos.
- Los eventos se entregan a la GUI en el **hilo del main loop** mediante
  `g_idle_add`; el callback de la GUI solo pinta.
- La **limpieza** (`cleanup.c`) instala manejadores de `SIGINT`/`SIGTERM`/
  `SIGHUP` y `atexit` para borrar los WAV temporales si el programa muere a
  mitad de un lote. Sus handlers usan solo operaciones async-signal-safe.

## Decisiones de diseño clave

1. **`controller.c` no depende de GTK**: el trabajo pesado es agnóstico de la
   interfaz; la GUI es solo una "pantalla" de eventos.
2. **WAV intermedios en carpeta temporal** (`cleanup.c`): la carpeta de salida
   del usuario solo recibe los documentos finales.
3. **El modelo whisper se carga una vez por lote** y se reutiliza en todos los
   archivos (aunque ahorrar memoria sea relevante, evita el coste de carga).
4. **`GDK_BACKEND=x11` forzado** en `main.c`: el GTK compilado por vcpkg solo
   trae el backend X11 (sin Wayland), y sin esta línea la app no abre en
   sesiones Wayland.
