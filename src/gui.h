#ifndef GUI_H
#define GUI_H

#include <gtk/gtk.h>

/*
 * GUI: ventana GTK 4 para elegir carpeta/archivos de entrada, carpeta de
 * salida, modelo de whisper e idioma, con lista de archivos, progreso y log.
 *
 * Esta fase es infraestructura: el botón "Procesar" todavía no transcribe,
 * solo muestra los archivos que procesaría.
 */

void gui_activate (GtkApplication *app);

#endif /* GUI_H */
