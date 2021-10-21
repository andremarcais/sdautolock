#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <error.h>

#include <systemd/sd-bus.h>

#define eprintf(FMT,...) fprintf(stderr,FMT __VA_OPT__(,) __VA_ARGS__)

static int lock = -1;
static sd_bus *bus;
static xcb_connection_t *disp;

static int timer_up(time_t tu, time_t *ret) {
  static time_t t0 = INT64_MIN, i = 0, te = 0, up;
  if(!i) {
    time(&t0);
    i = 1;
    up = 0;
  } else {
    if((te = time(NULL) - t0) >= tu) {
      time(&t0);
      te = 0;
      up = 1;
    } else {
      up = 0;
    }
  }
  if(ret) *ret = tu - te;
  return up;
}

static void aquire_sleep_lock() {
  sd_bus_error err = SD_BUS_ERROR_NULL;
  sd_bus_message *rep;
  int fd, r;
  if(lock != -1) return; // already has lock
  r = sd_bus_call_method( bus, "org.freedesktop.login1",
                          "/org/freedesktop/login1",
                          "org.freedesktop.login1.Manager",
                          "Inhibit", &err, &rep, "ssss",
                          "sleep", "myautolock", "locking", "delay");
  if( r< 0 ) {
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
}

static void release_sleep_lock() {
  if(lock != -1) {
    close(lock);
    lock = -1;
  }
}

static void lock_screen(char** argv) {
  switch(fork()) {
  case -1:
    perror("Failed run fork screen locker");
    break;
  case 0:
    release_sleep_lock();
    execvp(argv[0], argv);
    perror("Failed to exec locker");
  default:
    return;
  }
}

static int prepare_sleep(sd_bus_message *m, void *data, sd_bus_error *err) {
  char **argv = (char**)data;
  int enter;
  sd_bus_message_read(m, "b", &enter);
  if(enter) {
    lock_screen(argv);
    release_sleep_lock();
  } else {
    timer_up(0, NULL); // reset timer
    aquire_sleep_lock();
  }
  return 0;
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
    puts("TIME must be a non-zero integer.");
    exit(1);
  }

  return o;
}

int main(int argc, char *argv[]) {
  int r = 0;
  time_t remaining;
  const struct opts o = parse_args(argc, argv);

  if((r = sd_bus_default_system(&bus)) < 0)
    error(1, -r, "Failed to open system bus");

  disp = xcb_connect(NULL, NULL);

  aquire_sleep_lock();

  r = sd_bus_match_signal(bus, NULL,
                          "org.freedesktop.login1",
                          "/org/freedesktop/login1",
                          "org.freedesktop.login1.Manager",
                          "PrepareForSleep",
                          prepare_sleep, o.locker);
  if(r < 0)
    error(1, -r, "Failed to match prepare for sleep");

  while(r >= 0) {
    r = sd_bus_process(bus, NULL);
    if(timer_up(o.time, &remaining)) lock_screen(o.locker);
    if(r == 0) r = sd_bus_wait( bus, remaining*1e6 );
  }

  xcb_disconnect(disp);
  sd_bus_unref(bus);
  return 0;
}
