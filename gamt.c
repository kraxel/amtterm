#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>

#include <gtk/gtk.h>
#include <vte/vte.h>

struct gamt_window {
    /* gtk stuff */
    GtkActionGroup *ag;
    GtkUIManager   *ui;
    GtkWidget      *win;
    GtkWidget      *vte;
    GtkWidget      *status;
};

/* ------------------------------------------------------------------ */

static void menu_cb_quit(GtkMenuItem *item, void *data)
{
    struct gamt_window *gamt = data;

    gtk_widget_destroy(gamt->win);
}

static void destroy_cb(GtkWidget *widget, gpointer data)
{
    struct gamt_window *gamt = data;

    gtk_main_quit();
    free(gamt);
}

/* ------------------------------------------------------------------ */

static const GtkActionEntry entries[] = {
    {
	.name        = "FileMenu",
	.label       = "_File",
    },{
	.name        = "Quit",
	.stock_id    = GTK_STOCK_QUIT,
	.label       = "_Quit",
	.callback    = G_CALLBACK(menu_cb_quit),
    }
};

static char ui_xml[] =
"<ui>"
"  <menubar name='MainMenu'>"
"    <menu action='FileMenu'>"
"      <menuitem action='Quit'/>"
"    </menu>"
"  </menubar>"
#ifdef WITH_TOOLBAR
"  <toolbar action='ToolBar'>"
"    <toolitem action='Close'/>"
"  </toolbar>"
#endif
"</ui>";

/* ------------------------------------------------------------------ */

static struct gamt_window *gamt_window()
{
    GtkWidget *vbox, *frame, *item;
    GtkAccelGroup *accel;
    GError *err;
    struct gamt_window *gamt;
    
    gamt = malloc(sizeof(*gamt));
    if (NULL == gamt)
	return NULL;
    memset(gamt,0,sizeof(*gamt));

    /* gtk toplevel */
    gamt->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(G_OBJECT(gamt->win), "destroy",
                     G_CALLBACK(destroy_cb), gamt);

    /* menu + toolbar */
    gamt->ui = gtk_ui_manager_new();
    gamt->ag = gtk_action_group_new("MenuActions");
    gtk_action_group_add_actions(gamt->ag, entries, G_N_ELEMENTS(entries), gamt);
    gtk_ui_manager_insert_action_group(gamt->ui, gamt->ag, 0);
    accel = gtk_ui_manager_get_accel_group(gamt->ui);
    gtk_window_add_accel_group(GTK_WINDOW(gamt->win), accel);

    err = NULL;
    if (!gtk_ui_manager_add_ui_from_string(gamt->ui, ui_xml, -1, &err)) {
	g_message("building menus failed: %s", err->message);
	g_error_free(err);
	exit(1);
    }

    /* vte terminal */
    gamt->vte = vte_terminal_new();

    /* other widgets */
    gamt->status = gtk_label_new("status line");
    gtk_misc_set_alignment(GTK_MISC(gamt->status), 0, 0.5);
    gtk_misc_set_padding(GTK_MISC(gamt->status), 3, 1);

    /* Make a vbox and put stuff in */
    vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 1);
    gtk_container_add(GTK_CONTAINER(gamt->win), vbox);
    item = gtk_ui_manager_get_widget(gamt->ui, "/MainMenu");
    gtk_box_pack_start(GTK_BOX(vbox), item, FALSE, FALSE, 0);
    item = gtk_ui_manager_get_widget(gamt->ui, "/ToolBar");
    if (item)
	gtk_box_pack_start(GTK_BOX(vbox), item, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), gamt->vte, TRUE, TRUE, 0);

    frame = gtk_frame_new(NULL);
    gtk_box_pack_end(GTK_BOX(vbox), frame, FALSE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(frame), gamt->status);

    /* display window */
    gtk_widget_show_all(gamt->win);
    
    return gamt;
}

/* ------------------------------------------------------------------ */

static void usage(FILE *fp)
{
    fprintf(fp,
	    "TODO: help test\n"
	    "\n"
	    "-- \n"
	    "(c) 2007 Gerd Hoffmann <kraxel@redhat.com>\n");
}

int
main(int argc, char *argv[])
{
    int debug = 0;
    int c;

    gtk_init(&argc, &argv);
    for (;;) {
        if (-1 == (c = getopt(argc, argv, "hd")))
            break;
        switch (c) {
	case 'd':
	    debug++;
	    break;
        case 'h':
            usage(stdout);
            exit(0);
        default:
            usage(stderr);
            exit(1);
        }
    }

#if 0
    if (optind+1 > argc) {
	usage(stderr);
	exit(1);
    }
#endif

    if (NULL == gamt_window())
	exit(1);

    gtk_main();
    exit(0);
}
