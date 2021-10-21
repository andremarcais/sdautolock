#include <unistd.h>
#include <stdlib.h>
#include <error.h>
#include <systemd/sd-bus.h>

#define eprintf(FMT,...) fprintf(stderr,FMT __VA_OPT__(,) __VA_ARGS__)

static int lock = -1;
static sd_bus *bus;

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
    aquire_sleep_lock();
  }
  return 0;
}

int main(int argc, char *argv[]) {
  int r = 0;

  if(argc < 2) {
    char* name = "myautolock";
    if(argc == 1) name = argv[0];
    eprintf("Usage: %s <LOCKER> [ARGS...]\n", name);
    return 1;
  }

  if((r = sd_bus_default_system(&bus)) < 0)
    error(1, -r, "Failed to open system bus");

  aquire_sleep_lock();

  r = sd_bus_match_signal(bus, NULL,
                          "org.freedesktop.login1",
                          "/org/freedesktop/login1",
                          "org.freedesktop.login1.Manager",
                          "PrepareForSleep",
                          prepare_sleep, argv+1);

  while(r >= 0) {
    r = sd_bus_process(bus, NULL);
    if(r == 0) sd_bus_wait(bus, UINT64_MAX);
  }

  sd_bus_unref(bus);
  return 0;
}
