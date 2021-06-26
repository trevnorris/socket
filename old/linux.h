//
// ====================================================================
//
// This implementation uses webkit2gtk backend. It requires gtk+3.0 and
// webkit2gtk-4.0 libraries. Proper compiler flags can be retrieved via:
//
//   pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.0
//
// ====================================================================
//
#include <JavaScriptCore/JavaScript.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include "util.h"

namespace fs = std::filesystem;

std::string getCwd (const std::string);

inline std::string getCwd (const std::string argvp) {
  auto canonical = fs::canonical("/proc/self/exe");
  return std::string(fs::path(canonical).parent_path());
}

inline std::string createNativeDialog(
  int flags,
  const char *_,
  const char *default_path,
  const char *default_name)
  {

  GtkWidget *dialog;
  GtkFileFilter *filter;
  GtkFileChooser *chooser;
  GtkFileChooserAction action;
  gint res;
  char buf[128], *patterns;

  action = flags & NOC_FILE_DIALOG_SAVE
    ? GTK_FILE_CHOOSER_ACTION_SAVE
    : GTK_FILE_CHOOSER_ACTION_OPEN;

  if (flags & NOC_FILE_DIALOG_DIR) {
    action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
  }

  gtk_init_check(NULL, NULL);

  dialog = gtk_file_chooser_dialog_new(
    flags & NOC_FILE_DIALOG_SAVE
      ? "Save File"
      : "Open File",
    NULL,
    action,
    "_Cancel",
    GTK_RESPONSE_CANCEL,
    flags & NOC_FILE_DIALOG_SAVE
      ? "_Save"
      : "_Open",
    GTK_RESPONSE_ACCEPT,
    NULL
  );

  chooser = GTK_FILE_CHOOSER(dialog);

  if (flags & NOC_FILE_DIALOG_OVERWRITE_CONFIRMATION) {
    gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
  }

  if (default_path != nullptr) {
    gtk_file_chooser_set_filename(chooser, default_path);
  }

  if (default_name != nullptr) {
    gtk_file_chooser_set_current_name(chooser, default_name);
  }

  if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
    return std::string("");
  }

  auto filename = gtk_file_chooser_get_filename(chooser);

  gtk_widget_destroy(dialog);

  while (gtk_events_pending()) {
    gtk_main_iteration();
  }

  return std::string(filename);
}

namespace Opkit {

class gtk_webkit_engine {
  public:

  gtk_webkit_engine(bool debug, void *window)
    : m_window(static_cast<GtkWidget *>(window)) {

    setenv("GTK_OVERLAY_SCROLLING", "1", 1);

    gtk_init_check(0, NULL);
    m_window = static_cast<GtkWidget *>(window);

    if (m_window == nullptr) {
      m_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    }

    g_signal_connect(
      G_OBJECT(m_window),
      "destroy",
      G_CALLBACK(+[](GtkWidget *, gpointer arg) {
        static_cast<gtk_webkit_engine *>(arg)->terminate();
        // TODO kill the backend process from here or create a cb
      }),
      this
    );

    // Initialize webview widget
    m_webview = webkit_web_view_new();

    WebKitUserContentManager *manager =
      webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(m_webview));

    g_signal_connect(
      manager,
      "script-message-received::external",
      G_CALLBACK(+[](
        WebKitUserContentManager*,
        WebKitJavascriptResult *r,
        gpointer arg) {
        auto *w = static_cast<gtk_webkit_engine *>(arg);
#if WEBKIT_MAJOR_VERSION >= 2 && WEBKIT_MINOR_VERSION >= 22
        JSCValue *value =
          webkit_javascript_result_get_js_value(r);
        char *s = jsc_value_to_string(value);
#else
        JSGlobalContextRef ctx =
          webkit_javascript_result_get_global_context(r);
        JSValueRef value = webkit_javascript_result_get_value(r);
        JSStringRef js = JSValueToStringCopy(ctx, value, NULL);
        size_t n = JSStringGetMaximumUTF8CStringSize(js);
        char *s = g_new(char, n);
        JSStringGetUTF8CString(js, s, n);
        JSStringRelease(js);
#endif
        w->on_message(s);
        g_free(s);
      }),
      this
    );

    webkit_user_content_manager_register_script_message_handler(
      manager,
      "external"
    );

    init(
      "window.external = {"
      "  invoke: s => window.webkit.messageHandlers.external.postMessage(s)"
      "}"
    );

    m_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(m_window), m_vbox);

    // add the webview to the vertical box
    // gtk_container_add(GTK_CONTAINER(m_window), GTK_WIDGET(m_webview));
    gtk_widget_grab_focus(GTK_WIDGET(m_webview));

    WebKitSettings *settings =
      webkit_web_view_get_settings(WEBKIT_WEB_VIEW(m_webview));
    webkit_settings_set_javascript_can_access_clipboard(settings, true);

    if (debug) {
      webkit_settings_set_enable_write_console_messages_to_stdout(settings, true);
      webkit_settings_set_enable_developer_extras(settings, true);
    }

    gtk_box_pack_end(GTK_BOX(m_vbox), m_webview, TRUE, TRUE, 0);
  }

  void createContextMenu(std::string seq, std::string menuData) {
    GtkWidget *m_popup = gtk_menu_new();
    GtkWidget *item;

    menuData = replace(menuData, "_", "\n");

    auto menuItems = split(menuData, '\n');
    auto id = std::stoi(seq);

    for (auto itemData : menuItems) {
      auto pair = split(itemData, ':');

      if (pair[0].find("---") != -1) {
        item = gtk_separator_menu_item_new();
      } else {
        item = gtk_menu_item_new_with_label(pair[0].c_str());
        auto meta = std::string(seq + ";" + pair[0].c_str());
        gtk_widget_set_name(item, meta.c_str());

        g_signal_connect(
          G_OBJECT(item),
          "activate",
          G_CALLBACK(+[](GtkWidget *t, gpointer arg) {
            auto w = static_cast<Opkit::gtk_webkit_engine*>(arg);
            auto label = gtk_menu_item_get_label(GTK_MENU_ITEM(t));
            auto title = std::string(label);
            auto meta = gtk_widget_get_name(t);
            auto pair = split(meta, ';');
            auto seq = pair[0];

            w->eval(
              "(() => {"
              "  const detail = {"
              "    title: '" + title + "',"
              "    parent: 'contextMenu',"
              "    state: 0"
              "  };"

              "  window._ipc[" + seq + "].resolve(detail);"
              "  delete window._ipc[" + seq + "];"
              "})()"
            );
          }),
          this
        );
      }

      gtk_widget_show(item);
      gtk_menu_shell_append(GTK_MENU_SHELL(m_popup), item);
    }

    auto win = GDK_WINDOW(gtk_widget_get_window(m_window));
    auto seat = gdk_display_get_default_seat(gdk_display_get_default());
    auto mouse_device = gdk_seat_get_pointer(seat);

    GdkRectangle rect;
    gint x, y;

    gdk_window_get_device_position(win, mouse_device, &x, &y, NULL);

    rect.x = x;
    rect.y = y;
    rect.width = 0;
    rect.height = 0;

    gtk_menu_popup_at_rect(
      GTK_MENU(m_popup),
      win,
      &rect,
      GDK_GRAVITY_SOUTH_WEST,
      GDK_GRAVITY_NORTH_WEST,
      gtk_get_current_event()
    );
  }

  void about () {
    GtkWidget *dialog = gtk_dialog_new();
    gtk_window_set_default_size(GTK_WINDOW(dialog), 300, 200);

    GtkWidget *body = gtk_dialog_get_content_area(GTK_DIALOG(GTK_WINDOW(dialog)));
    GtkContainer *content = GTK_CONTAINER(body);

    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(
      "/usr/share/icons/hicolor/256x256/apps/operator.png",
      60,
      60,
      TRUE,
      NULL
    );

    GtkWidget *img = gtk_image_new_from_pixbuf(pixbuf);
    gtk_widget_set_margin_top(img, 20);
    gtk_widget_set_margin_bottom(img, 20);

    gtk_box_pack_start(GTK_BOX(content), img, FALSE, FALSE, 0);

    std::string title_value(appData["title"] + " " + appData["version"]);

    GtkWidget *label_title = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(label_title), title_value.c_str());
    gtk_container_add(content, label_title);

    GtkWidget *label_copyRight = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(label_copyRight), appData["copyRight"].c_str());
    gtk_container_add(content, label_copyRight);

    g_signal_connect(
      dialog,
      "response",
      G_CALLBACK(gtk_widget_destroy),
      NULL
    );

    gtk_widget_show_all(body);
    gtk_widget_show_all(dialog);
    gtk_window_set_title(GTK_WINDOW(dialog), "About");

    gtk_dialog_run(GTK_DIALOG(dialog));
  }

  void show () {
    gtk_widget_show_all(m_window);
  }

  void hide () {
    gtk_widget_hide(m_window);
  }

  void inspect () {
  }

  void menu(std::string menu) {
    if (menu.empty()) return void(0);

    GtkWidget *menubar = gtk_menu_bar_new();
    GtkAccelGroup *aclrs = gtk_accel_group_new();

    // deserialize the menu
    menu = replace(menu, "%%", "\n");

    // split on ;
    auto menus = split(menu, ';');

    for (auto m : menus) {
      auto menu = split(m, '\n');
      auto line = trim(menu[0]);
      if (line.empty()) continue;
      auto menuTitle = split(line, ':')[0];
      GtkWidget *subMenu = gtk_menu_new();
      GtkWidget *menuItem = gtk_menu_item_new_with_label(menuTitle.c_str());

      for (int i = 1; i < menu.size(); i++) {
        auto line = trim(menu[i]);
        if (line.empty()) continue;
        auto parts = split(line, ':');
        auto title = parts[0];
        std::string key = "";

        GtkWidget *item;

        if (parts[0].find("---") != -1) {
          item = gtk_separator_menu_item_new();
        } else {
          if (parts.size() > 1) {
            key = parts[1] == "_" ? "" : trim(parts[1]);
          }
          item = gtk_menu_item_new_with_label(title.c_str());

          // TODO(@heapwolf): how can we set the accellerator?
          // gtk_accel_group_connect(

          /* GClosure cb = G_CALLBACK(+[](GtkWidget* w, gpointer arg) {

          });

          gtk_accel_group_connect(
            aclrs,
            GDK_KEY_A,
            GDK_CONTROL_MASK,
            GTK_ACCEL_MASK,
            &cb
          ); */

          g_signal_connect(
            G_OBJECT(item),
            "activate",
            G_CALLBACK(+[](GtkWidget *t, gpointer arg) {
              auto w = static_cast<Opkit::gtk_webkit_engine*>(arg);
              auto title = gtk_menu_item_get_label(GTK_MENU_ITEM(t));
              auto parent = gtk_widget_get_name(t);

              if (std::string(title).find("About") == 0) {
                return w->about();
              }

              // if (std::string(title).find("Quit") == 0) {
              //  return w->terminate();
              //}

              // TODO(@heapwolf) can we get the state?
              w->eval(
                "(() => {"
                "  const detail = {"
                "    title: '" + std::string(title) + "',"
                "    parent: '" + std::string(parent) + "',"
                "    state: 0"
                "  };"

                "  const event = new window.CustomEvent('menuItemSelected', { detail });"
                "  window.dispatchEvent(event);"
                "})()"
              );
            }),
            this
          );

        }

        gtk_widget_set_name(item, menuTitle.c_str());
        gtk_menu_shell_append(GTK_MENU_SHELL(subMenu), item);
      }

      gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuItem), subMenu);
      gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menuItem);
    }

    gtk_box_pack_start(GTK_BOX(m_vbox), menubar, FALSE, FALSE, 0);
    gtk_widget_show_all(m_window);
  }

  void *window() { return (void*) m_window; }

  void run() {
    gtk_main();
  }

  void terminate() {
    gtk_main_quit();
  }

  void dispatch(std::function<void()> f) {
    g_idle_add_full(
      G_PRIORITY_HIGH_IDLE,
      (GSourceFunc)([](void* f) -> int {
        (*static_cast<dispatch_fn_t*>(f))();
        return G_SOURCE_REMOVE;
      }),
    new std::function<void()>(f),
    [](void* f) { delete static_cast<dispatch_fn_t *>(f); }
    );
  }

  void setTitle(const std::string title) {
    gtk_window_set_title(GTK_WINDOW(m_window), title.c_str());
  }

  void setSize(int width, int height, int hints) {
    show();

    gtk_window_set_resizable(
      GTK_WINDOW(m_window),
      hints != WEBVIEW_HINT_FIXED
    );

    if (hints == WEBVIEW_HINT_NONE) {
      gtk_window_resize(GTK_WINDOW(m_window), width, height);
    } else if (hints == WEBVIEW_HINT_FIXED) {
      gtk_widget_set_size_request(m_window, width, height);
    } else {
      GdkGeometry g;
      g.min_width = g.max_width = width;
      g.min_height = g.max_height = height;
      GdkWindowHints h =
          (hints == WEBVIEW_HINT_MIN ? GDK_HINT_MIN_SIZE : GDK_HINT_MAX_SIZE);
      // This defines either MIN_SIZE, or MAX_SIZE, but not both:
      gtk_window_set_geometry_hints(GTK_WINDOW(m_window), nullptr, &g, h);
    }
  }

  int openExternal(std::string url) {
    return gtk_show_uri_on_window(GTK_WINDOW(m_window), url.c_str(), GDK_CURRENT_TIME, NULL);
  }

  void navigate(const std::string url) {
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(m_webview), url.c_str());
  }

  void init(const std::string js) {
    WebKitUserContentManager *manager =
      webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(m_webview));

    webkit_user_content_manager_add_script(
      manager,
      webkit_user_script_new(
        js.c_str(),
        WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        NULL,
        NULL
      )
    );
  }

  void eval(const std::string js) {
    webkit_web_view_run_javascript(
      WEBKIT_WEB_VIEW(m_webview),
      js.c_str(),
      NULL,
      NULL,
      NULL
    );
  }

  GtkWidget *m_vbox;
  GtkWidget *m_window;
private:
  virtual void on_message(const std::string msg) = 0;
  GtkWidget *m_webview;
};

using browser_engine = gtk_webkit_engine;
} // namespace Opkit