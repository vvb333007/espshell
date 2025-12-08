/* 
 * This file is a part of the ESPShell Arduino library (Espressif's ESP32-family CPUs)
 *
 * Latest source code can be found at Github: https://github.com/vvb333007/espshell/
 * Stable releases: https://github.com/vvb333007/espshell/tags
 *
 * Feel free to use this code as you wish: it is absolutely free for commercial and 
 * non-commercial, education purposes.  Credits, however, would be greatly appreciated.
 *
 * Author: Viacheslav Logunov <vvb333007@gmail.com>
 */

// -- Time & date --
// System time and date can be set either manually (via "time set") or automatically via NTP (over WiFi)
// System time is maintained by RTC module of ESP32 SoC and provides correct time updates in a deep sleep and after reset.
// Powering off ESP32 will reset its RTC module
//
// Even though RTC module of ESP32 keeps track of time during resets or deep sleep it knows nothing about time zones.
// ESPShell saves timezone on time zone change and loads it on sketch startup (see "time zone XXX" command).
//

#if COMPILING_ESPSHELL

#include <sys/time.h>
#include <time.h>

// Time settings and parameters.
//
static struct {

  uint32_t local_set:1;   // Local time was set and is likely to be valid
  uint32_t reserved:31;
  const char *src;        // human readable time source (who set the clock)
  char zone[12];          // time zone in format ready for setenv()/tzset()
  uint64_t last_sync;     // useconds since last local time update (manual , NTP or external RTC chip)
} Time = { 0 };



// Convert microseconds to "XXX days XX hours" or "XX hours XX minutes" and so on: approximate value
// in easy human readable form. Values less than 60 seconds are shown as "<1 minute"
//
// This function is used when we want to show "Last updated: XXX ago" or similar
// NOTE: buf_len must be not less than 20 symbols : "24 hours 59 minutes", 32 symbols is the safe choice
//
static char *q_timelen(uint64_t usec, char *buf, size_t buf_len) {

  uint32_t seconds = usec / 1000000ULL, // TODO: NOTE: will not work for time intervals greater than 136 years
                     x,y;
  const char *timespec[]  = {"day",    "hour",  "minute", "second"};
  uint32_t dividers[]     = { 24*60*60,  60*60,    60,    1 };

  if (!buf || buf_len < 32)
    return NULL;

  for (int i = 0; i < 3; i++) {

    if (seconds >= dividers[i]) {
      x = seconds / dividers[i];
      y = (seconds % dividers[i]) / dividers[i + 1];
      if (!y)
        snprintf(buf, buf_len, "%lu %s%s", x, timespec[i], x == 1 ? "" : "s");
      else
        snprintf(buf, buf_len, "%lu %s%s, %lu %s%s", x, timespec[i], x == 1 ? "" : "s",
                                                       y, timespec[i + 1], y == 1 ? "" : "s");
      return buf;
    }

  }

  strlcpy(buf, "<1 minute", buf_len);
  return buf;
}


// Callback function, that tells ESPShell that time has been changed. Called by "time set".
// NTP time sync also calls time_has_been_updated() from its "time received" callback
//
// Updates "Last sync" and "Time source" variables
//
static void time_has_been_updated(const char *new_source) {
    Time.local_set = true;
    Time.src = new_source;
    Time.last_sync = q_micros();
    HELP(q_printf("\r\n%% New system time/date has been set. (%s)\r\n", new_source ? new_source : "unspecified source"));
}

// Return a month number by its name. "january" = 1.
// Returns <1 if /name/ is not a valid name of the month
//
static int8_t time_month_by_name(const char *name) {

  int8_t month = -1,   // month number, 1..12.
         len = 1,      // len is "min(3, strlen(name))"
         c;            // first symbol of the name (i.e. name[0])

  if (likely(name && name[0])) {
    c = name[0];
    // lowercase name's first character
    if (c >= 'A' && c <= 'Z')
        c = c | (1 << 5);

    if (name[1]) {
      len++;
      if (name[2])
        len++;
    }

    switch (name[0]) {
      case 'd': month = 12; break;
      case 'f': month = 2;  break;
      case 'n': month = 11; break;
      case 'o': month = 10; break;
      case 's': month = 9;  break;
      case 'a': month = len > 1 ? (name[1] == 'p' ? 4 : 8) : -1; break; //april-august.
      case 'j': month = (name[1] == 'a') ? 1 : (len < 3 ? -1 : (name[2] == 'n' ? 6 : 7)); break;  // january-june-july
      case 'm': month = (len > 2) ? (name[2] == 'r' ? 3 : 5) : -1; break; // march-may
      default:  break;
    }
  }
  return month;
}

static void time_apply_zone() {
  setenv("TZ", Time.zone, 1);
  tzset();
}

// Time zone relative to UTC. E.g. Bangkok is UTC+7, so command will be "time zone 7" or "time zone +7"
// "time zone 1"
// "time zone -1 hour 45 minutes"
// "time zone 45 minutes"
// "time zone none"
//

static int cmd_time_zone(int argc, char **argv) {

  int64_t val;
  

  if (argc < 3)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[2],"none"))
    val = 0;
  else {
    val = userinput_read_timespec(argc, argv, 2, NULL) / 1000000ULL; // convert to seconds
    if (val < 60) {
      //timespec without a time specifier defaults to seconds but we assume hours
      // i.e. "time zone 1" will read as 1 hour
      val = val * 3600ULL;
    }
  }

  if (val > 12*60*60 || val < -12*60*60) {
    HELP(q_print("% Time zone value is out of range (>12 hours), time zone not set\r\n"));
    return CMD_FAILED;
  }



  snprintf(Time.zone,sizeof(Time.zone),"UTC%c%02d:%02d",
                            val < 0 ? '+' : '-',
                            (int )val / 3600,
                            ((int )val % 3600) / 60);
  
  time_apply_zone();
  q_printf("%% Set TZ=\"%s\", local time has been adjusted\r\n",Time.zone);
  // TimeZone must be saved to NVS: RTC keeps track of the time but knows nothing about timezones
  // so after reload "show time" will display UTC time instead of local time

  nv_save_config();

  return 0;
}

// "time set ..."
//
static int cmd_time_set(int argc, char **argv) {

  int stop = -1;
  struct timeval tv;

  if (argc < 3)
    return CMD_MISSING_ARG;
  
  tv.tv_sec = userinput_read_datime(argc,argv,2,&stop);
  tv.tv_usec = 0;

  if (tv.tv_sec) {
    settimeofday(&tv, NULL);
    time_has_been_updated("user input");
  } else {
    HELP(q_print("% System time is unchanged\r\n"));
    // report failed argument (if we have one)
    return stop > 0 ? stop : CMD_FAILED;
  }

  return 0;
}


// All "time" commands are routed here: unfortunately shell is quite simple and only differentiate
// by the first keyword ("the command").
//
static int cmd_time(int argc, char **argv) {

  if (argc < 2)
    return cmd_show_time(argc, argv);

  if (!q_strcmp(argv[1],"set"))
    return cmd_time_set(argc,argv);

  if (!q_strcmp(argv[1],"zone"))
    return cmd_time_zone(argc,argv);

  if (!q_strcmp(argv[1],"flies")) {
    q_print("% Agree :(\r\n");
    return 0;
  }

  // Unrecognized argument #1
  return 1;
}


// "show time"
//
static int cmd_show_time(int argc, char **argv) {

  char buf[128];    // buffer for strftime. enough space for multibyte locales
  //char buftz[8];
  struct tm tm_adj; // adjusted (according to a timezone) time
  time_t now;

  now = time(NULL);
  if (!now)
    return CMD_FAILED;
  //gmtime_r(&now, &tm_adj);
  localtime_r(&now, &tm_adj);
    
  if (strftime(buf, sizeof(buf), "%e of %B (%A) <i>%H:%M</>:%S ,year %Y", &tm_adj) == 0)
    return CMD_FAILED;
  

  q_printf("%% Today is: %s (<i>%s</>)\r\n", buf, Time.zone);
  q_printf("%% Time source is %s", Time.src ? Time.src : "on-chip RTC (volatile)");

  if (Time.local_set)
    q_printf(", last updated: %s ago\r\n", q_timelen(q_micros() - Time.last_sync, buf, sizeof(buf))  ); //reuse buf
  else if (tm_adj.tm_year < 125) // year < 2025?
    q_printf(", time and/or date may be incorrect\r\n");
  else
    q_print(CRLF);

  return 0;
}

#endif // #if COMPILING_ESPSHELL
