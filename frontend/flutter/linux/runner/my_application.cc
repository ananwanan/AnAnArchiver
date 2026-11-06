#include "my_application.h"

#include <flutter_linux/flutter_linux.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include "flutter/generated_plugin_registrant.h"

struct _MyApplication {
    GtkApplication parent_instance;
    char** dart_entrypoint_arguments;
    GtkWindow* window;
    FlMethodChannel* channel;
    FlValue* pending_open;
    bool desktop_ready;
};

G_DEFINE_TYPE(MyApplication, my_application, GTK_TYPE_APPLICATION)

static void window_method_call(FlMethodChannel*, FlMethodCall* call, gpointer user_data) {
    auto* self = MY_APPLICATION(user_data);
    auto* window = self->window;
    FlValue* value = fl_method_call_get_args(call);
    g_autoptr(FlMethodResponse) response = nullptr;
    const auto* method = fl_method_call_get_name(call);
    if (g_strcmp0(method, "ready") == 0) {
        self->desktop_ready = true;
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(self->pending_open));
        g_clear_pointer(&self->pending_open, fl_value_unref);
    } else if (g_strcmp0(method, "state") == 0) {
        int width, height;
        gtk_window_get_size(window, &width, &height);
        g_autoptr(FlValue) state = fl_value_new_map();
        fl_value_set_string_take(state, "width", fl_value_new_int(CLAMP(width, 640, 7680)));
        fl_value_set_string_take(state, "height", fl_value_new_int(CLAMP(height, 480, 4320)));
        fl_value_set_string_take(state, "maximized",
                                 fl_value_new_bool(gtk_window_is_maximized(window)));
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(state));
    } else if (g_strcmp0(method, "restore") == 0 && fl_value_get_type(value) == FL_VALUE_TYPE_MAP) {
        auto* width = fl_value_lookup_string(value, "width");
        auto* height = fl_value_lookup_string(value, "height");
        auto* maximized = fl_value_lookup_string(value, "maximized");
        if (width && height && fl_value_get_type(width) == FL_VALUE_TYPE_INT &&
            fl_value_get_type(height) == FL_VALUE_TYPE_INT)
            gtk_window_resize(window, static_cast<int>(CLAMP(fl_value_get_int(width), 640, 7680)),
                              static_cast<int>(CLAMP(fl_value_get_int(height), 480, 4320)));
        if (maximized && fl_value_get_type(maximized) == FL_VALUE_TYPE_BOOL &&
            fl_value_get_bool(maximized))
            gtk_window_maximize(window);
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    } else if (g_strcmp0(fl_method_call_get_name(call), "title") == 0 &&
               fl_value_get_type(value) == FL_VALUE_TYPE_STRING) {
        const gchar* title = fl_value_get_string(value);
        gtk_window_set_title(window, title);
        GtkWidget* bar = gtk_window_get_titlebar(window);
        if (GTK_IS_HEADER_BAR(bar))
            gtk_header_bar_set_title(GTK_HEADER_BAR(bar), title);
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
    } else {
        response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
    }
    g_autoptr(GError) error = nullptr;
    if (!fl_method_call_respond(call, response, &error))
        g_warning("Window method: %s", error->message);
}

// Called when first Flutter frame received.
static void first_frame_cb(MyApplication* self, FlView* view) {
    gtk_widget_show(gtk_widget_get_toplevel(GTK_WIDGET(view)));
}

// Implements GApplication::activate.
static void my_application_activate(GApplication* application) {
    MyApplication* self = MY_APPLICATION(application);
    if (self->window) {
        gtk_window_present(self->window);
        return;
    }
    GtkWindow* window = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(application)));
    self->window = window;

    // Use a header bar when running in GNOME as this is the common style used
    // by applications and is the setup most users will be using (e.g. Ubuntu
    // desktop).
    // If running on X and not using GNOME then just use a traditional title bar
    // in case the window manager does more exotic layout, e.g. tiling.
    // If running on Wayland assume the header bar will work (may need changing
    // if future cases occur).
    gboolean use_header_bar = TRUE;
#ifdef GDK_WINDOWING_X11
    GdkScreen* screen = gtk_window_get_screen(window);
    if (GDK_IS_X11_SCREEN(screen)) {
        const gchar* wm_name = gdk_x11_screen_get_window_manager_name(screen);
        if (g_strcmp0(wm_name, "GNOME Shell") != 0) {
            use_header_bar = FALSE;
        }
    }
#endif
    if (use_header_bar) {
        GtkHeaderBar* header_bar = GTK_HEADER_BAR(gtk_header_bar_new());
        gtk_widget_show(GTK_WIDGET(header_bar));
        gtk_header_bar_set_title(header_bar, "LiteArc");
        gtk_header_bar_set_show_close_button(header_bar, TRUE);
        gtk_window_set_titlebar(window, GTK_WIDGET(header_bar));
    } else {
        gtk_window_set_title(window, "LiteArc");
    }

    gtk_window_set_default_size(window, 1280, 720);

    g_autoptr(FlDartProject) project = fl_dart_project_new();
    fl_dart_project_set_dart_entrypoint_arguments(project, self->dart_entrypoint_arguments);

    FlView* view = fl_view_new(project);
    GdkRGBA background_color;
    // Background defaults to black, override it here if necessary, e.g. #00000000
    // for transparent.
    gdk_rgba_parse(&background_color, "#000000");
    fl_view_set_background_color(view, &background_color);
    gtk_widget_show(GTK_WIDGET(view));
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(view));

    // Show the window when Flutter renders.
    // Requires the view to be realized so we can start rendering.
    g_signal_connect_swapped(view, "first-frame", G_CALLBACK(first_frame_cb), self);
    gtk_widget_realize(GTK_WIDGET(view));

    fl_register_plugins(FL_PLUGIN_REGISTRY(view));
    g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
    FlMethodChannel* channel =
        fl_method_channel_new(fl_engine_get_binary_messenger(fl_view_get_engine(view)),
                              "litearc/window", FL_METHOD_CODEC(codec));
    self->channel = channel;
    fl_method_channel_set_method_call_handler(channel, window_method_call, self, nullptr);

    gtk_widget_grab_focus(GTK_WIDGET(view));
}

// GApplication routes startup paths and subsequent file-open events to one process.
static void my_application_open(GApplication* application, GFile** files, gint count,
                                const gchar*) {
    auto* self = MY_APPLICATION(application);
    my_application_activate(application);
    g_autoptr(FlValue) paths = fl_value_new_list();
    for (int i = 0; i < MIN(count, 2); ++i) {
        g_autofree gchar* path = g_file_get_path(files[i]);
        fl_value_append_take(paths, fl_value_new_string(path ? path : "unsupported://file"));
    }
    if (self->desktop_ready)
        fl_method_channel_invoke_method(self->channel, "openFiles", paths, nullptr, nullptr,
                                        nullptr);
    else {
        g_clear_pointer(&self->pending_open, fl_value_unref);
        self->pending_open = fl_value_ref(paths);
    }
}

// Implements GApplication::startup.
static void my_application_startup(GApplication* application) {
    // MyApplication* self = MY_APPLICATION(object);

    // Perform any actions required at application startup.

    G_APPLICATION_CLASS(my_application_parent_class)->startup(application);
}

// Implements GApplication::shutdown.
static void my_application_shutdown(GApplication* application) {
    // MyApplication* self = MY_APPLICATION(object);

    // Perform any actions required at application shutdown.

    G_APPLICATION_CLASS(my_application_parent_class)->shutdown(application);
}

// Implements GObject::dispose.
static void my_application_dispose(GObject* object) {
    MyApplication* self = MY_APPLICATION(object);
    g_clear_pointer(&self->dart_entrypoint_arguments, g_strfreev);
    g_clear_pointer(&self->pending_open, fl_value_unref);
    g_clear_object(&self->channel);
    G_OBJECT_CLASS(my_application_parent_class)->dispose(object);
}

static void my_application_class_init(MyApplicationClass* klass) {
    G_APPLICATION_CLASS(klass)->activate = my_application_activate;
    G_APPLICATION_CLASS(klass)->open = my_application_open;
    G_APPLICATION_CLASS(klass)->startup = my_application_startup;
    G_APPLICATION_CLASS(klass)->shutdown = my_application_shutdown;
    G_OBJECT_CLASS(klass)->dispose = my_application_dispose;
}

static void my_application_init(MyApplication* self) {}

MyApplication* my_application_new() {
    // Set the program name to the application ID, which helps various systems
    // like GTK and desktop environments map this running application to its
    // corresponding .desktop file. This ensures better integration by allowing
    // the application to be recognized beyond its binary name.
    g_set_prgname(APPLICATION_ID);

    return MY_APPLICATION(g_object_new(my_application_get_type(), "application-id", APPLICATION_ID,
                                       "flags", G_APPLICATION_HANDLES_OPEN, nullptr));
}
