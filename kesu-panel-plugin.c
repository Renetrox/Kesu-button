#include <gtk/gtk.h>
#include <libxfce4panel/libxfce4panel.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define KESU_HORIZONTAL_PADDING 4
#define KESU_MENU_WATCH_MS 500

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

    GtkWidget *top_window;
    GtkWidget *top_event_box;
    GtkWidget *top_image;

    gchar *button_theme;
    gchar *button_themes_dir;
    gchar *theme_dir;
    gchar *launcher_path;

    gchar *normal_path;
    gchar *hover_path;
    gchar *pressed_path;

    gchar *top_normal_path;
    gchar *top_hover_path;
    gchar *top_pressed_path;

    GdkPixbuf *normal_pixbuf;
    GdkPixbuf *hover_pixbuf;
    GdkPixbuf *pressed_pixbuf;

    GdkPixbuf *top_normal_pixbuf;
    GdkPixbuf *top_hover_pixbuf;
    GdkPixbuf *top_pressed_pixbuf;

    KesuLabelData label_data;

    gint panel_size;
    gint image_w;
    gint image_h;
    gint top_w;
    gint top_h;
    gdouble scale;

    gboolean has_top;
    gboolean pointer_inside;
    gboolean pressed;
    gboolean menu_active;
    guint menu_watch_id;
} KesuPlugin;

static gboolean on_enter(GtkWidget *w, GdkEventCrossing *e, gpointer data);
static gboolean on_leave(GtkWidget *w, GdkEventCrossing *e, gpointer data);
static gboolean on_press(GtkWidget *w, GdkEventButton *e, gpointer data);
static gboolean on_release(GtkWidget *w, GdkEventButton *e, gpointer data);
static gboolean launcher_geometry(KesuPlugin *k, gint *x, gint *y, gint *w, gint *h);
static const gchar *panel_position_name(KesuPlugin *k, gint center_x, gint center_y);
static void position_top_window(KesuPlugin *k);
static void refresh_state(KesuPlugin *k);

static gchar *home_build(const gchar *rel) {
    return g_build_filename(g_get_home_dir(), rel, NULL);
}

static gchar *expand_home(const gchar *path) {
    if (!path || !*path) return NULL;
    if (g_str_has_prefix(path, "~/"))
        return g_build_filename(g_get_home_dir(), path + 2, NULL);
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
    g_free(l->name);
    g_free(l->normal);
    g_free(l->hover);
    g_free(l->pressed);
    memset(l, 0, sizeof(*l));
}

static gchar *xml_prop(xmlNode *node, const gchar *name) {
    xmlChar *p;
    gchar *s;

    if (!node || !name) return NULL;

    p = xmlGetProp(node, (const xmlChar *)name);
    if (!p) return NULL;

    s = g_strdup((const gchar *)p);
    xmlFree(p);

    if (s) g_strstrip(s);
    return s;
}

static gint xml_prop_int(xmlNode *node, const gchar *name, gint fallback) {
    gchar *s = xml_prop(node, name);
    gint n;

    if (!s || !*s) {
        g_free(s);
        return fallback;
    }

    n = (gint)g_ascii_strtoll(s, NULL, 10);
    g_free(s);
    return n;
}

static xmlNode *xml_child(xmlNode *parent, const gchar *name) {
    xmlNode *n;

    for (n = parent ? parent->children : NULL; n; n = n->next) {
        if (n->type == XML_ELEMENT_NODE &&
            xmlStrcmp(n->name, (const xmlChar *)name) == 0)
            return n;
    }

    return NULL;
}

static void load_config(KesuPlugin *k) {
    gchar *cfg = home_build(".config/xfcemenu/config.ini");
    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;
    gboolean ok = g_key_file_load_from_file(kf, cfg, G_KEY_FILE_NONE, &err);

    if (!ok && err) {
        g_warning("Kesú: no se pudo leer config.ini: %s", err->message);
        g_error_free(err);
    }

    {
        gchar *def_button_dir = home_build(".local/share/xfcemenu/themes/Button");
        gchar *def_launcher = home_build(".local/bin/xfcemenu");
        gchar *raw_dir;
        gchar *raw_launcher;

        k->button_theme = ini_get(ok ? kf : NULL, "theme", "button_theme", "Win2-7");
        raw_dir = ini_get(ok ? kf : NULL, "paths", "button_themes_dir", def_button_dir);
        raw_launcher = ini_get(ok ? kf : NULL, "paths", "launcher", def_launcher);

        k->button_themes_dir = expand_home(raw_dir);
        k->launcher_path = expand_home(raw_launcher);
        k->theme_dir = g_build_filename(k->button_themes_dir, k->button_theme, NULL);

        g_free(raw_dir);
        g_free(raw_launcher);
        g_free(def_button_dir);
        g_free(def_launcher);
    }

    g_free(cfg);
    g_key_file_free(kf);
}

static gchar *find_theme_file(KesuPlugin *k, const gchar *file) {
    gchar *p;

    if (!file || !*file) return NULL;

    if (g_path_is_absolute(file)) {
        if (g_file_test(file, G_FILE_TEST_EXISTS))
            return g_strdup(file);
        return NULL;
    }

    p = g_build_filename(k->theme_dir, file, NULL);
    if (g_file_test(p, G_FILE_TEST_EXISTS))
        return p;
    g_free(p);

    p = g_build_filename(k->button_themes_dir, file, NULL);
    if (g_file_test(p, G_FILE_TEST_EXISTS))
        return p;
    g_free(p);

    return NULL;
}

static gchar *theme_asset(KesuPlugin *k, xmlNode *node, const gchar *attribute) {
    gchar *name = xml_prop(node, attribute);
    gchar *path = NULL;

    if (name && *name)
        path = find_theme_file(k, name);

    if (name && *name && !path)
        g_warning("Kesú: el tema declara %s='%s' pero no se encontró el archivo",
                  attribute, name);

    g_free(name);
    return path;
}

static void fallback_path(KesuPlugin *k, gchar **slot,
                          const gchar *primary, const gchar *secondary) {
    if (*slot) return;

    *slot = find_theme_file(k, primary);
    if (!*slot && secondary)
        *slot = find_theme_file(k, secondary);
}

static void parse_label_node(KesuPlugin *k, xmlNode *label) {
    if (!label) return;

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

    k->label_data.enabled =
        (k->label_data.normal && *k->label_data.normal);
}

static void clear_theme_paths(KesuPlugin *k) {
    g_free(k->normal_path);
    g_free(k->hover_path);
    g_free(k->pressed_path);
    g_free(k->top_normal_path);
    g_free(k->top_hover_path);
    g_free(k->top_pressed_path);

    k->normal_path = NULL;
    k->hover_path = NULL;
    k->pressed_path = NULL;
    k->top_normal_path = NULL;
    k->top_hover_path = NULL;
    k->top_pressed_path = NULL;
}

static void parse_theme(KesuPlugin *k) {
    gchar *xml;
    xmlDoc *doc = NULL;
    xmlNode *root;
    xmlNode *theme;
    xmlNode *background;
    xmlNode *top;
    xmlNode *label;

    clear_theme_paths(k);
    label_clear(&k->label_data);
    k->has_top = FALSE;

    xml = g_build_filename(k->theme_dir, "themedata.xml", NULL);

    if (g_file_test(xml, G_FILE_TEST_EXISTS)) {
        doc = xmlReadFile(xml, NULL,
                          XML_PARSE_RECOVER |
                          XML_PARSE_NOERROR |
                          XML_PARSE_NOWARNING);
    }

    root = doc ? xmlDocGetRootElement(doc) : NULL;
    theme = xml_child(root, "theme");
    background = xml_child(theme, "Background");
    top = xml_child(theme, "Top");
    label = xml_child(theme, "Label");

    if (theme)
        k->has_top = (xml_prop_int(theme, "Top", 0) == 1);

    if (background) {
        k->normal_path = theme_asset(k, background, "Image");
        k->hover_path = theme_asset(k, background, "ImageHover");
        k->pressed_path = theme_asset(k, background, "ImagePressed");
    }

    if (k->has_top && top) {
        k->top_normal_path = theme_asset(k, top, "Image");
        k->top_hover_path = theme_asset(k, top, "ImageHover");
        k->top_pressed_path = theme_asset(k, top, "ImagePressed");
    }

    parse_label_node(k, label);

    /*
     * Compatibilidad con los temas ya usados por Kesu y con paquetes viejos
     * incompletos: el XML manda; estos nombres sólo son fallback.
     */
    fallback_path(k, &k->normal_path, "start-here.png", "logo.svg");
    fallback_path(k, &k->hover_path, "start-here-glow.png", "glow.svg");
    fallback_path(k, &k->pressed_path, "start-here-depressed.png", "pressed.svg");

    if (k->has_top) {
        fallback_path(k, &k->top_normal_path, "start-here-top.png", NULL);
        fallback_path(k, &k->top_hover_path, "start-here-top-glow.png", NULL);
        fallback_path(k, &k->top_pressed_path, "start-here-top-depressed.png", NULL);

        if (!k->top_normal_path) {
            g_warning("Kesú: Top=\"1\" sin una imagen <Top> utilizable; se desactiva el overlay");
            k->has_top = FALSE;
        }
    }

    if (doc) xmlFreeDoc(doc);
    g_free(xml);

    g_message("Kesú: tema '%s' cargado desde themedata.xml (Top=%s, Label=%s)",
              k->button_theme ? k->button_theme : "",
              k->has_top ? "sí" : "no",
              k->label_data.enabled ? "sí" : "no");
}

static gboolean panel_is_top(KesuPlugin *k) {
    XfceScreenPosition pos;

    if (!k || !k->plugin) return FALSE;

    pos = xfce_panel_plugin_get_screen_position(k->plugin);
    return xfce_screen_position_is_top(pos);
}

static GdkPixbuf *load_main_pixbuf(KesuPlugin *k,
                                   const gchar *path,
                                   gboolean save_scale) {
    GError *err = NULL;
    GdkPixbuf *src;
    GdkPixbuf *scaled;
    gint sw, sh;
    gint th, tw;
    gdouble sc;

    if (!path || !g_file_test(path, G_FILE_TEST_EXISTS))
        return NULL;

    src = gdk_pixbuf_new_from_file(path, &err);
    if (!src) {
        if (err) {
            g_warning("Kesú: no se pudo cargar %s: %s", path, err->message);
            g_error_free(err);
        }
        return NULL;
    }

    sw = gdk_pixbuf_get_width(src);
    sh = gdk_pixbuf_get_height(src);

    if (sw <= 0 || sh <= 0)
        return src;

    /*
     * Igual que GnoMenu: Background define la escala respecto al alto real
     * del panel. El padding de Kesu sólo amplía el área clicable horizontal.
     */
    th = k->panel_size;
    if (th < 16) th = 32;

    sc = (gdouble)th / (gdouble)sh;
    tw = MAX(1, (gint)(sw * sc));

    if (save_scale)
        k->scale = sc;

    scaled = gdk_pixbuf_scale_simple(src, tw, th, GDK_INTERP_BILINEAR);
    g_object_unref(src);

    /*
     * GnoMenu volteaba solamente el Background principal cuando el panel
     * estaba arriba y el tema usaba Top="1". La capa <Top> no se volteaba.
     */
    if (scaled && k->has_top && panel_is_top(k)) {
        GdkPixbuf *flipped = gdk_pixbuf_flip(scaled, FALSE);
        if (flipped) {
            g_object_unref(scaled);
            scaled = flipped;
        }
    }

    return scaled;
}

static GdkPixbuf *load_top_pixbuf(KesuPlugin *k, const gchar *path) {
    GError *err = NULL;
    GdkPixbuf *src;
    GdkPixbuf *scaled;
    gint sw, sh;
    gint tw, th;
    gdouble sc;

    if (!path || !g_file_test(path, G_FILE_TEST_EXISTS))
        return NULL;

    src = gdk_pixbuf_new_from_file(path, &err);
    if (!src) {
        if (err) {
            g_warning("Kesú: no se pudo cargar overlay %s: %s", path, err->message);
            g_error_free(err);
        }
        return NULL;
    }

    sw = gdk_pixbuf_get_width(src);
    sh = gdk_pixbuf_get_height(src);

    if (sw <= 0 || sh <= 0)
        return src;

    /*
     * Punto clave del motor original: <Top> NO se escala al alto del panel.
     * Usa exactamente la escala calculada por Background.
     */
    sc = k->scale > 0.0 ? k->scale : 1.0;
    tw = MAX(1, (gint)(sw * sc));
    th = MAX(1, (gint)(sh * sc));

    scaled = gdk_pixbuf_scale_simple(src, tw, th, GDK_INTERP_BILINEAR);
    g_object_unref(src);
    return scaled;
}

static void set_label_markup(GtkWidget *label, const gchar *markup) {
    PangoAttrList *attrs = NULL;
    gchar *text = NULL;
    gunichar accel = 0;
    GError *err = NULL;

    if (!markup || !*markup)
        return;

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

static gboolean on_top_draw(GtkWidget *widget G_GNUC_UNUSED,
                            cairo_t *cr,
                            gpointer data G_GNUC_UNUSED) {
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    cairo_restore(cr);
    return FALSE;
}

static void create_top_window(KesuPlugin *k) {
    GdkScreen *screen;
    GdkVisual *visual;

    if (!k->has_top || k->top_window)
        return;

    k->top_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(k->top_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(k->top_window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(k->top_window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(k->top_window), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(k->top_window), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(k->top_window), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(k->top_window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(k->top_window), GDK_WINDOW_TYPE_HINT_DOCK);
    gtk_window_stick(GTK_WINDOW(k->top_window));

    gtk_widget_set_name(k->top_window, "kesu-top-overlay");
    gtk_widget_set_app_paintable(k->top_window, TRUE);

    screen = gtk_widget_get_screen(GTK_WIDGET(k->plugin));
    visual = screen ? gdk_screen_get_rgba_visual(screen) : NULL;
    if (visual)
        gtk_widget_set_visual(k->top_window, visual);

    g_signal_connect(k->top_window, "draw", G_CALLBACK(on_top_draw), k);

    k->top_event_box = gtk_event_box_new();
    k->top_image = gtk_image_new();

    gtk_event_box_set_visible_window(GTK_EVENT_BOX(k->top_event_box), FALSE);
    gtk_container_add(GTK_CONTAINER(k->top_event_box), k->top_image);
    gtk_container_add(GTK_CONTAINER(k->top_window), k->top_event_box);

    gtk_widget_add_events(
        k->top_event_box,
        GDK_BUTTON_PRESS_MASK |
        GDK_BUTTON_RELEASE_MASK |
        GDK_ENTER_NOTIFY_MASK |
        GDK_LEAVE_NOTIFY_MASK);

    g_signal_connect(k->top_event_box, "enter-notify-event", G_CALLBACK(on_enter), k);
    g_signal_connect(k->top_event_box, "leave-notify-event", G_CALLBACK(on_leave), k);
    g_signal_connect(k->top_event_box, "button-press-event", G_CALLBACK(on_press), k);
    g_signal_connect(k->top_event_box, "button-release-event", G_CALLBACK(on_release), k);
}

static void update_layout(KesuPlugin *k) {
    gint req_w;
    gint req_h;
    gint image_x;
    gint image_y;

    if (k->normal_pixbuf) {
        k->image_w = gdk_pixbuf_get_width(k->normal_pixbuf);
        k->image_h = gdk_pixbuf_get_height(k->normal_pixbuf);
    } else {
        k->image_w = k->panel_size;
        k->image_h = k->panel_size;
    }

    req_w = k->image_w + KESU_HORIZONTAL_PADDING;
    req_h = k->panel_size;

    gtk_widget_set_size_request(k->event_box, req_w, req_h);
    gtk_widget_set_size_request(k->fixed, req_w, req_h);
    gtk_widget_set_size_request(k->image, k->image_w, k->image_h);

    image_x = KESU_HORIZONTAL_PADDING / 2;
    image_y = MAX(0, (k->panel_size - k->image_h) / 2);

    gtk_fixed_move(GTK_FIXED(k->fixed), k->image, image_x, image_y);

    if (k->label_data.enabled) {
        gtk_widget_show(k->label);
        gtk_fixed_move(
            GTK_FIXED(k->fixed),
            k->label,
            image_x + (gint)(k->label_data.x * k->scale),
            image_y + (gint)(k->label_data.y * k->scale));
    } else {
        gtk_widget_hide(k->label);
    }
}

static void unref_pixbuf(GdkPixbuf **pixbuf) {
    if (*pixbuf) {
        g_object_unref(*pixbuf);
        *pixbuf = NULL;
    }
}

static void reload_pixbufs(KesuPlugin *k) {
    unref_pixbuf(&k->normal_pixbuf);
    unref_pixbuf(&k->hover_pixbuf);
    unref_pixbuf(&k->pressed_pixbuf);
    unref_pixbuf(&k->top_normal_pixbuf);
    unref_pixbuf(&k->top_hover_pixbuf);
    unref_pixbuf(&k->top_pressed_pixbuf);

    k->normal_pixbuf = load_main_pixbuf(k, k->normal_path, TRUE);
    k->hover_pixbuf = load_main_pixbuf(k, k->hover_path, FALSE);
    k->pressed_pixbuf = load_main_pixbuf(k, k->pressed_path, FALSE);

    if (!k->hover_pixbuf && k->normal_pixbuf)
        k->hover_pixbuf = g_object_ref(k->normal_pixbuf);

    if (!k->pressed_pixbuf && k->normal_pixbuf)
        k->pressed_pixbuf = g_object_ref(k->normal_pixbuf);

    if (k->has_top) {
        k->top_normal_pixbuf = load_top_pixbuf(k, k->top_normal_path);
        k->top_hover_pixbuf = load_top_pixbuf(k, k->top_hover_path);
        k->top_pressed_pixbuf = load_top_pixbuf(k, k->top_pressed_path);

        if (!k->top_hover_pixbuf && k->top_normal_pixbuf)
            k->top_hover_pixbuf = g_object_ref(k->top_normal_pixbuf);

        if (!k->top_pressed_pixbuf && k->top_normal_pixbuf)
            k->top_pressed_pixbuf = g_object_ref(k->top_normal_pixbuf);
    }

    update_layout(k);
}

static void set_state(KesuPlugin *k, const gchar *state) {
    GdkPixbuf *pix = k->normal_pixbuf;
    GdkPixbuf *top_pix = k->top_normal_pixbuf;
    const gchar *txt = k->label_data.normal;

    if (g_strcmp0(state, "pressed") == 0) {
        if (k->pressed_pixbuf) pix = k->pressed_pixbuf;
        if (k->top_pressed_pixbuf) top_pix = k->top_pressed_pixbuf;
        if (k->label_data.pressed) txt = k->label_data.pressed;
    } else if (g_strcmp0(state, "hover") == 0) {
        if (k->hover_pixbuf) pix = k->hover_pixbuf;
        if (k->top_hover_pixbuf) top_pix = k->top_hover_pixbuf;
        if (k->label_data.hover) txt = k->label_data.hover;
    }

    if (pix)
        gtk_image_set_from_pixbuf(GTK_IMAGE(k->image), pix);
    else
        gtk_image_set_from_icon_name(
            GTK_IMAGE(k->image), "start-here", GTK_ICON_SIZE_BUTTON);

    if (k->label_data.enabled)
        set_label_markup(k->label, txt);

    if (k->has_top && k->top_window && k->top_image && top_pix) {
        k->top_w = gdk_pixbuf_get_width(top_pix);
        k->top_h = gdk_pixbuf_get_height(top_pix);

        gtk_image_set_from_pixbuf(GTK_IMAGE(k->top_image), top_pix);
        gtk_widget_set_size_request(k->top_event_box, k->top_w, k->top_h);
        gtk_window_resize(GTK_WINDOW(k->top_window),
                          MAX(1, k->top_w), MAX(1, k->top_h));
        position_top_window(k);
    }
}

static void refresh_state(KesuPlugin *k) {
    if (k->pressed || k->menu_active)
        set_state(k, "pressed");
    else if (k->pointer_inside)
        set_state(k, "hover");
    else
        set_state(k, "normal");
}

static gboolean menu_process_running(void) {
    gchar *pidfile;
    gchar *contents = NULL;
    gsize length = 0;
    gint64 parsed;
    pid_t pid;
    gboolean running = FALSE;

    pidfile = g_strdup_printf(
        "/tmp/xfcemenu-%s.pid",
        g_get_user_name() ? g_get_user_name() : "");

    if (!g_file_get_contents(pidfile, &contents, &length, NULL) ||
        !contents || length == 0) {
        g_free(contents);
        g_free(pidfile);
        return FALSE;
    }

    parsed = g_ascii_strtoll(contents, NULL, 10);
    g_free(contents);
    g_free(pidfile);

    if (parsed <= 1)
        return FALSE;

    pid = (pid_t)parsed;

    errno = 0;
    if (kill(pid, 0) == 0 || errno == EPERM)
        running = TRUE;

    return running;
}

static gboolean launcher_geometry(KesuPlugin *k, gint *x, gint *y, gint *w, gint *h) {
    GtkWidget *toplevel;
    GdkWindow *top_window;
    gint local_x = 0;
    gint local_y = 0;
    gint root_x = 0;
    gint root_y = 0;

    if (!k || !k->event_box || !gtk_widget_get_realized(k->event_box))
        return FALSE;

    /*
     * event_box usa visible_window=FALSE, así que gtk_widget_get_window()
     * puede devolver la GdkWindow compartida del panel. Traducimos el origen
     * real del widget al toplevel y luego a coordenadas de pantalla.
     */
    toplevel = gtk_widget_get_toplevel(k->event_box);

    if (!toplevel ||
        !GTK_IS_WINDOW(toplevel) ||
        !gtk_widget_get_realized(toplevel))
        return FALSE;

    if (!gtk_widget_translate_coordinates(
            k->event_box, toplevel, 0, 0, &local_x, &local_y))
        return FALSE;

    top_window = gtk_widget_get_window(toplevel);
    if (!top_window)
        return FALSE;

    gdk_window_get_origin(top_window, &root_x, &root_y);

    *x = root_x + local_x;
    *y = root_y + local_y;
    *w = gtk_widget_get_allocated_width(k->event_box);
    *h = gtk_widget_get_allocated_height(k->event_box);

    return (*w > 0 && *h > 0);
}

static const gchar *panel_position_name(KesuPlugin *k, gint center_x, gint center_y) {
    XfceScreenPosition pos =
        xfce_panel_plugin_get_screen_position(k->plugin);

    if (xfce_screen_position_is_top(pos)) return "top";
    if (xfce_screen_position_is_bottom(pos)) return "bottom";
    if (xfce_screen_position_is_left(pos)) return "left";
    if (xfce_screen_position_is_right(pos)) return "right";

    /*
     * Panel flotante/desconocido: inferir borde usando la posición real del
     * botón, no asumir panel inferior.
     */
    {
        GtkOrientation orientation =
            xfce_panel_plugin_get_orientation(k->plugin);
        GdkScreen *screen = gtk_widget_get_screen(k->event_box);

        if (screen) {
            gint monitor =
                gdk_screen_get_monitor_at_point(screen, center_x, center_y);
            GdkRectangle geometry;

            gdk_screen_get_monitor_geometry(screen, monitor, &geometry);

            if (orientation == GTK_ORIENTATION_VERTICAL)
                return center_x < geometry.x + geometry.width / 2
                    ? "left" : "right";

            return center_y < geometry.y + geometry.height / 2
                ? "top" : "bottom";
        }

        return orientation == GTK_ORIENTATION_VERTICAL
            ? "left" : "bottom";
    }
}

static void position_top_window(KesuPlugin *k) {
    gint x = 0, y = 0, w = 0, h = 0;
    gint overlay_x;
    gint overlay_y;
    const gchar *edge;

    if (!k || !k->has_top || !k->top_window ||
        !k->top_normal_pixbuf || k->top_w <= 0 || k->top_h <= 0)
        return;

    if (!launcher_geometry(k, &x, &y, &w, &h)) {
        gtk_widget_hide(k->top_window);
        return;
    }

    edge = panel_position_name(k, x + w / 2, y + h / 2);

    /*
     * El <Top> legacy sobresale hacia el escritorio:
     * panel superior -> debajo del panel
     * panel inferior -> encima del panel
     *
     * Se alinea con la imagen real (el EventBox de Kesu tiene 2 px de
     * padding horizontal a cada lado).
     */
    overlay_x = x + KESU_HORIZONTAL_PADDING / 2;

    if (g_strcmp0(edge, "top") == 0) {
        overlay_y = y + h;
    } else if (g_strcmp0(edge, "bottom") == 0) {
        overlay_y = y - k->top_h;
    } else {
        /*
         * GnoMenu sólo definía esta semántica para paneles horizontales.
         * Mantener oculto el overlay es preferible a rotar/deformar un skin.
         */
        gtk_widget_hide(k->top_window);
        return;
    }

    gtk_window_move(GTK_WINDOW(k->top_window), overlay_x, overlay_y);
    gtk_widget_show_all(k->top_window);
}

static gchar *launcher_command(KesuPlugin *k) {
    const gchar *launcher =
        (k->launcher_path &&
         g_file_test(k->launcher_path, G_FILE_TEST_EXISTS))
        ? k->launcher_path : NULL;

    gchar *fallback = NULL;
    gchar *base;
    gint x = 0, y = 0, w = 0, h = 0;

    if (!launcher) {
        fallback = home_build(".local/bin/xfcemenu");
        launcher = fallback;
    }

    if (launcher && g_file_test(launcher, G_FILE_TEST_EXISTS))
        base = g_shell_quote(launcher);
    else
        base = g_strdup("xfcemenu");

    g_free(fallback);

    if (!launcher_geometry(k, &x, &y, &w, &h))
        return base;

    {
        const gchar *panel_position =
            panel_position_name(k, x + w / 2, y + h / 2);
        gchar *cmd = g_strdup_printf(
            "%s --anchor-x %d --anchor-y %d "
            "--anchor-width %d --anchor-height %d "
            "--panel-position %s",
            base, x, y, w, h, panel_position);

        g_message("Kesú: launcher anchor %s %d,%d %dx%d",
                  panel_position, x, y, w, h);
        g_free(base);
        return cmd;
    }
}

static gboolean on_enter(GtkWidget *w G_GNUC_UNUSED,
                         GdkEventCrossing *e G_GNUC_UNUSED,
                         gpointer data) {
    KesuPlugin *k = data;
    k->pointer_inside = TRUE;
    refresh_state(k);
    return FALSE;
}

static gboolean on_leave(GtkWidget *w G_GNUC_UNUSED,
                         GdkEventCrossing *e G_GNUC_UNUSED,
                         gpointer data) {
    KesuPlugin *k = data;
    k->pointer_inside = FALSE;
    k->pressed = FALSE;
    refresh_state(k);
    return FALSE;
}

static gboolean on_press(GtkWidget *w G_GNUC_UNUSED,
                         GdkEventButton *e,
                         gpointer data) {
    KesuPlugin *k = data;

    if (e->button == 1) {
        k->pressed = TRUE;
        refresh_state(k);
        return TRUE;
    }

    return FALSE;
}

static gboolean on_release(GtkWidget *w G_GNUC_UNUSED,
                           GdkEventButton *e,
                           gpointer data) {
    KesuPlugin *k = data;

    if (e->button == 1) {
        gboolean was_active = menu_process_running();
        gchar *cmd = launcher_command(k);
        GError *err = NULL;
        gboolean launched;

        k->pressed = FALSE;

        launched = g_spawn_command_line_async(cmd, &err);

        if (!launched) {
            if (err) {
                g_warning("Kesú: no se pudo lanzar '%s': %s",
                          cmd, err->message);
                g_error_free(err);
            }
            k->menu_active = was_active;
        } else {
            /*
             * El launcher de Angujanu es toggle. Esto reproduce el estado 2
             * de GnoMenu: ImagePressed permanece mientras el menú está activo.
             * El timer lo corrige si el proceso termina por otra vía.
             */
            k->menu_active = !was_active;
        }

        g_free(cmd);
        refresh_state(k);
        return TRUE;
    }

    return FALSE;
}

static gboolean sync_menu_state(gpointer data) {
    KesuPlugin *k = data;
    gboolean active;

    if (!k)
        return G_SOURCE_REMOVE;

    active = menu_process_running();

    if (active != k->menu_active) {
        k->menu_active = active;
        refresh_state(k);
    }

    if (k->has_top)
        position_top_window(k);

    return G_SOURCE_CONTINUE;
}

static gboolean on_size_changed(XfcePanelPlugin *plugin G_GNUC_UNUSED,
                                gint size,
                                gpointer data) {
    KesuPlugin *k = data;

    k->panel_size = size;
    reload_pixbufs(k);
    refresh_state(k);
    position_top_window(k);
    return TRUE;
}

static void on_screen_position_changed(
    XfcePanelPlugin *plugin G_GNUC_UNUSED,
    XfceScreenPosition position G_GNUC_UNUSED,
    gpointer data) {
    KesuPlugin *k = data;

    /*
     * Necesario para aplicar/quitar el flip del Background al mover el panel
     * entre arriba y abajo y para recolocar la segunda ventana.
     */
    reload_pixbufs(k);
    refresh_state(k);
    position_top_window(k);
}

static void free_kesu(XfcePanelPlugin *plugin G_GNUC_UNUSED, gpointer data) {
    KesuPlugin *k = data;

    if (!k) return;

    if (k->menu_watch_id) {
        g_source_remove(k->menu_watch_id);
        k->menu_watch_id = 0;
    }

    if (k->top_window) {
        gtk_widget_destroy(k->top_window);
        k->top_window = NULL;
    }

    unref_pixbuf(&k->normal_pixbuf);
    unref_pixbuf(&k->hover_pixbuf);
    unref_pixbuf(&k->pressed_pixbuf);
    unref_pixbuf(&k->top_normal_pixbuf);
    unref_pixbuf(&k->top_hover_pixbuf);
    unref_pixbuf(&k->top_pressed_pixbuf);

    g_free(k->button_theme);
    g_free(k->button_themes_dir);
    g_free(k->theme_dir);
    g_free(k->launcher_path);

    clear_theme_paths(k);
    label_clear(&k->label_data);
    g_free(k);
}

static void kesu_construct(XfcePanelPlugin *plugin) {
    KesuPlugin *k = g_new0(KesuPlugin, 1);

    k->plugin = plugin;
    k->panel_size = xfce_panel_plugin_get_size(plugin);
    k->scale = 1.0;

    load_config(k);
    parse_theme(k);

    k->event_box = gtk_event_box_new();
    k->fixed = gtk_fixed_new();
    k->image = gtk_image_new();
    k->label = gtk_label_new(NULL);

    gtk_event_box_set_visible_window(GTK_EVENT_BOX(k->event_box), FALSE);
    gtk_container_add(GTK_CONTAINER(k->event_box), k->fixed);
    gtk_fixed_put(GTK_FIXED(k->fixed), k->image, 0, 0);
    gtk_fixed_put(GTK_FIXED(k->fixed), k->label, 0, 0);

    gtk_widget_add_events(
        k->event_box,
        GDK_BUTTON_PRESS_MASK |
        GDK_BUTTON_RELEASE_MASK |
        GDK_ENTER_NOTIFY_MASK |
        GDK_LEAVE_NOTIFY_MASK);

    g_signal_connect(k->event_box, "enter-notify-event", G_CALLBACK(on_enter), k);
    g_signal_connect(k->event_box, "leave-notify-event", G_CALLBACK(on_leave), k);
    g_signal_connect(k->event_box, "button-press-event", G_CALLBACK(on_press), k);
    g_signal_connect(k->event_box, "button-release-event", G_CALLBACK(on_release), k);

    g_signal_connect(plugin, "size-changed", G_CALLBACK(on_size_changed), k);
    g_signal_connect(plugin, "screen-position-changed",
                     G_CALLBACK(on_screen_position_changed), k);
    g_signal_connect(plugin, "free-data", G_CALLBACK(free_kesu), k);

    if (k->has_top)
        create_top_window(k);

    reload_pixbufs(k);
    k->menu_active = menu_process_running();
    refresh_state(k);

    gtk_container_add(GTK_CONTAINER(plugin), k->event_box);
    xfce_panel_plugin_add_action_widget(plugin, k->event_box);

    gtk_widget_show_all(GTK_WIDGET(plugin));

    if (!k->label_data.enabled)
        gtk_widget_hide(k->label);

    position_top_window(k);

    /*
     * También mantiene la posición del overlay si el usuario reordena Kesu
     * dentro del panel y sincroniza el estado pressed con el menú real.
     */
    k->menu_watch_id =
        g_timeout_add(KESU_MENU_WATCH_MS, sync_menu_state, k);
}

XFCE_PANEL_PLUGIN_REGISTER(kesu_construct)
