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

// UNDER DEVELOPMENT, DOES NOT COMPILE!

#if COMPILING_ESPSHELL

#include <sys/time.h>
#include <time.h>

static struct {

  uint32_t local_set:1;   // Local time was set and is likely to be valid
  uint32_t reserved:31;
  const char *src;        // human readable time source (who set the clock)
  char zone[16];
  time_t last_sync;       // seconds since last local time update
} Time = { 0 };

static int8_t time_month_by_name(const char *name) {

  int8_t month = -1,   // month number, 1..12.
         len = 1;      // number of characters present in /name/ ("not less than", not strlen(name))

  if (likely(name && name[0])) {

    if (name[1]) {
      len++;
      if (name[2])
        len++;
    }

    switch (name[0]) {
      
      case 'a': month = len > 1 ? (name[1] == 'p' ? 4   //april
                                                  : 8)  //august
                                : -1;                        
                break;
      case 'd': month = 12;
                break;
      case 'f': month = 2;
                break;
      case 'j': month = (name[1] == 'a') ? 1                                     // january 
                                         : (len < 3 ? -1
                                                    : (name[2] == 'n' ? 6 : 7)); // june/july
                break;  
      case 'm': month = (len > 2) ? (name[2] == 'r' ? 3   // march
                                                    : 5)  // may
                                  : -1;
                break;
      case 'n': month = 11;
                break;
      case 'o': month = 10;
                break;
      case 's': month = 9;
                break;
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
    val = read_timespec(argc, argv, 2, NULL) / 1000000ULL; // convert to seconds
    if (val < 60) {
      //timespec without a time specifier defaults to seconds but we assume hours
      // i.e. "time zone 1" will read as 1 second
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
  // TODO: refactor config saving
  nv_save_config(NULL);

  return 0;
}

// "time set ..."
//
static int cmd_time_set(int argc, char **argv) {

  struct timeval tv;

  if (argc < 3)
    return CMD_MISSING_ARG;
  
  tv.tv_sec = read_datime(argc,argv,2,NULL);
  tv.tv_usec = 0;

  if (tv.tv_sec)
    settimeofday(&tv, NULL);
  else {
    HELP(q_print("% System time is unchanged\r\n"));
    return CMD_FAILED;
  }

  return 0;
}


// All "time" commands are routed here: unfortunately shell is quite simple and only differentiate
// by the first keyword ("the command").
//
static int cmd_time(int argc, char **argv) {

  if (argc < 2)
    return CMD_MISSING_ARG;

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
    
  if (strftime(buf, sizeof(buf), "%e of %B (%A) %H:%M:%S ,year %Y", &tm_adj) == 0)
    return CMD_FAILED;
  

  q_printf("%% Today is: %s (<i>%s</>)\r\n", buf, Time.zone);
  if (!Time.local_set)
    q_print("% Not synchronized\r\n");
  if (Time.src)
    q_printf("%% Time source is %s, %ssynchronized\r\n", Time.src, Time.local_set ? "" : "not ");

  return 0;
}

#endif // #if COMPILING_ESPSHELL
