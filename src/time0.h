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
  uint32_t src_manual:1;  //
  uint32_t src_ntp:1;     // NTP configured
  uint32_t src_rtc:1;     // RTC configured
  uint32_t rtc_rw:1;      // Allow updates to RTC
  uint32_t format_12:1;   // 12 or 24?
  uint32_t local_set:1;   // Local time was set and is likely to be valid
  uint32_t rtc_nosync:1;   // "no-sync" keyword was used

  const char *server;
  const uint8_t rtc_addr;
  const uint8_t rtc_bus;
  const uint8_t rtc_base;

  signed int zone;    // timezone offset in seconds
  time_t last_sync;   // seconds since last local time update
} Time = { 0 };


static struct {
    const char *name;
    uint8_t i2c_addr;   // I2C device address
    uint8_t base_reg;   // First register where time starts. Assumed order: Sec, Min, Hour, Day, Weekday, Month, Year
//    uint8_t cent_reg;   // Century register, or 0xff if not present
//    uint8_t cent_mask;  // Century bit position
} RtcParams[] = {
  
  { "ds1307"  ,0x68, 0x00 }, 
  { "ds3231"  ,0x68, 0x00 },
  { "pcf8563" ,0x51, 0x02 }, // note: bit7 of seconds = vl flag
  { "pcf8523" ,0x68, 0x03 },
  { "mcp7940n",0x6f, 0x00 }, // note: bit7 of seconds = st (osc enable), must be handled
  { "bm8563"  ,0x51, 0x02 },
  { "rx8025"  ,0x32, 0x00 },
  { "rx8900"  ,0x32, 0x00 },
  { 0 }
};


static int8_t time_month_by_name(const char *name) {

  int8_t month = -1,   // month number, 1..12.
         len = 1;    

  if (name && name[0]) {

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


static time_t time_local() {

  struct tm tm_adj; // adjusted (according to a timezone) time
  time_t    now;    // raw seconds since UNIX epoch.
  
  if ((now = time(NULL)) < (time_t)(tz * 60))
    return now;

  return now + Time.zone;
}

static bool time_zone2ascii(char *buf, int size) {
  
  if (likely(buf != NULL && size > 5) {

    char sign = '+';
    signed int tz_abs = Time.zone;

    if (tz_abs < 0) {
      tz_abs = -tz_abs;
      sign = '-';
    }
    // e.g. "+0700"
    snprintf(buf,size,"%c%02d%02d",
            sign
            tz_abs / 3600,
           (tz_abs % 3600)/60);
    return true;
  }
  return false;
}

// "12:3" "01:02:33" "1:1:1"
// TODO: move to userinput.h
//
static bool read_hms(const char *p, int8_t *h, int8_t *m, int8_t *s) {

  int8_t hms[3] = { 0 }, i = 0;

  while (*p) {
    if (*p == ':') {
      if (++i > 2)
        return false;
    } else if (*p >= '0' && *p <= '9')
      hms[i] = hms[i] * 10 + (*p - '0');
    else
      return false;
    ++p;
  }

  if (h) *h = &hms[0];
  if (m) *m = &hms[1];
  if (s) *s = &hms[2];

  return true;
}



#define XX(_Local, _Ntp, _Rtc) do { \
    Time.src_internal = _Local; \
    Time.src_ntp = _Ntp; \
    Time.src_rtc = _Rtc; \
} while( 0 );


// "time source ntp|rtc|internal ... "
//
static int cmd_time_source(int argc, char **argv) {

  if (argc < 3)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[2],"ntp"))
    return cmd_time_source_ntp(argc,argv);
  else if (!q_strcmp(argv[2],"rtc"))
    return cmd_time_source_rtc(argc,argv);
  else if (!q_strcmp(argv[2],"internal"))
    XX(1,0,0);
  else {
    q_print("%% Supported time sources are \"rtc\", \"ntp\" and \"internal\"\r\n");
    return 2;
  }
  return 0;
}



// "time format 12|24"
//
static int cmd_time_format(int argc, char **argv) {

  if (argc < 3)
    return CMD_MISSING_ARG;
  
  if (argv[2][0] == '1')
    Time.format_12 = 1;
  else if (argv[2][0] == '2')
    Time.format_12 = 0;
  else
    return 2;

  return 0;
}

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
  else
    val = read_timespec(argc, argv, 2, NULL) / 1000000ULL; // convert to seconds

  if (val > 12*60*60 || val < -12*60*60) {
    HELP(q_print("% Time zone value is out of range (>12 hours), time zone not set\r\n"));
    return CMD_FAILED;
  }
  Time.zone = val;

  return 0;
}

// "time set ..."
//
static int cmd_time_set(int argc, char **argv) {

  struct timeval tv;
  
  tv.tv_sec = read_datime(argc,argv,2,NULL);
  tv.tv_usec = 0;

  if (tv.tv_sec)
    settimeofday(&tv, NULL);
  else {
    HELP(q_print("% Time was not set\r\n"));
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

  if (!q_strcmp(argc[1],"set"))
    return cmd_time_set(argc,argv);

  if (!q_strcmp(argc[1],"format"))
    return cmd_time_format(argc,argv);

  if (!q_strcmp(argc[1],"source"))
    return cmd_time_source(argc,argv);

  if (!q_strcmp(argc[1],"zone"))
    return cmd_time_zone(argc,argv);

  if (!q_strcmp(argc[1],"flies")) {
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
  char buftz[8];
  struct tm tm_adj; // adjusted (according to a timezone) time
  time_t now;

  now = time_local();
  if (!now)
    return CMD_FAILED;
  gmtime_r(&now, &tm_adj);
    
  if (strftime(buf, sizeof(buf), "%e of %B (%A) %H:%M:%S ,year %Y", &tm_utc) == 0)
    return CMD_FAILED;
  
  time_zone2ascii(buftz,sizeof(buftz));
  q_printf("%% Today is: %s (Timezone is <i>%s UTC</>)\r\n", buf, buftz);
  if (!Time.local_set)
    q_print("% <i>Time was NOT set, date/time above is approximate</>\r\n");
  if (Time.src_manual)
    q_print("%% Time is maintained by CPU\r\n");
  if (Time.server)
    q_printf("%% NTP server: <i>%s</>\r\n", Time.server);
  if (Time.src_ntp)
    q_printf("%% Time source is NTP, last sync <i>%u</> seconds ago\r\n", Time.last_sync);
  else if (Time.src_rtc) {
    q_printf("%% Time source is RTC, (addr=%x, bus=i2c%d, base_reg=%u)\r\n", Time.rtc_addr, Time.rtc_bus, Time.rtc_base);
    if (Time.rtc_nosync)
      q_print("% <i>Local time was NOT updated from the RTC</>: no-sync flag was used\r\n");
    if (Time.rtc_rw == 0)
      q_print("% <i>Updating the local time WILL NOT update RTC</>: read-only flag was used\r\n");
  }

  return 0;
}


#undef XX

#endif // #if COMPILING_ESPSHELL
