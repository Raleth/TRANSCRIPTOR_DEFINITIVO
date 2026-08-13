#ifndef GUI_H
#define GUI_H

/* dependencias (GTK 4) */
#include <gtk/gtk.h>

/*
 * GUI: interfaz GTK 4 de Ana-Trans.
 *
 * Construye la ventana (pestañas Transcripción y Preferencias), reúne la
 * configuración del usuario y entrega el trabajo al motor asíncrono
 * (controller.c) para procesarlo en segundo plano. Esta capa solo pinta
 * widgets y muestra los eventos del motor: no ejecuta ffmpeg ni whisper.
 *
 * [PROYECTO]    lógica de la ventana y de las preferencias.
 * [DEPENDENCIA] GTK 4 (widgets, diálogos), GLib.
 */

void gui_activate (GtkApplication *app);

#endif /* GUI_H */
