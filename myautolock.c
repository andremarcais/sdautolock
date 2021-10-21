#include <unistd.h>
#include <error.h>
#include <systemd/sd-bus.h>

#define eprintf(FMT,...) fprintf(stderr,FMT __VA_OPT__(,) __VA_ARGS__)

int main(int argc, char *argv[]) {
  sd_bus_error buserr = SD_BUS_ERROR_NULL;
  int r;
  sd_bus *bus;

  if((r = sd_bus_default_system(&bus)) < 0)
    error(1, -r, "Failed to open system bus");

  sd_bus_message *busrep;
  r = sd_bus_call_method( bus, "org.freedesktop.login1",
                          "/org/freedesktop/login1",
                          "org.freedesktop.login1.Manager",
                          "Inhibit", &buserr, &busrep, "ssss",
                          "sleep", "myautolock", "locking", "delay");
  if( r< 0 ) {
    if(sd_bus_error_is_set(&buserr)) {
      eprintf("Failed to inhibit lock: %s: %s\n", buserr.name, buserr.message);
      sd_bus_error_free(&buserr);
      return 1;
    } else {
      error(1, -r, "Failed to inhibit lock");
    }
  }
  sd_bus_error_free(&buserr);

  sleep(1000);

  return 0;
}
