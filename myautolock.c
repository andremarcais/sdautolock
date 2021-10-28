#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <error.h>

#include <systemd/sd-bus.h>

#define eprintf(FMT,...) fprintf(stderr,FMT __VA_OPT__(,) __VA_ARGS__)

static int lock = -1; // sleep inhibitor lock
static sd_bus *bus;
static sd_event *loop;

// command line options
static struct {
  time_t time;
  char* idle;
  char** locker;
} o;

static int handle_time_up(sd_event_source *s, uint64_t usec, void *userdata);
static int handle_locker_exit(sd_event_source *s, const siginfo_t *si, void *userdata);
static int handle_lock_session(sd_bus_message *m, void *data, sd_bus_error *err);
static int handle_prepare_sleep(sd_bus_message *m, void *data, sd_bus_error *err);

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

// run screen locker if not running. this is idempotent
static void lock_screen(char** argv) {
  static pid_t pid = 0;
  char *lock_str;
  size_t lock_len;
  int r;
  if(pid) return;
  switch((pid = fork())) {
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
    r = sd_event_add_child(loop, NULL, pid, WEXITED, handle_locker_exit, &pid);
    if(r < 0) eprintf("Failed to add child %d event: %s\n", pid, strerror(-r));
    return;
  }
}

static int parse_args(int argc, char* argv[]) {
  static const char* usage = "Usage: %s IDLE TIME LOCKER [ARGS...]\n";
  char* name = "myautolock";

  if(argc > 0) name = argv[0];

  if(argc < 4) {
    eprintf(usage, name);
    return 1;
  }

  o.locker = argv+3;
  o.idle = argv[1];

  if((o.time = atoi(argv[2])) == 0) {
    eprintf(usage, name);
    fputs("TIME must be a non-zero integer.\n",stderr);
    return 1;
  }

  return 0;
}

struct matcher {
  char* session;
  char** locker;
  sd_bus_slot *sleep;
  sd_bus_slot *lock;
};

static void init_matcher(struct matcher *m) {
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

static unsigned remaining_idle_time() {
  FILE *input;
  time_t time;
  int pid, fds[2];
  if(pipe(fds) == -1) {
    perror("failed to open idle time utility pipe");
    return o.time;
  }
  switch((pid = fork())) {
  case -1:
    perror("Failed fork idle time utility\n");
    return o.time;
  case 0:
    close(fds[0]);
    close(lock);
    dup2(fds[1], 1);
    execlp(o.idle, o.idle, NULL);
    perror("failed to exec idle utility");
    exit(1);
  default:
    close(fds[1]);
    input = fdopen(fds[0], "r");
    fscanf(input, "%ld", &time);
    waitpid(pid, NULL, 0);
    time /= 1000;
    if(o.time > time) return o.time - time;
    else return 0;
  }
}

static void set_timer(time_t time) {
  int r;
  r = sd_event_add_time_relative(loop, NULL, CLOCK_MONOTONIC, time*1e6, 0, handle_time_up, NULL);
  if(r < 0) eprintf("Failed to set timer: %s\n", strerror(-r));
}

// lock screen when time elapses
static int handle_time_up(sd_event_source *s, uint64_t usec, void *userdata) {
  time_t rem;
  if((rem = remaining_idle_time())) {
    set_timer(rem);
  } else {
    lock_screen(o.locker);
  }
  return 0;
}

static int handle_locker_exit(sd_event_source *s, const siginfo_t *si, void *userdata) {
  int *pid = (pid_t*) userdata;
  *pid = 0;
  set_timer(o.time);
  return 0;
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

int main(int argc, char *argv[]) {
  int r = 0;
  sigset_t sigs;
  struct matcher m;

  if((r = parse_args(argc, argv))) return r;

  if((r = sd_bus_default_system(&bus)) < 0)
    error(1, -r, "Failed to open system bus");

  acquire_sleep_lock();

  init_matcher(&m);
  activate_matches(&m);

  sigemptyset(&sigs);
  sigaddset(&sigs, SIGCHLD);
  sigprocmask(SIG_BLOCK, &sigs, NULL);

  r = sd_event_default(&loop);
  if(r < 0) eprintf("Failed to allocate event loop: %s\n", strerror(-r));

  set_timer(o.time);
  sd_bus_attach_event(bus, loop, 0);

  r = sd_event_loop(loop);
  if(r < 0) eprintf("Failed to start event loop: %s\n", strerror(-r));

  sd_event_unref(loop);
  sd_bus_unref(bus);
  return r;
}
