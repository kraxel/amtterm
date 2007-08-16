#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <ctype.h>

#include <gtk/gtk.h>
#include <vte/vte.h>

#include "parseconfig.h"
#include "redir.h"

#define APPNAME "gamt"

struct gamt_window {
    /* gtk stuff */
    GtkActionGroup *ag;
    GtkUIManager   *ui;
    GtkWidget      *win;
    GtkWidget      *vte;
    GtkWidget      *status;

    GtkActionGroup *hosts_ag;
    guint          hosts_id;

    /* sol stuff */
    struct redir   redir;
    GIOChannel     *ch;
    guint          id;
};

static char amt_host[64];
static char amt_port[16];
static char amt_user[32] = "admin";
static char amt_pass[32];

static int gamt_getstring(GtkWidget *window, char *title, char *message,
			  char *dest, int dlen, int hide);
static int gamt_connect(struct gamt_window *gamt);
static void gamt_rebuild_hosts(struct gamt_window *gamt);

/* ------------------------------------------------------------------ */

#define CFG_SECTION       "config", "config"
#define CFG_FONT          CFG_SECTION, "font"
#define CFG_FOREGROUND    CFG_SECTION, "foreground"
#define CFG_BACKGROUND    CFG_SECTION, "background"

/* ------------------------------------------------------------------ */

static void menu_cb_connect(GtkAction *action, void *data)
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

static void menu_cb_connect_to(GtkAction *action, void *data)
{
    struct gamt_window *gamt = data;
    
    if (gamt->redir.state != REDIR_NONE &&
	gamt->redir.state != REDIR_CLOSED &&
	gamt->redir.state != REDIR_ERROR)
	/* already have an active connection */
	return;

    if (1 != sscanf(gtk_action_get_name(action), "ConnectTo_%s", amt_host))
	return;
    gamt_connect(gamt);
}

static void menu_cb_disconnect(GtkAction *action, void *data)
{
    struct gamt_window *gamt = data;

    if (gamt->redir.state != REDIR_RUN_SOL)
	return;
    redir_sol_stop(&gamt->redir);
}

static void menu_cb_config_font(GtkAction *action, void *data)
{
    struct gamt_window *gamt = data;
    GtkWidget *dialog;
    char *fontname;

    dialog = gtk_font_selection_dialog_new("Terminal font");
    fontname = cfg_get_str(CFG_FONT);
    gtk_font_selection_dialog_set_font_name
	(GTK_FONT_SELECTION_DIALOG(dialog), fontname);
    gtk_widget_show_all(dialog);
    if (GTK_RESPONSE_OK == gtk_dialog_run(GTK_DIALOG(dialog))) {
	fontname = gtk_font_selection_dialog_get_font_name
	    (GTK_FONT_SELECTION_DIALOG(dialog));
	vte_terminal_set_font_from_string(VTE_TERMINAL(gamt->vte), fontname);
	cfg_set_str(CFG_FONT, fontname);
    }
    gtk_widget_destroy(dialog);
}

static int pickcolor(char *title, GdkColor *color)
{
    GtkWidget *dialog;
    GtkColorSelection *csel;
    int rc = -1;

    dialog = gtk_color_selection_dialog_new(title);
    csel = GTK_COLOR_SELECTION(GTK_COLOR_SELECTION_DIALOG(dialog)->colorsel);
    gtk_color_selection_set_has_opacity_control(csel, FALSE);
    gtk_color_selection_set_current_color(csel, color);
    
    gtk_widget_show_all(dialog);
    if (GTK_RESPONSE_OK == gtk_dialog_run(GTK_DIALOG(dialog))) {
	gtk_color_selection_get_current_color(csel, color);
	rc = 0;
    }
    gtk_widget_destroy(dialog);
    return rc;
}

static void menu_cb_config_fg(GtkAction *action, void *data)
{
    struct gamt_window *gamt = data;
    GdkColor color = {0,0,0,0};
    char name[16];

    gdk_color_parse(cfg_get_str(CFG_FOREGROUND), &color);
    if (0 != pickcolor("Text color", &color))
	return;
    vte_terminal_set_color_foreground(VTE_TERMINAL(gamt->vte), &color);
    snprintf(name, sizeof(name), "#%04x%04x%04x",
	     color.red, color.green, color.blue);
    cfg_set_str(CFG_FOREGROUND, name);
}

static void menu_cb_config_bg(GtkAction *action, void *data)
{
    struct gamt_window *gamt = data;
    GdkColor color = {0,0,0,0};
    char name[16];

    gdk_color_parse(cfg_get_str(CFG_BACKGROUND), &color);
    if (0 != pickcolor("Background color", &color))
	return;
    vte_terminal_set_color_background(VTE_TERMINAL(gamt->vte), &color);
    snprintf(name, sizeof(name), "#%04x%04x%04x",
	     color.red, color.green, color.blue);
    cfg_set_str(CFG_BACKGROUND, name);
}

static void menu_cb_quit(GtkAction *action, void *data)
{
    struct gamt_window *gamt = data;

    gtk_widget_destroy(gamt->win);
}

static void menu_cb_about(GtkAction *action, void *data)
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
    case REDIR_RUN_SOL:
	cfg_set_int("config", "hosts", gamt->redir.host,
		    cfg_get_int("config", "hosts", gamt->redir.host, 0) +1);
	gamt_rebuild_hosts(gamt);
	/* fall through */
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
	.name        = "ConfigMenu",
	.label       = "_Options",
    },{
	.name        = "HostMenu",
	.label       = "Ho_sts",
    },{
	.name        = "HelpMenu",
	.label       = "_Help",
    },{

	/* File menu */
	.name        = "Connect",
	.stock_id    = GTK_STOCK_CONNECT,
	.label       = "_Connect ...",
	.callback    = G_CALLBACK(menu_cb_connect),
    },{
	.name        = "Disconnect",
	.stock_id    = GTK_STOCK_DISCONNECT,
	.label       = "_Disconnect",
	.callback    = G_CALLBACK(menu_cb_disconnect),
    },{
	.name        = "Quit",
	.stock_id    = GTK_STOCK_QUIT,
	.label       = "_Quit",
	.callback    = G_CALLBACK(menu_cb_quit),
    },{

	/* Config menu */
	.name        = "VteFont",
	.stock_id    = GTK_STOCK_SELECT_FONT,
	.label       = "Terminal _font ...",
	.callback    = G_CALLBACK(menu_cb_config_font),
    },{
	.name        = "VteForeground",
	.label       = "_Text Color ...",
	.callback    = G_CALLBACK(menu_cb_config_fg),
    },{
	.name        = "VteBackground",
	.label       = "_Background Color ...",
	.callback    = G_CALLBACK(menu_cb_config_bg),
    },{

	/* Help menu */
	.name        = "About",
	.stock_id    = GTK_STOCK_ABOUT,
	.label       = "_About ...",
	.callback    = G_CALLBACK(menu_cb_about),
    }
};

static char ui_xml[] =
"<ui>\n"
"  <menubar action='MainMenu'>\n"
"    <menu action='FileMenu'>\n"
"      <menuitem action='Connect'/>\n"
"      <menuitem action='Disconnect'/>\n"
"      <separator/>\n"
"      <menuitem action='Quit'/>\n"
"    </menu>\n"
"    <menu action='HostMenu'>\n"
"    </menu>\n"
"    <menu action='ConfigMenu'>\n"
"      <menuitem action='VteFont'/>\n"
"      <menuitem action='VteForeground'/>\n"
"      <menuitem action='VteBackground'/>\n"
"    </menu>\n"
"    <menu action='HelpMenu'>\n"
"      <menuitem action='About'/>\n"
"    </menu>\n"
"  </menubar>\n"
"  <toolbar action='ToolBar'>\n"
"    <toolitem action='Quit'/>\n"
"    <toolitem action='Connect'/>\n"
"    <toolitem action='Disconnect'/>\n"
"  </toolbar>\n"
"</ui>\n";

static char hosts_xml_start[] =
"<ui>\n"
"  <menubar name='MainMenu'>\n"
"    <menu action='HostMenu'>\n";

static char hosts_xml_end[] =
"    </menu>\n"
"  </menubar>\n"
"</ui>\n";

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

static void gamt_rebuild_hosts(struct gamt_window *gamt)
{
    int count, size, pos;
    char *hosts_xml, *host, action[128];
    GtkActionEntry entry;
    GError *err = NULL;

    /* remove */
    if (gamt->hosts_id) {
	gtk_ui_manager_remove_ui(gamt->ui, gamt->hosts_id);
	gamt->hosts_id = 0;
    }
    if (gamt->hosts_ag) {
	gtk_ui_manager_remove_action_group(gamt->ui, gamt->hosts_ag);
	g_object_unref(gamt->hosts_ag);
	gamt->hosts_ag = NULL;
    }

    /* build */
    memset(&entry, 0, sizeof(entry));
    entry.callback = G_CALLBACK(menu_cb_connect_to);
    gamt->hosts_ag = gtk_action_group_new("HostActions");
    count = cfg_entries_count("config", "hosts");
    size = 128 * count + sizeof(hosts_xml_start) + sizeof(hosts_xml_end);
    hosts_xml = malloc(size); pos = 0;
    pos += sprintf(hosts_xml+pos, "%s", hosts_xml_start);
    for (host = cfg_entries_first("config", "hosts");
	 NULL != host;
	 host = cfg_entries_next("config", "hosts", host)) {
	snprintf(action, sizeof(action), "ConnectTo_%s", host);
	pos += snprintf(hosts_xml+pos, 128,
			"      <menuitem action='%s'/>\n",
			action);
	entry.name = action;
	entry.label = host;
	gtk_action_group_add_actions(gamt->hosts_ag, &entry, 1, gamt);
    }
    pos += sprintf(hosts_xml+pos, "%s", hosts_xml_end);
    
    /* add */
    gtk_ui_manager_insert_action_group(gamt->ui, gamt->hosts_ag, 1);
    gamt->hosts_id = gtk_ui_manager_add_ui_from_string(gamt->ui, hosts_xml, -1, &err);
    if (!gamt->hosts_id) {
	g_message("building host menu failed: %s", err->message);
	g_error_free(err);
    }
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
    GdkColor color;
    GError *err;
    struct gamt_window *gamt;
    char *str;
    
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
    gamt_rebuild_hosts(gamt);

    /* vte terminal */
    gamt->vte = vte_terminal_new();
    g_signal_connect(gamt->vte, "commit", G_CALLBACK(user_input), gamt);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(gamt->vte), 4096);
    str = cfg_get_str(CFG_FONT);
    if (str)
	vte_terminal_set_font_from_string(VTE_TERMINAL(gamt->vte), str);

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
#if 0
    item = gtk_ui_manager_get_widget(gamt->ui, "/ToolBar");
    gtk_box_pack_start(GTK_BOX(vbox), item, FALSE, FALSE, 0);
#endif
    gtk_box_pack_start(GTK_BOX(vbox), gamt->vte, TRUE, TRUE, 0);

    frame = gtk_frame_new(NULL);
    gtk_box_pack_end(GTK_BOX(vbox), frame, FALSE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(frame), gamt->status);

    /* display window */
    gtk_widget_show_all(gamt->win);
    
    str = cfg_get_str(CFG_FOREGROUND);
    if (str) {
	gdk_color_parse(str, &color);
	vte_terminal_set_color_foreground(VTE_TERMINAL(gamt->vte), &color);
    }
    str = cfg_get_str(CFG_BACKGROUND);
    if (str) {
	gdk_color_parse(str, &color);
	vte_terminal_set_color_background(VTE_TERMINAL(gamt->vte), &color);
    }

    return gamt;
}

/* ------------------------------------------------------------------ */

static void usage(FILE *fp)
{
    fprintf(fp,
            "\n"
	    "This is " APPNAME ", release " VERSION ", I'll establish\n"
	    "serial-over-lan (sol) connections to your Intel AMT boxes.\n"
            "\n"
            "usage: " APPNAME " [options] host\n"
            "options:\n"
            "   -h            print this text\n"
            "   -u user       username (default: admin)\n"
            "   -p pass       password (default: $AMT_PASSWORD)\n"
            "   -f font       terminal font\n"
            "   -c color      text color\n"
            "   -b color      backgrounf color\n"
            "\n"
            "By default port 16994 is used.\n"
	    "If no password is given " APPNAME " will ask for one.\n"
            "\n"
            "-- \n"
            "(c) 2007 Gerd Hoffmann <kraxel@redhat.com>\n"
	    "\n");
}

int
main(int argc, char *argv[])
{
    struct gamt_window *gamt;
    char configfile[256];
    char *h;
    int debug = 0;
    int c;

    if (NULL != (h = getenv("AMT_PASSWORD")))
	snprintf(amt_pass, sizeof(amt_pass), "%s", h);

    /* read config, make sure we have sane defaults */
    snprintf(configfile, sizeof(configfile), "%s/.gamtrc", getenv("HOME"));
    cfg_parse_file("config", configfile);
    if (!cfg_get_str(CFG_FONT))
	cfg_set_str(CFG_FONT, "monospace 12");
    if (!cfg_get_str(CFG_FOREGROUND))
	cfg_set_str(CFG_FOREGROUND, "gray");
    if (!cfg_get_str(CFG_BACKGROUND))
	cfg_set_str(CFG_BACKGROUND, "black");

    gtk_init(&argc, &argv);
    for (;;) {
        if (-1 == (c = getopt(argc, argv, "hdu:p:f:c:b:")))
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
	    cfg_set_str(CFG_FONT, optarg);
	    break;
	case 'c':
	    cfg_set_str(CFG_FOREGROUND, optarg);
	    break;
	case 'b':
	    cfg_set_str(CFG_BACKGROUND, optarg);
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
    cfg_write_file("config", configfile);
    exit(0);
}
