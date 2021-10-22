#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/screensaver.h>

#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <error.h>

#include <systemd/sd-bus.h>

#define eprintf(FMT,...) fprintf(stderr,FMT __VA_OPT__(,) __VA_ARGS__)

static int lock = -1;
static sd_bus *bus;
static xcb_connection_t *disp;

// if none acquired already, globally acquire a sleep lock
static void acquire_sleep_lock() {
  sd_bus_error err = SD_BUS_ERROR_NULL;
  sd_bus_message *rep;
  int fd, r;
  if(lock != -1) return; // already has lock
  r = sd_bus_call_method( bus, "org.freedesktop.login1",
                          "/org/freedesktop/login1",
                          "org.freedesktop.login1.Manager",
                          "Inhibit", &err, &rep, "ssss",
                          "sleep", "myautolock", "locking", "delay");
  if( r < 0 ) {
    if(sd_bus_error_is_set(&err)) {
      eprintf("Failed to inhibit lock: %s: %s\n", err.name, err.message);
      sd_bus_error_free(&err);
      sd_bus_message_unref(rep);
      exit(1);
    } else {
      sd_bus_message_unref(rep);
      error(1, -r, "Failed to inhibit lock");
    }
  }
  sd_bus_error_free(&err);
  sd_bus_message_read(rep, "h", &fd);
  lock = dup(fd);
  sd_bus_message_unref(rep);
  // don't close lock fd on exec
  fcntl(lock, F_SETFD, ~FD_CLOEXEC & fcntl(lock, F_GETFD));
}

// globally release lock if locked
static void release_sleep_lock() {
  if(lock != -1) {
    close(lock);
    lock = -1;
  }
}

// run screen locker, release our sleep lock until child returns
static void lock_screen(char** argv) {
  char *lock_str;
  size_t lock_len;
  switch(fork()) {
  case -1:
    perror("Failed run fork screen locker");
    break;
  case 0:
    // child also recieves sleep lock (or a dummy) and must release it
    if(lock == -1) lock = open("/dev/null", O_RDWR);

    lock_len = snprintf(NULL, 0, "%d", lock) + 1;
    lock_str = malloc(lock_len);
    snprintf(lock_str, lock_len, "%d", lock);
    setenv("MY_SLEEP_LOCK_FD", lock_str, 1);
    free(lock_str);

    execvp(argv[0], argv);
    perror("Failed to exec locker");
    exit(1);
  default:
    release_sleep_lock();
    wait(NULL);
    acquire_sleep_lock();
    return;
  }
}

// handles PrepareForSleep signal
static int prepare_sleep(sd_bus_message *m, void *data, sd_bus_error *err) {
  char **argv = (char**)data;
  int enter;
  sd_bus_message_read(m, "b", &enter);
  if(enter) lock_screen(argv);
  return 0;
}

// handles Lock signal
static int lock_session(sd_bus_message *m, void *data, sd_bus_error *err) {
  char **argv = (char**)data;
  lock_screen(argv);
  return 0;
}

// X root window used to get idle time
static xcb_window_t connect_and_get_root() {
  int scrnum;
  disp = xcb_connect(NULL, &scrnum);
  const xcb_setup_t *setup = xcb_get_setup(disp);
  xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
  for(int i = 0; i < scrnum; ++i) xcb_screen_next(&iter);
  xcb_screen_t *scr = iter.data;
  return scr->root;
}

// query screen saver extension
static int has_screensaver() {
  xcb_query_extension_cookie_t cok = xcb_query_extension(disp, 16, "MIT-SCREEN-SAVER");
  xcb_query_extension_reply_t *rep = xcb_query_extension_reply(disp, cok, NULL);
  int present = rep->present;
  free(rep);
  return present;
}

// amount of idle time left of `tt` seconds before lock should start
static time_t remaining_idle_time(xcb_window_t root, time_t tt) {
  xcb_screensaver_query_info_cookie_t cok = xcb_screensaver_query_info(disp, root);
  xcb_screensaver_query_info_reply_t *rep = xcb_screensaver_query_info_reply(disp, cok, NULL);
  time_t te = rep->ms_since_user_input / 1000;
  free(rep);
  if(te < tt) return tt - te;
  else return 0;
}

struct opts {
  time_t time;
  char** locker;
};

static struct opts parse_args(int argc, char* argv[]) {
  struct opts o;
  static const char* usage = "Usage: %s <TIME> <LOCKER> [ARGS...]\n";
  char* name = "myautolock";

  if(argc > 0) name = argv[0];

  if(argc < 3) {
    eprintf(usage, name);
    exit(1);
  }
  o.locker = argv+2;

  if((o.time = atoi(argv[1])) == 0) {
    eprintf(usage, name);
    fputs("TIME must be a non-zero integer.\n",stderr);
    exit(1);
  }

  return o;
}

static void setup_signal_matches(const struct opts o) {
  int r;

  r = sd_bus_match_signal(bus, NULL,
                          "org.freedesktop.login1",
                          "/org/freedesktop/login1/session/self",
                          "org.freedesktop.login1.Manager",
                          "PrepareForSleep", prepare_sleep, o.locker);
  if(r < 0)
    error(1, -r, "Failed to match PrepareForSleep signal");

  r = sd_bus_match_signal(bus, NULL,
                          "org.freedesktop.login1",
                          NULL, // TODO only current session?
                          "org.freedesktop.login1.Session",
                          "Lock", lock_session, o.locker);
  if(r < 0)
    error(1, -r, "Failed to match lock Lock signal");
}

int main(int argc, char *argv[]) {
  int r = 0;
  xcb_window_t root;
  time_t rem = 1000;
  const struct opts o = parse_args(argc, argv);

  if((r = sd_bus_default_system(&bus)) < 0)
    error(1, -r, "Failed to open system bus");

  root = connect_and_get_root();

  if(!has_screensaver()) {
    fputs("Screensaver not suported by Xorg server.",stderr);
    return 1;
  }

  acquire_sleep_lock();

  setup_signal_matches(o);

  r = 0;
  while(r >= 0) {
    r = sd_bus_process(bus, NULL);
    if((rem = remaining_idle_time(root, o.time)) == 0) lock_screen(o.locker);
    if(r == 0) r = sd_bus_wait( bus, rem*1e6 );
  }

  xcb_disconnect(disp);
  sd_bus_unref(bus);
  return 0;
}
