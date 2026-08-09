#ifndef BATCH_H
#define BATCH_H

#include <glib.h>

G_BEGIN_DECLS

/*
 * batch: infraestructura para trabajar con contenido de audio/video
 * por carpeta o por archivos sueltos. Esta fase NO procesa nada:
 * solo enumera y filtra los archivos.
 *
 * Extensiones reconocidas (audio/video):
 *   mp3 wav m4a mp4 mkv mov flac ogg opus webm aac avi wmv
 */

/* Devuelve TRUE si la ruta tiene una extensión de audio/video reconocida. */
gboolean  batch_is_media_path     (const char *path);

/*
 * Escanea la carpeta indicada y devuelve un GPtrArray de gchar* (rutas
 * absolutas, ordenadas alfabéticamente, sin duplicados) con los archivos
 * de audio/video. El array tiene free func: liberar con batch_free_file_list.
 * Devuelve NULL si la carpeta no existe/no se puede abrir y llena error.
 */
GPtrArray *batch_scan_folder      (const char *folder, GError **error);

/*
 * Filtra una lista de rutas (archivos sueltos): conserva solo archivos
 * regulares de audio/video, ordenados y sin duplicados.
 */
GPtrArray *batch_filter_file_list (char **paths, int n_paths);

/* Libera la lista devuelta por batch_scan_folder / batch_filter_file_list. */
void       batch_free_file_list   (GPtrArray *list);

G_END_DECLS

#endif /* BATCH_H */
