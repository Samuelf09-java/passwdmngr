#include <gtk/gtk.h>

typedef enum _AppMode {
    CLI,
    GUI
} AppMode;

extern AppMode mode;

extern GtkApplication *passwdmngr;
extern GtkWindow *root_window;