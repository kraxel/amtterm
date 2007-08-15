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

#include "redir.h"

struct gamt_window {
    /* gtk stuff */
    GtkActionGroup *ag;
    GtkUIManager   *ui;
    GtkWidget      *win;
    GtkWidget      *vte;
    GtkWidget      *status;

    /* sol stuff */
    struct redir   redir;
    GIOChannel     *ch;
    guint          id;
};

static char amt_host[64];
static char amt_port[16];
static char amt_user[32] = "admin";
static char amt_pass[32];
static char vte_font[64];

static int gamt_getstring(GtkWidget *window, char *title, char *message,
			  char *dest, int dlen, int hide);
static int gamt_connect(struct gamt_window *gamt);

/* ------------------------------------------------------------------ */

static void menu_cb_connect(GtkMenuItem *item, void *data)
{
    struct gamt_window *gamt = data;
    int rc;

    if (gamt->redir.state != REDIR_NONE &&
	gamt->redir.state != REDIR_CLOSED &&
	gamt->redir.state != REDIR_ERROR)
	/* already have an active connection */
	return;

    rc = gamt_getstring(gamt->win, "Connecting",
			"Connect to host ?",
			amt_host, sizeof(amt_host), 0);
    if (0 != rc)
	return;

    gamt_connect(gamt);
}

static void menu_cb_disconnect(GtkMenuItem *item, void *data)
{
    struct gamt_window *gamt = data;

    if (gamt->redir.state != REDIR_RUN_SOL)
	return;
    redir_sol_stop(&gamt->redir);
}

static void menu_cb_quit(GtkMenuItem *item, void *data)
{
    struct gamt_window *gamt = data;

    gtk_widget_destroy(gamt->win);
}

static void menu_cb_about(GtkMenuItem *item, void *data)
{
    static char *comments = "Intel AMT serial-over-lan client";
    static char *copyright = "(c) 2007 Gerd Hoffmann";
    static char *authors[] = { "Gerd Hoffmann <kraxel@redhat.com>", NULL };
    struct gamt_window *gamt = data;

    gtk_show_about_dialog(GTK_WINDOW(gamt->win),
                          "authors",         authors,
                          "comments",        comments,
                          "copyright",       copyright,
                          "logo-icon-name",  GTK_STOCK_ABOUT,
                          "version",         VERSION,
                          NULL);
}

static void destroy_cb(GtkWidget *widget, gpointer data)
{
    struct gamt_window *gamt = data;

    gtk_main_quit();
    free(gamt);
}

/* ------------------------------------------------------------------ */

static int recv_gtk(void *cb_data, unsigned char *buf, int len)
{
    struct gamt_window *gamt = cb_data;
    vte_terminal_feed(VTE_TERMINAL(gamt->vte), buf, len);
    return 0;
}

static void state_gtk(void *cb_data, enum redir_state old, enum redir_state new)
{
    struct gamt_window *gamt = cb_data;
    unsigned char buf[128];

    switch (new) {
    case REDIR_ERROR:
	snprintf(buf, sizeof(buf), "%s: %s FAILED", gamt->redir.host,
		 redir_state_desc(old));
	if (old == REDIR_AUTH) {
	    /* ask for a new password next time ... */
	    strcpy(amt_pass, "");
	}
	break;
    default:
	snprintf(buf, sizeof(buf), "%s: %s", gamt->redir.host,
		 redir_state_desc(new));
	break;
    }
    gtk_label_set_text(GTK_LABEL(gamt->status), buf);
}

static void user_input(VteTerminal *vte, gchar *buf, guint len,
		       gpointer data)
{
    struct gamt_window *gamt = data;

    if (gamt->redir.state == REDIR_RUN_SOL)
	redir_sol_send(&gamt->redir, buf, len);
}

/* ------------------------------------------------------------------ */

static const GtkActionEntry entries[] = {
    {
	.name        = "FileMenu",
	.label       = "_File",
    },{
	.name        = "HelpMenu",
	.label       = "_Help",
    },{

	/* File menu */
	.name        = "Connect",
	.label       = "_Connect ...",
	.callback    = G_CALLBACK(menu_cb_connect),
    },{
	.name        = "Disconnect",
	.label       = "_Disconnect",
	.callback    = G_CALLBACK(menu_cb_disconnect),
    },{
	.name        = "Quit",
	.stock_id    = GTK_STOCK_QUIT,
	.label       = "_Quit",
	.callback    = G_CALLBACK(menu_cb_quit),
    },{

	/* Help menu */
	.name        = "About",
	.stock_id    = GTK_STOCK_ABOUT,
	.label       = "_About ...",
	.callback    = G_CALLBACK(menu_cb_about),
    }
};

static char ui_xml[] =
"<ui>"
"  <menubar name='MainMenu'>"
"    <menu action='FileMenu'>"
"      <menuitem action='Connect'/>"
"      <menuitem action='Disconnect'/>"
"      <separator/>"
"      <menuitem action='Quit'/>"
"    </menu>"
"    <menu action='HelpMenu'>"
"      <menuitem action='About'/>"
"    </menu>"
"  </menubar>"
#ifdef WITH_TOOLBAR
"  <toolbar action='ToolBar'>"
"    <toolitem action='Close'/>"
"  </toolbar>"
#endif
"</ui>";

/* ------------------------------------------------------------------ */

static int gamt_getstring(GtkWidget *window, char *title, char *message,
			  char *dest, int dlen, int hide)
{
    GtkWidget *dialog, *label, *entry;
    const char *txt;
    int retval;
   
    /* Create the widgets */
    dialog = gtk_dialog_new_with_buttons(title,
					 GTK_WINDOW(window),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_STOCK_OK,
					 GTK_RESPONSE_ACCEPT,
					 GTK_STOCK_CANCEL,
					 GTK_RESPONSE_REJECT,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    
    label = gtk_label_new(message);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

    entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), dest);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    if (hide)
	gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);

    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), label);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), entry);
    gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 10);
#if 0 /* FIXME: doesn't work ... */
    gtk_container_set_border_width(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), 10);
#endif

    /* show and wait for response */
    gtk_widget_show_all(dialog);
    switch (gtk_dialog_run(GTK_DIALOG(dialog))) {
    case GTK_RESPONSE_ACCEPT:
	txt = gtk_entry_get_text(GTK_ENTRY(entry));
	snprintf(dest, dlen, "%s", txt);
	retval = 0;
	break;
    default:
	retval = -1;
	break;
    }
    gtk_widget_destroy(dialog);
    return retval;
}

static gboolean gamt_data(GIOChannel *source, GIOCondition condition,
			  gpointer data)
{
    struct gamt_window *gamt = data;

    redir_data(&gamt->redir);

    if (gamt->redir.state == REDIR_CLOSED ||
	gamt->redir.state == REDIR_ERROR) {
	g_source_destroy(g_main_context_find_source_by_id
                         (g_main_context_default(), gamt->id));
	gamt->id = 0;
	gamt->ch = NULL;
    }
    return TRUE;
}

static int gamt_connect(struct gamt_window *gamt)
{
    int rc;
    
    if (0 == strlen(amt_pass)) {
	char msg[128];

	snprintf(msg, sizeof(msg), "AMT password for %s@%s ?",
		 amt_user, amt_host);
	rc = gamt_getstring(gamt->win, "Authentication", msg,
			    amt_pass, sizeof(amt_pass), 1);
	if (0 != rc)
	    return -1;
    }

    memset(&gamt->redir, 0, sizeof(gamt->redir));
    memcpy(&gamt->redir.type, "SOL ", 4);

    snprintf(gamt->redir.host, sizeof(gamt->redir.host), "%s", amt_host);
    snprintf(gamt->redir.port, sizeof(gamt->redir.port), "%s", amt_port);
    snprintf(gamt->redir.user, sizeof(gamt->redir.user), "%s", amt_user);
    snprintf(gamt->redir.pass, sizeof(gamt->redir.pass), "%s", amt_pass);

    gamt->redir.verbose  = 1;
    gamt->redir.cb_data  = gamt;
    gamt->redir.cb_recv  = recv_gtk;
    gamt->redir.cb_state = state_gtk;

    if (-1 == redir_connect(&gamt->redir))
	return -1;

    vte_terminal_reset(VTE_TERMINAL(gamt->vte), TRUE, TRUE);
    gamt->ch = g_io_channel_unix_new(gamt->redir.sock);
    gamt->id = g_io_add_watch(gamt->ch, G_IO_IN, gamt_data, gamt);
    redir_start(&gamt->redir);
    return 0;
}

static struct gamt_window *gamt_window()
{
    GtkWidget *vbox, *frame, *item;
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
#if 0
    GtkAccelGroup *accel = gtk_ui_manager_get_accel_group(gamt->ui);
    gtk_window_add_accel_group(GTK_WINDOW(gamt->win), accel);
#endif

    err = NULL;
    if (!gtk_ui_manager_add_ui_from_string(gamt->ui, ui_xml, -1, &err)) {
	g_message("building menus failed: %s", err->message);
	g_error_free(err);
	exit(1);
    }

    /* vte terminal */
    gamt->vte = vte_terminal_new();
    g_signal_connect(gamt->vte, "commit", G_CALLBACK(user_input), gamt);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(gamt->vte), 4096);
    if (strlen(vte_font))
	vte_terminal_set_font_from_string(VTE_TERMINAL(gamt->vte), vte_font);

    /* other widgets */
    gamt->status = gtk_label_new("idle");
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
    struct gamt_window *gamt;
    char *h;
    int debug = 0;
    int c;

    if (NULL != (h = getenv("AMT_PASSWORD")))
	snprintf(amt_pass, sizeof(amt_pass), "%s", h);

    gtk_init(&argc, &argv);
    for (;;) {
        if (-1 == (c = getopt(argc, argv, "hdu:p:f:")))
            break;
        switch (c) {
	case 'd':
	    debug++;
	    break;
	case 'u':
	    snprintf(amt_user, sizeof(amt_user), "%s", optarg);
	    break;
	case 'p':
	    snprintf(amt_pass, sizeof(amt_pass), "%s", optarg);
	    memset(optarg,'*',strlen(optarg)); /* rm passwd from ps list */
	    break;
	case 'f':
	    snprintf(vte_font, sizeof(vte_font), "%s", optarg);
	    break;

        case 'h':
            usage(stdout);
            exit(0);
        default:
            usage(stderr);
            exit(1);
        }
    }

    gamt = gamt_window();
    if (NULL == gamt)
	exit(1);

    if (optind+1 <= argc) {
	snprintf(amt_host, sizeof(amt_host), "%s", argv[optind]);
	gamt_connect(gamt);
    }
    
    gtk_main();
    exit(0);
}
