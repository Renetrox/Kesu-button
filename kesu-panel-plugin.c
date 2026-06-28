#include <gtk/gtk.h>
#include <libxfce4panel/libxfce4panel.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#define KESU_PADDING 4

typedef struct {
    gchar *name;
    gchar *normal;
    gchar *hover;
    gchar *pressed;
    gint x;
    gint y;
    gboolean enabled;
} KesuLabelData;

typedef struct {
    XfcePanelPlugin *plugin;
    GtkWidget *event_box;
    GtkWidget *fixed;
    GtkWidget *image;
    GtkWidget *label;

    gchar *button_theme;
    gchar *button_themes_dir;
    gchar *theme_dir;
    gchar *launcher_path;

    gchar *normal_path;
    gchar *hover_path;
    gchar *pressed_path;

    GdkPixbuf *normal_pixbuf;
    GdkPixbuf *hover_pixbuf;
    GdkPixbuf *pressed_pixbuf;

    KesuLabelData label_data;

    gint panel_size;
    gint image_w;
    gint image_h;
    gdouble scale;
    gboolean pressed;
} KesuPlugin;

static gchar *home_build(const gchar *rel) { return g_build_filename(g_get_home_dir(), rel, NULL); }

static gchar *expand_home(const gchar *path) {
    if (!path || !*path) return NULL;
    if (g_str_has_prefix(path, "~/")) return g_build_filename(g_get_home_dir(), path + 2, NULL);
    return g_strdup(path);
}

static gchar *ini_get(GKeyFile *kf, const gchar *grp, const gchar *key, const gchar *fallback) {
    GError *err = NULL;
    gchar *v = kf ? g_key_file_get_string(kf, grp, key, &err) : NULL;
    if (err) g_error_free(err);
    if (v) {
        g_strstrip(v);
        if (*v) return v;
        g_free(v);
    }
    return g_strdup(fallback);
}

static void label_clear(KesuLabelData *l) {
    g_free(l->name); g_free(l->normal); g_free(l->hover); g_free(l->pressed);
    memset(l, 0, sizeof(*l));
}

static gchar *xml_prop(xmlNode *node, const gchar *name) {
    xmlChar *p = xmlGetProp(node, (const xmlChar *)name);
    if (!p) return NULL;
    gchar *s = g_strdup((const gchar *)p);
    xmlFree(p);
    if (s) g_strstrip(s);
    return s;
}

static gint xml_prop_int(xmlNode *node, const gchar *name, gint fallback) {
    gchar *s = xml_prop(node, name);
    if (!s || !*s) { g_free(s); return fallback; }
    gint n = (gint)g_ascii_strtoll(s, NULL, 10);
    g_free(s);
    return n;
}

static void load_config(KesuPlugin *k) {
    gchar *cfg = home_build(".config/xfcemenu/config.ini");
    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;
    gboolean ok = g_key_file_load_from_file(kf, cfg, G_KEY_FILE_NONE, &err);
    if (!ok && err) { g_warning("Kesú: no se pudo leer config.ini: %s", err->message); g_error_free(err); }

    gchar *def_button_dir = home_build(".local/share/xfcemenu/themes/Button");
    gchar *def_launcher = home_build(".local/bin/xfcemenu");

    k->button_theme = ini_get(ok ? kf : NULL, "theme", "button_theme", "Win2-7");
    gchar *raw_dir = ini_get(ok ? kf : NULL, "paths", "button_themes_dir", def_button_dir);
    gchar *raw_launcher = ini_get(ok ? kf : NULL, "paths", "launcher", def_launcher);
    k->button_themes_dir = expand_home(raw_dir);
    k->launcher_path = expand_home(raw_launcher);
    k->theme_dir = g_build_filename(k->button_themes_dir, k->button_theme, NULL);

    g_free(raw_dir); g_free(raw_launcher); g_free(def_button_dir); g_free(def_launcher); g_free(cfg);
    g_key_file_free(kf);
}

static gchar *find_theme_file(KesuPlugin *k, const gchar *file) {
    gchar *p = g_build_filename(k->theme_dir, file, NULL);
    if (g_file_test(p, G_FILE_TEST_EXISTS)) return p;
    g_free(p);
    p = g_build_filename(k->button_themes_dir, file, NULL);
    if (g_file_test(p, G_FILE_TEST_EXISTS)) return p;
    g_free(p);
    return NULL;
}

static void parse_label(KesuPlugin *k) {
    gchar *xml = g_build_filename(k->theme_dir, "themedata.xml", NULL);
    if (!g_file_test(xml, G_FILE_TEST_EXISTS)) { g_free(xml); return; }

    xmlDoc *doc = xmlReadFile(xml, NULL, XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) { g_free(xml); return; }

    xmlNode *root = xmlDocGetRootElement(doc);
    xmlNode *theme = NULL, *label = NULL;
    for (xmlNode *n = root ? root->children : NULL; n; n = n->next) {
        if (n->type == XML_ELEMENT_NODE && xmlStrcmp(n->name, (const xmlChar *)"theme") == 0) { theme = n; break; }
    }
    for (xmlNode *n = theme ? theme->children : NULL; n; n = n->next) {
        if (n->type == XML_ELEMENT_NODE && xmlStrcmp(n->name, (const xmlChar *)"Label") == 0) { label = n; break; }
    }

    if (label) {
        k->label_data.name = xml_prop(label, "Name");
        k->label_data.normal = xml_prop(label, "MarkupNormal");
        k->label_data.hover = xml_prop(label, "MarkupHover");
        k->label_data.pressed = xml_prop(label, "MarkupPressed");
        k->label_data.x = xml_prop_int(label, "LabelX", 0);
        k->label_data.y = xml_prop_int(label, "LabelY", 0);

        if ((!k->label_data.normal || !*k->label_data.normal) && k->label_data.name)
            k->label_data.normal = g_strdup(k->label_data.name);
        if ((!k->label_data.hover || !*k->label_data.hover) && k->label_data.normal)
            k->label_data.hover = g_strdup(k->label_data.normal);
        if ((!k->label_data.pressed || !*k->label_data.pressed) && k->label_data.normal)
            k->label_data.pressed = g_strdup(k->label_data.normal);
        k->label_data.enabled = (k->label_data.normal && *k->label_data.normal);
    }

    xmlFreeDoc(doc);
    g_free(xml);
}

static void reload_paths(KesuPlugin *k) {
    g_free(k->normal_path); g_free(k->hover_path); g_free(k->pressed_path);
    k->normal_path = find_theme_file(k, "start-here.png");
    k->hover_path = find_theme_file(k, "start-here-glow.png");
    k->pressed_path = find_theme_file(k, "start-here-depressed.png");
    if (!k->normal_path) k->normal_path = find_theme_file(k, "logo.svg");
    if (!k->hover_path) k->hover_path = find_theme_file(k, "glow.svg");
    if (!k->pressed_path) k->pressed_path = find_theme_file(k, "pressed.svg");
}

static GdkPixbuf *load_pixbuf(KesuPlugin *k, const gchar *path, gboolean save_scale) {
    if (!path || !g_file_test(path, G_FILE_TEST_EXISTS)) return NULL;
    GError *err = NULL;
    GdkPixbuf *src = gdk_pixbuf_new_from_file(path, &err);
    if (!src) { if (err) { g_warning("Kesú: no se pudo cargar %s: %s", path, err->message); g_error_free(err); } return NULL; }
    gint sw = gdk_pixbuf_get_width(src), sh = gdk_pixbuf_get_height(src);
    if (sw <= 0 || sh <= 0) return src;
    gint th = k->panel_size - KESU_PADDING;
    if (th < 16) th = k->panel_size;
    if (th < 16) th = 32;
    gdouble sc = (gdouble)th / (gdouble)sh;
    gint tw = MAX(1, (gint)(sw * sc));
    if (save_scale) k->scale = sc;
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(src, tw, th, GDK_INTERP_BILINEAR);
    g_object_unref(src);
    return scaled;
}

static void set_label_markup(GtkWidget *label, const gchar *markup) {
    if (!markup || !*markup) return;
    PangoAttrList *attrs = NULL;
    gchar *text = NULL;
    gunichar accel = 0;
    GError *err = NULL;
    if (pango_parse_markup(markup, -1, 0, &attrs, &text, &accel, &err)) {
        gtk_label_set_text(GTK_LABEL(label), text ? text : "");
        gtk_label_set_attributes(GTK_LABEL(label), attrs);
        if (attrs) pango_attr_list_unref(attrs);
        g_free(text);
    } else {
        gtk_label_set_text(GTK_LABEL(label), markup);
        if (err) g_error_free(err);
    }
}

static void update_layout(KesuPlugin *k) {
    if (k->normal_pixbuf) { k->image_w = gdk_pixbuf_get_width(k->normal_pixbuf); k->image_h = gdk_pixbuf_get_height(k->normal_pixbuf); }
    else { k->image_w = k->panel_size; k->image_h = k->panel_size; }
    gint req_w = k->image_w + KESU_PADDING;
    gint req_h = k->panel_size;
    gtk_widget_set_size_request(k->event_box, req_w, req_h);
    gtk_widget_set_size_request(k->fixed, req_w, req_h);
    gtk_widget_set_size_request(k->image, k->image_w, k->image_h);
    gint image_x = KESU_PADDING / 2;
    gint image_y = MAX(0, (k->panel_size - k->image_h) / 2);
    gtk_fixed_move(GTK_FIXED(k->fixed), k->image, image_x, image_y);
    if (k->label_data.enabled) {
        gtk_widget_show(k->label);
        gtk_fixed_move(GTK_FIXED(k->fixed), k->label,
            image_x + (gint)(k->label_data.x * k->scale),
            image_y + (gint)(k->label_data.y * k->scale));
    } else gtk_widget_hide(k->label);
}

static void reload_pixbufs(KesuPlugin *k) {
    if (k->normal_pixbuf) g_object_unref(k->normal_pixbuf);
    if (k->hover_pixbuf) g_object_unref(k->hover_pixbuf);
    if (k->pressed_pixbuf) g_object_unref(k->pressed_pixbuf);
    k->normal_pixbuf = load_pixbuf(k, k->normal_path, TRUE);
    k->hover_pixbuf = load_pixbuf(k, k->hover_path, FALSE);
    k->pressed_pixbuf = load_pixbuf(k, k->pressed_path, FALSE);
    if (!k->hover_pixbuf && k->normal_pixbuf) k->hover_pixbuf = g_object_ref(k->normal_pixbuf);
    if (!k->pressed_pixbuf && k->normal_pixbuf) k->pressed_pixbuf = g_object_ref(k->normal_pixbuf);
    update_layout(k);
}

static void set_state(KesuPlugin *k, const gchar *state) {
    GdkPixbuf *pix = k->normal_pixbuf;
    const gchar *txt = k->label_data.normal;
    if (g_strcmp0(state, "pressed") == 0) { if (k->pressed_pixbuf) pix = k->pressed_pixbuf; if (k->label_data.pressed) txt = k->label_data.pressed; }
    else if (g_strcmp0(state, "hover") == 0) { if (k->hover_pixbuf) pix = k->hover_pixbuf; if (k->label_data.hover) txt = k->label_data.hover; }
    if (pix) gtk_image_set_from_pixbuf(GTK_IMAGE(k->image), pix);
    else gtk_image_set_from_icon_name(GTK_IMAGE(k->image), "start-here", GTK_ICON_SIZE_BUTTON);
    if (k->label_data.enabled) set_label_markup(k->label, txt);
}

static gchar *launcher_command(KesuPlugin *k) {
    const gchar *launcher = (k->launcher_path && g_file_test(k->launcher_path, G_FILE_TEST_EXISTS)) ? k->launcher_path : NULL;
    gchar *fallback = NULL;
    if (!launcher) { fallback = home_build(".local/bin/xfcemenu"); launcher = fallback; }
    gchar *cmd;
    if (launcher && g_file_test(launcher, G_FILE_TEST_EXISTS)) cmd = g_shell_quote(launcher);
    else cmd = g_strdup("xfcemenu");
    g_free(fallback);
    return cmd;
}

static gboolean on_enter(GtkWidget *w G_GNUC_UNUSED, GdkEventCrossing *e G_GNUC_UNUSED, gpointer data) { KesuPlugin *k=data; if (!k->pressed) set_state(k,"hover"); return FALSE; }
static gboolean on_leave(GtkWidget *w G_GNUC_UNUSED, GdkEventCrossing *e G_GNUC_UNUSED, gpointer data) { KesuPlugin *k=data; k->pressed=FALSE; set_state(k,"normal"); return FALSE; }
static gboolean on_press(GtkWidget *w G_GNUC_UNUSED, GdkEventButton *e, gpointer data) { KesuPlugin *k=data; if (e->button==1) { k->pressed=TRUE; set_state(k,"pressed"); return TRUE; } return FALSE; }
static gboolean on_release(GtkWidget *w G_GNUC_UNUSED, GdkEventButton *e, gpointer data) {
    KesuPlugin *k=data;
    if (e->button==1) {
        k->pressed=FALSE; set_state(k,"hover");
        gchar *cmd = launcher_command(k);
        GError *err = NULL;
        if (!g_spawn_command_line_async(cmd, &err)) { if (err) { g_warning("Kesú: no se pudo lanzar '%s': %s", cmd, err->message); g_error_free(err); } }
        g_free(cmd);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_size_changed(XfcePanelPlugin *plugin G_GNUC_UNUSED, gint size, gpointer data) {
    KesuPlugin *k=data; k->panel_size=size; reload_pixbufs(k); set_state(k,"normal"); return TRUE;
}

static void free_kesu(XfcePanelPlugin *plugin G_GNUC_UNUSED, gpointer data) {
    KesuPlugin *k=data;
    if (!k) return;
    if (k->normal_pixbuf) g_object_unref(k->normal_pixbuf);
    if (k->hover_pixbuf) g_object_unref(k->hover_pixbuf);
    if (k->pressed_pixbuf) g_object_unref(k->pressed_pixbuf);
    g_free(k->button_theme); g_free(k->button_themes_dir); g_free(k->theme_dir); g_free(k->launcher_path);
    g_free(k->normal_path); g_free(k->hover_path); g_free(k->pressed_path);
    label_clear(&k->label_data);
    g_free(k);
}

static void kesu_construct(XfcePanelPlugin *plugin) {
    KesuPlugin *k = g_new0(KesuPlugin, 1);
    k->plugin = plugin;
    k->panel_size = xfce_panel_plugin_get_size(plugin);
    k->scale = 1.0;
    load_config(k);
    parse_label(k);
    reload_paths(k);

    k->event_box = gtk_event_box_new();
    k->fixed = gtk_fixed_new();
    k->image = gtk_image_new();
    k->label = gtk_label_new(NULL);
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(k->event_box), FALSE);
    gtk_container_add(GTK_CONTAINER(k->event_box), k->fixed);
    gtk_fixed_put(GTK_FIXED(k->fixed), k->image, 0, 0);
    gtk_fixed_put(GTK_FIXED(k->fixed), k->label, 0, 0);

    gtk_widget_add_events(k->event_box, GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_ENTER_NOTIFY_MASK|GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(k->event_box,"enter-notify-event",G_CALLBACK(on_enter),k);
    g_signal_connect(k->event_box,"leave-notify-event",G_CALLBACK(on_leave),k);
    g_signal_connect(k->event_box,"button-press-event",G_CALLBACK(on_press),k);
    g_signal_connect(k->event_box,"button-release-event",G_CALLBACK(on_release),k);
    g_signal_connect(plugin,"size-changed",G_CALLBACK(on_size_changed),k);
    g_signal_connect(plugin,"free-data",G_CALLBACK(free_kesu),k);

    reload_pixbufs(k);
    set_state(k,"normal");
    gtk_container_add(GTK_CONTAINER(plugin), k->event_box);
    xfce_panel_plugin_add_action_widget(plugin, k->event_box);
    gtk_widget_show_all(GTK_WIDGET(plugin));
    if (!k->label_data.enabled) gtk_widget_hide(k->label);
}

XFCE_PANEL_PLUGIN_REGISTER(kesu_construct)
