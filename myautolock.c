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
static int locker = 0;
static sd_bus *bus;
static xcb_connection_t *disp;
static sd_event *loop;

// prints error freeing `rep` and `err` returning an exit code
static int dbus_print_error(sd_bus_message *rep, sd_bus_error *err, int code, const char* msg) {
  if(sd_bus_error_is_set(err)) {
    eprintf("%s: %s: %s\n", msg, err->name, err->message);
    sd_bus_error_free(err);
    return 1;
  } else {
    eprintf("%s: %s\n", msg, strerror(code));
    return 1;
  }
}

// if none acquired already, globally acquire a sleep lock. this is idempotent
static int acquire_sleep_lock() {
  sd_bus_error err = SD_BUS_ERROR_NULL;
  sd_bus_message *rep;
  int fd, r;
  if(lock != -1) return 1; // already has lock
  r = sd_bus_call_method( bus, "org.freedesktop.login1",
                          "/org/freedesktop/login1",
                          "org.freedesktop.login1.Manager",
                          "Inhibit", &err, &rep, "ssss",
                          "sleep", "myautolock", "locking", "delay");
  if( r < 0 )
    return dbus_print_error(rep, &err, -r, "Failed to acquire sleep inhibitor");
  sd_bus_error_free(&err);
  sd_bus_message_read(rep, "h", &fd);
  lock = dup(fd);
  sd_bus_message_unref(rep);
  // don't close lock fd on exec
  fcntl(lock, F_SETFD, ~FD_CLOEXEC & fcntl(lock, F_GETFD));
  return 0;
}

// globally release lock if locked
static void release_sleep_lock() {
  if(lock != -1) {
    close(lock);
    lock = -1;
  }
}

static int handle_locker_exit(sd_event_source *s, const siginfo_t *si, void *userdata) {
  int *pid = (pid_t*) userdata;
  *pid = 0;
  return 0;
}

// run screen locker if not running. this is idempotent
static void lock_screen(char** argv) {
  static pid_t pid = 0;
  char *lock_str;
  size_t lock_len;
  if(locker) return;
  switch((locker = fork())) {
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
    sd_event_add_child(loop, NULL, pid, WEXITED, handle_locker_exit, &pid);
    return;
  }
}

// handles PrepareForSleep signal
static int handle_prepare_sleep(sd_bus_message *m, void *data, sd_bus_error *err) {
  char **argv = (char**)data;
  int enter;
  sd_bus_message_read(m, "b", &enter);
  if(enter) {
    lock_screen(argv);
    release_sleep_lock();
  } else {
    acquire_sleep_lock();
  }
  return 0;
}

// handles Lock signal
static int handle_lock_session(sd_bus_message *m, void *data, sd_bus_error *err) {
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

struct matcher {
  char* session;
  char** locker;
  sd_bus_slot *sleep;
  sd_bus_slot *lock;
};

static void init_matcher(struct matcher *m, const struct opts o) {
  int r;
  sd_bus_error err = SD_BUS_ERROR_NULL;
  sd_bus_message *rep;
  char* path;

  r = sd_bus_call_method(bus, "org.freedesktop.login1",
                         "/org/freedesktop/login1",
                         "org.freedesktop.login1.Manager",
                         "GetSession", &err, &rep, "s", "");
  if( r < 0 )
    exit( dbus_print_error(rep, &err, -r, "Failed to get current session") );
  sd_bus_error_free(&err);

  sd_bus_message_read(rep, "o", &path);
  m->session = malloc(strlen(path)+1);
  strcpy(m->session, path);
  sd_bus_message_unref(rep);

  m->locker = o.locker;
}

static void activate_matches(struct matcher *m) {
  int r;

  r = sd_bus_match_signal(bus, NULL,
                          "org.freedesktop.login1",
                          "/org/freedesktop/login1",
                          "org.freedesktop.login1.Manager",
                          "PrepareForSleep", handle_prepare_sleep, m->locker);
  if(r < 0)
    error(1, -r, "Failed to match PrepareForSleep signal");

  r = sd_bus_match_signal(bus, NULL,
                          "org.freedesktop.login1",
                          m->session,
                          "org.freedesktop.login1.Session",
                          "Lock", handle_lock_session, m->locker);
  if(r < 0)
    error(1, -r, "Failed to match lock Lock signal");
}

static void deactivate_matches(struct matcher *m) {
  sd_bus_slot_unref(m->lock);
  sd_bus_slot_unref(m->sleep);
}

int main(int argc, char *argv[]) {
  int r = 0;
  xcb_window_t root;
  time_t rem = 1000;
  struct matcher m;
  const struct opts o = parse_args(argc, argv);

  if((r = sd_bus_default_system(&bus)) < 0)
    error(1, -r, "Failed to open system bus");

  root = connect_and_get_root();

  if(!has_screensaver()) {
    fputs("Screensaver not suported by Xorg server.",stderr);
    return 1;
  }

  acquire_sleep_lock();

  init_matcher(&m, o);
  activate_matches(&m);

  r = sd_event_default(&loop);
  if(r < 0) eprintf("Failed to allocate event loop: %s\n", strerror(-r));

  sd_bus_attach_event(bus, loop, 0);

  r = sd_event_loop(loop);
  if(r < 0) eprintf("Failed to start event loop: %s\n", strerror(-r));

  sd_event_unref(loop);
  xcb_disconnect(disp);
  sd_bus_unref(bus);
  return r;
}
