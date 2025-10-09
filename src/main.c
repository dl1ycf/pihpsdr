/* Copyright (C)
* 2015 - John Melton, G0ORX/N6LYT
* 2025 - Christoph van Wüllen, DL1YCF
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*/
#include <gtk/gtk.h>
#include <math.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <wdsp.h>    // only needed for WDSPwisdom() and wisdom_get_status()

#include "actions.h"
#include "appearance.h"
#include "audio.h"
#include "band.h"
#include "bandstack.h"
#include "css.h"
#include "discovery.h"
#include "discovered.h"
#include "ext.h"
#include "gpio.h"
#include "piHPSDR_logo.h"
#include "main.h"
#include "message.h"
#include "new_menu.h"
#include "new_protocol.h"
#include "old_protocol.h"
#include "radio.h"
#ifdef SATURN
  #include "saturnmain.h"
#endif
#ifdef SOAPYSDR
  #include "soapy_protocol.h"
#endif
#include "startup.h"
#include "test_menu.h"
#include "version.h"
#include "vfo.h"

struct utsname unameData;

GdkScreen *screen;
int display_size;
int display_width[6] = {0, 0, 640, 800, 1024, 1280};
int display_height[6] = {0, 0, 400, 480, 600, 720};
int display_vfobar[6] = {0, 0, 0, 0, 0, 0};
int this_monitor;

static GdkCursor *cursor_arrow;
static GdkCursor *cursor_watch;

GtkWidget *top_window = NULL;
GtkWidget *topgrid;
gulong keypress_signal_id = 0;

static GtkWidget *status_label;

void status_text(const char *text) {
  gtk_label_set_text(GTK_LABEL(status_label), text);
  usleep(100000);

  while (gtk_events_pending ()) {
    gtk_main_iteration ();
  }
}

static pthread_t wisdom_thread_id;
static int wisdom_running = 0;

static void* wisdom_thread(void *arg) {
  if (WDSPwisdom ((char *)arg)) {
    t_print("%s: WDSP wisdom file has been rebuilt.\n", __FUNCTION__);
  } else {
    t_print("%s: Re-using existing WDSP wisdom file.\n", __FUNCTION__);
  }

  wisdom_running = 0;
  return NULL;
}

// cppcheck-suppress constParameterCallback
static gboolean main_delete (GtkWidget *widget) {
  if (radio != NULL) {
    radio_stop_program();
  }

  _exit(0);
}

static int init(void *data) {
  char wisdom_directory[1025];
  char text[1024];
  t_print("%s\n", __FUNCTION__);
  //
  // We want to intercept some key strokes
  //
  gtk_widget_add_events(top_window, GDK_KEY_PRESS_MASK);
  keypress_signal_id = g_signal_connect(top_window, "key_press_event", G_CALLBACK(discovery_keypress_cb), NULL);
  audio_get_cards();
  cursor_arrow = gdk_cursor_new(GDK_ARROW);
  cursor_watch = gdk_cursor_new(GDK_WATCH);
  gdk_window_set_cursor(gtk_widget_get_window(top_window), cursor_watch);
  //
  // Let WDSP (via FFTW) check for wisdom file in current dir
  // If there is one, the "wisdom thread" takes no time
  // Depending on the WDSP version, the file is wdspWisdom or wdspWisdom00.
  //
  (void) getcwd(text, sizeof(text));
  snprintf(wisdom_directory, sizeof(wisdom_directory), "%s/", text);
  t_print("%s: Securing wisdom file in directory: %s\n", __FUNCTION__, wisdom_directory);
  status_text("Checking FFTW Wisdom file ...");
  wisdom_running = 1;
  pthread_create(&wisdom_thread_id, NULL, wisdom_thread, wisdom_directory);

  while (wisdom_running) {
    // wait for the wisdom thread to complete, meanwhile
    // handling any GTK events.
    usleep(100000); // 100ms

    while (gtk_events_pending ()) {
      gtk_main_iteration ();
    }

    snprintf(text, sizeof(text), "Do not close window until wisdom plans are completed ...\n\n... %s",
             wisdom_get_status());
    status_text(text);
  }

  //
  // When widsom plans are complete, start discovery process
  //
  g_timeout_add(100, delayed_discovery, NULL);
  return 0;
}

static void activate_pihpsdr(GtkApplication *app, gpointer data) {
  GtkWidget *label;
  char text[256];
  t_print("%s: Build: %s (Commit: %s, Date: %s)\n", __FUNCTION__, build_version, build_commit, build_date);
  t_print("%s: GTK+ version %u.%u.%u\n", __FUNCTION__, gtk_major_version, gtk_minor_version, gtk_micro_version);
  uname(&unameData);
  t_print("%s: sysname=  %s\n", __FUNCTION__, unameData.sysname);
  t_print("%s: nodename= %s\n", __FUNCTION__, unameData.nodename);
  t_print("%s: release=  %s\n", __FUNCTION__, unameData.release);
  t_print("%s: version=  %s\n", __FUNCTION__, unameData.version);
  t_print("%s: machine=  %s\n", __FUNCTION__, unameData.machine);
  load_css();
  //
  // Start with default font. The selected
  // becomes active if the radio is started
  //
  load_font(0);
  GdkDisplay *display = gdk_display_get_default();

  if (display == NULL) {
    t_print("%s: FATAL: no default display!\n", __FUNCTION__);
    _exit(0);
  }

  screen = gdk_display_get_default_screen(display);

  if (screen == NULL) {
    t_print("%s: FATAL: no default screen!\n", __FUNCTION__);
    _exit(0);
  }

  //
  // Create top window with minimum size
  //
  top_window = gtk_application_window_new (app);
  gtk_widget_set_size_request(top_window, 100, 100);
  gtk_window_set_title (GTK_WINDOW (top_window), "piHPSDR");
  //
  // do not use GTK_WIN_POS_CENTER_ALWAYS, since this will let the
  // window jump back to the center each time the window is
  // re-created, e.g. in reconfigure_radio()
  //
  // Note: enabling "resizable" leads to strange behaviour in the
  //       Wayland window manager so we  suppress this. All resize
  //       events are "programmed" and not "user intervention"
  //       anyway.
  //
  gtk_window_set_position(GTK_WINDOW(top_window), GTK_WIN_POS_CENTER);
  gtk_window_set_resizable(GTK_WINDOW(top_window), FALSE);
  //
  // Get the position of the top window, and then determine
  // to which monitor this position belongs.
  //
  int x, y;
  gtk_window_get_position(GTK_WINDOW(top_window), &x, &y);
  this_monitor = gdk_screen_get_monitor_at_point(screen, x, y);
  t_print("%s: Monitor Number within Screen=%d\n", __FUNCTION__, this_monitor);
  //
  // Determine the size of "our" monitor
  //
  GdkRectangle rect;
  gdk_screen_get_monitor_geometry(screen, this_monitor, &rect);
  // Start with 800x480, since this width is required for the "discovery" screen.
  // Go to "full screen" mode if display nearly matches 800x480
  // This is all overridden later for the radio from the props file
  display_width[0] = rect.width;
  display_height[0] = rect.height;
  display_width[1]  = 800;
  display_height[1] = 480;
  display_size   = 1;  // Custom
  t_print("%s: Monitor: width=%d height=%d\n", __FUNCTION__, display_width[0], display_height[0]);

  //
  // Go to full-screen mode by default, if the screen size is approx. 800*480
  //
  if (display_width[0] > 780 && display_width[0] < 820 && display_height[0] > 460 && display_height[0] < 500) {
    display_size = 0;   // FullScreen
  }

  t_print("%s: display_width=%d display_height=%d\n", __FUNCTION__,
          display_width[display_size], display_height[display_size]);

  if (display_size == 0) {
    t_print("%s: Going full screen\n", __FUNCTION__);
    gtk_window_fullscreen_on_monitor(GTK_WINDOW(top_window), screen, this_monitor);
  } else {
    t_print("%s: display_width=%d display_height=%d\n", __FUNCTION__,
            display_width[display_size], display_height[display_size]);
  }

  g_signal_connect (top_window, "delete-event", G_CALLBACK (main_delete), NULL);
  topgrid = gtk_grid_new();
  gtk_widget_set_size_request(topgrid, display_width[display_size], display_height[display_size]);
  gtk_grid_set_row_homogeneous(GTK_GRID(topgrid), FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(topgrid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(topgrid), 10);
  gtk_container_add (GTK_CONTAINER (top_window), topgrid);
  //
  // Closely following Heiko's suggestion, we now have the HPSDR log contained
  // in the code and need not fiddle around with the question from where to load it.
  //
  GtkWidget *image = piHPSDR_logo();

  if (image) {
    gtk_grid_attach(GTK_GRID(topgrid), image, 0, 0, 1, 3);
  }

  label = gtk_label_new("piHPSDR");
  gtk_widget_set_name(label, "big_txt");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(topgrid), label, 1, 0, 3, 1);
  label = gtk_label_new("originally written by John Melton (G0ORX/N6LYT)\n"
                        "extended and maintained by Christoph van Wüllen (DL1YCF)");
  gtk_widget_set_name(label, "med_txt");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(topgrid), label, 1, 1, 3, 1);
  snprintf(text, sizeof(text), "Built %s, Version %s (commit=%s)\nOptions: %s\nAudio module: %s",
           build_date, build_version, build_commit, build_options, build_audio);
  label = gtk_label_new(text);
  gtk_widget_set_name(label, "med_txt");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(topgrid), label, 1, 2, 3, 1);
  status_label = gtk_label_new(NULL);
  gtk_widget_set_name(status_label, "med_txt");
  gtk_widget_set_halign(status_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(topgrid), status_label, 1, 3, 3, 1);
  gtk_widget_show_all(top_window);
  g_idle_add(init, NULL);
}

int main(int argc, char **argv) {
  GtkApplication *pihpsdr;
  int rc;
  char name[1024];

  //
  // If invoked with -V, print version and FPGA firmware compatibility information
  //
  if (argc >= 2 && !strcmp("-V", argv[1])) {
    fprintf(stderr, "piHPSDR version %s(%s); built %s\n", build_version, build_commit, build_date);
    fprintf(stderr, "Compile-time options      : %sAudioModule=%s\n", build_options, build_audio);
#ifdef SATURN
    fprintf(stderr, "SATURN min:max minor FPGA : %d:%d\n", saturn_minor_version_min(), saturn_minor_version_max());
    fprintf(stderr, "SATURN min:max major FPGA : %d:%d\n", saturn_major_version_min(), saturn_major_version_max());
#endif
    exit(0);
  }

  //
  // If invoked with -TestMenu, then set a flag for using the test menu
  // (debug and program development only)
  //
  if (argc >= 2 && !strcmp("-TestMenu", argv[1])) {
    open_test_menu = 1;

    //
    // remove this argument from the list since GTK cannot handle it
    //
    for (int i = 2; i < argc; i++) {
      argv[i - 1] = argv[i];
    }

    argc--;
  }

  //
  // The following call will most likely fail (until this program
  // has the privileges to reduce the nice value). But if the
  // privilege is there, it may help to run piHPSDR at a lower nice
  // value.
  //
  rc = getpriority(PRIO_PROCESS, 0);
  t_print("%s: Base priority on startup: %d\n", __FUNCTION__, rc);
  setpriority(PRIO_PROCESS, 0, -10);
  rc = getpriority(PRIO_PROCESS, 0);
  t_print("%s: Base priority after adjustment: %d\n", __FUNCTION__, rc);
  startup(argv[0]);
  snprintf(name, sizeof(name), "org.g0orx.pihpsdr.pid%d", getpid());
  pihpsdr = gtk_application_new(name, G_APPLICATION_FLAGS_NONE);
  g_signal_connect(pihpsdr, "activate", G_CALLBACK(activate_pihpsdr), NULL);
  rc = g_application_run(G_APPLICATION(pihpsdr), argc, argv);
  t_print("%s: exiting ...\n", __FUNCTION__);
  g_object_unref(pihpsdr);
  return rc;
}

int fatal_error(void *data) {
  //
  // This replaces the calls to exit. It now emits
  // a GTK modal dialog waiting for user response.
  // After this response, the program exits.
  //
  // The red color chosen for the first string should
  // work both on the dark and light themes.
  //
  // Note this must only be called from the "main thread", that is,
  // you can only invoke this function via g_idle_add()
  //
  const gchar *msg = (gchar *) data;
  static int quit = 0;

  if (quit) {
    return G_SOURCE_REMOVE;
  }

  quit = 1;

  if (top_window) {
    GtkDialogFlags flags = GTK_DIALOG_DESTROY_WITH_PARENT;
    GtkWidget *dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW(top_window),
                        flags,
                        GTK_MESSAGE_ERROR,
                        GTK_BUTTONS_CLOSE,
                        "<span color='red' size='x-large' weight='bold'>piHPSDR warning/error message:</span>"
                        "\n\n<span size='x-large' weight='bold'>   %s</span>\n\n",
                        msg);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
  }

  if (!strncmp(msg, "FATAL", 5)) {
    exit(1);
  }

  quit = 0;
  return G_SOURCE_REMOVE;
}
