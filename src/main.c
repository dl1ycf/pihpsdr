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
#include <gdk/gdk.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
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
#include "configure.h"
#include "css.h"
#include "discovery.h"
#include "discovered.h"
#include "exit_menu.h"
#include "ext.h"
#include "gpio.h"
#include "hpsdr_logo.h"
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
#ifdef TTS
#include "tts.h"
#endif
#include "version.h"
#include "vfo.h"

struct utsname unameData;

GdkScreen *screen;
int display_width;
int display_height;
int screen_height;
int screen_width;
int full_screen;
int this_monitor;

static GdkCursor *cursor_arrow;
static GdkCursor *cursor_watch;

GtkWidget *top_window = NULL;
GtkWidget *topgrid;

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
  WDSPwisdom ((char *)arg);
  wisdom_running = 0;
  return NULL;
}

// cppcheck-suppress constParameterCallback
gboolean keypress_cb(GtkWidget *widget, GdkEventKey *event, gpointer data) {
  gboolean ret = TRUE;

  // Ignore key-strokes until radio is ready
  if (radio == NULL) { return FALSE; }

  //
  // Intercept key-strokes. The "keypad" stuff
  // has been contributed by Ron.
  // Everything that is not intercepted is handled downstream.
  //
  // space             ==>  MOX
  // u                 ==>  active receiver VFO up
  // d                 ==>  active receiver VFO down
  // Keypad 0..9       ==>  NUMPAD 0 ... 9
  // Keypad Decimal    ==>  NUMPAD DEC
  // Keypad Subtract   ==>  NUMPAD BS
  // Keypad Divide     ==>  NUMPAD CL
  // Keypad Multiply   ==>  NUMPAD Hz
  // Keypad Add        ==>  NUMPAD kHz
  // Keypad Enter      ==>  NUMPAD MHz
  //
  // Function keys invoke Text-to-Speech machine
  // (see tts.c)
  // F1                ==>  Frequency
  // F2                ==>  Mode
  // F3                ==>  Filter width
  // F4                ==>  RX S-meter level
  // F5                ==>  TX drive
  // F6                ==>  Attenuation/Preamp
  //
  switch (event->keyval) {
#ifdef TTS
  case GDK_KEY_F1:
    tts_freq();
    break;

  case GDK_KEY_F2:
    tts_mode();
    break;

  case GDK_KEY_F3:
    tts_filter();
    break;

  case GDK_KEY_F4:
    tts_smeter();
    break;

  case GDK_KEY_F5:
    tts_txdrive();
    break;

  case GDK_KEY_F6:
    tts_atten();
    break;
#endif

  case GDK_KEY_space:
    radio_toggle_mox();
    break;

  case  GDK_KEY_d:
    vfo_step(-1);
    break;

  case GDK_KEY_u:
    vfo_step(1);
    break;

  //
  // Suggestion of Richard: using U and D for changing
  // the frequency of the "other" VFO in large steps
  // (useful for split operation)
  //
  case  GDK_KEY_U:
    vfo_id_step(1 - active_receiver->id, 10);
    break;

  case  GDK_KEY_D:
    vfo_id_step(1 - active_receiver->id, -10);
    break;

  //
  // This is a contribution of Ron, it uses a keypad for
  // entering a frequency
  //
  case GDK_KEY_KP_0:
    vfo_num_pad(0, active_receiver->id);
    break;

  case GDK_KEY_KP_1:
    vfo_num_pad(1, active_receiver->id);
    break;

  case GDK_KEY_KP_2:
    vfo_num_pad(2, active_receiver->id);
    break;

  case GDK_KEY_KP_3:
    vfo_num_pad(3, active_receiver->id);
    break;

  case GDK_KEY_KP_4:
    vfo_num_pad(4, active_receiver->id);
    break;

  case GDK_KEY_KP_5:
    vfo_num_pad(5, active_receiver->id);
    break;

  case GDK_KEY_KP_6:
    vfo_num_pad(6, active_receiver->id);
    break;

  case GDK_KEY_KP_7:
    vfo_num_pad(7, active_receiver->id);
    break;

  case GDK_KEY_KP_8:
    vfo_num_pad(8, active_receiver->id);
    break;

  case GDK_KEY_KP_9:
    vfo_num_pad(9, active_receiver->id);
    break;

  case GDK_KEY_KP_Divide:
    vfo_num_pad(-1, active_receiver->id);
    break;

  case GDK_KEY_KP_Multiply:
    vfo_num_pad(-2, active_receiver->id);
    break;

  case GDK_KEY_KP_Add:
    vfo_num_pad(-3, active_receiver->id);
    break;

  case GDK_KEY_KP_Enter:
    vfo_num_pad(-4, active_receiver->id);
    break;

  //
  // Some countries (e.g. Germany) do not have a "decimal point"
  // in a properly localised OS. In Germany we have a comma instead.
  // A quick-and-dirty fix accepts both a decimal and a comma
  // (a.k.a. separator) here.
  //
  case GDK_KEY_KP_Decimal:
  case GDK_KEY_KP_Separator:
    vfo_num_pad(-5, active_receiver->id);
    break;

  case GDK_KEY_KP_Subtract:
    vfo_num_pad(-6, active_receiver->id);
    break;

  default:
    // not intercepted, so handle downstream
    ret = FALSE;
    break;
  }

  g_idle_add(ext_vfo_update, NULL);
  return ret;
}

// cppcheck-suppress constParameterCallback
static gboolean main_delete (GtkWidget *widget) {
  if (radio != NULL) {
    stop_program();
  }

  _exit(0);
}

static int init(void *data) {
  char wisdom_directory[1025];
  char text[1024];
  t_print("%s\n", __FUNCTION__);
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
  t_print("Securing wisdom file in directory: %s\n", wisdom_directory);
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
  char text[256];
  t_print("Build: %s (Commit: %s, Date: %s)\n", build_version, build_commit, build_date);
  t_print("GTK+ version %u.%u.%u\n", gtk_major_version, gtk_minor_version, gtk_micro_version);
  uname(&unameData);
  t_print("sysname: %s\n", unameData.sysname);
  t_print("nodename: %s\n", unameData.nodename);
  t_print("release: %s\n", unameData.release);
  t_print("version: %s\n", unameData.version);
  t_print("machine: %s\n", unameData.machine);
  load_css();
  //
  // Start with default font. The selected
  // becomes active if the radio is started
  //
  load_font(0);
  GdkDisplay *display = gdk_display_get_default();

  if (display == NULL) {
    t_print("no default display!\n");
    _exit(0);
  }

  screen = gdk_display_get_default_screen(display);

  if (screen == NULL) {
    t_print("no default screen!\n");
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
  t_print("Monitor Number within Screen=%d\n", this_monitor);
  //
  // Determine the size of "our" monitor
  //
  GdkRectangle rect;
  gdk_screen_get_monitor_geometry(screen, this_monitor, &rect);
  screen_width = rect.width;
  screen_height = rect.height;
  t_print("Monitor: width=%d height=%d\n", screen_width, screen_height);
  // Start with 800x480, since this width is required for the "discovery" screen.
  // Go to "full screen" mode if display nearly matches 800x480
  // This is all overridden later for the radio from the props file
  display_width  = 800;
  display_height = 480;
  full_screen    = 0;

  //
  // Go to full-screen mode by default, if the screen size is approx. 800*480
  //
  if (screen_width > 780 && screen_width < 820 && screen_height > 460 && screen_height < 500) {
    full_screen = 1;
    display_width = screen_width;
    display_height = screen_height;
  }

  t_print("display_width=%d display_height=%d\n", display_width, display_height);

  if (full_screen) {
    t_print("full screen\n");
    gtk_window_fullscreen_on_monitor(GTK_WINDOW(top_window), screen, this_monitor);
  }

  g_signal_connect (top_window, "delete-event", G_CALLBACK (main_delete), NULL);
  //
  // We want to use the space-bar as an alternative to go to TX
  //
  gtk_widget_add_events(top_window, GDK_KEY_PRESS_MASK);
  g_signal_connect(top_window, "key_press_event", G_CALLBACK(keypress_cb), NULL);
  topgrid = gtk_grid_new();
  gtk_widget_set_size_request(topgrid, display_width, display_height);
  gtk_grid_set_row_homogeneous(GTK_GRID(topgrid), FALSE);
  gtk_grid_set_column_homogeneous(GTK_GRID(topgrid), FALSE);
  gtk_grid_set_column_spacing (GTK_GRID(topgrid), 10);
  gtk_container_add (GTK_CONTAINER (top_window), topgrid);
  //
  // Closely following Heiko's suggestion, we now have the HPSDR log contained
  // in the code and need not fiddle around with the question from where to load it.
  //
  GtkWidget *image = hpsdr_logo();

  if (image) {
    gtk_grid_attach(GTK_GRID(topgrid), image, 0, 0, 1, 2);
  }

  GtkWidget *pi_label = gtk_label_new("piHPSDR by John Melton G0ORX/N6LYT");
  gtk_widget_set_name(pi_label, "big_txt");
  gtk_widget_set_halign(pi_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(topgrid), pi_label, 1, 0, 3, 1);
  snprintf(text, sizeof(text), "Built %s, Version %s\nOptions: %s\nAudio module: %s",
           build_date, build_version, build_options, build_audio);
  GtkWidget *build_date_label = gtk_label_new(text);
  gtk_widget_set_name(build_date_label, "med_txt");
  gtk_widget_set_halign(build_date_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(topgrid), build_date_label, 1, 1, 3, 1);
  status_label = gtk_label_new(NULL);
  gtk_widget_set_name(status_label, "med_txt");
  gtk_widget_set_halign(status_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(topgrid), status_label, 1, 2, 3, 1);
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
    fprintf(stderr, "piHPSDR version and commit: %s, %s; built %s\n", build_version, build_commit, build_date);
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
  t_print("Base priority on startup: %d\n", rc);
  setpriority(PRIO_PROCESS, 0, -10);
  rc = getpriority(PRIO_PROCESS, 0);
  t_print("Base priority after adjustment: %d\n", rc);
  startup(argv[0]);
  snprintf(name, sizeof(name), "org.g0orx.pihpsdr.pid%d", getpid());
  //t_print("gtk_application_new: %s\n",name);
  pihpsdr = gtk_application_new(name, G_APPLICATION_FLAGS_NONE);
  g_signal_connect(pihpsdr, "activate", G_CALLBACK(activate_pihpsdr), NULL);
  rc = g_application_run(G_APPLICATION(pihpsdr), argc, argv);
  t_print("exiting ...\n");
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
