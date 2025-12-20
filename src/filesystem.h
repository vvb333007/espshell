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

#if COMPILING_ESPSHELL


// WARNING!!!  Hello, fellow software developer! If you’re about to modify this code, we have some bad news:
// WARNING!!!  The code below relies heavily on shared read/write buffers. Many functions WILL modify their
//             input buffers (paths, argv arrays, etc.).
// WARNING!!!  And it gets worse: some of these buffers are static local variables whose pointers are returned
//             to the caller — so handle them with extra care.
// WARNING!!!  If you see a function taking a parameter like `char *`, note that it is deliberately NOT
//             `const char *`.
//
//                                          -- File Manager --
//
// Minimalistic file manager with support for FAT, LittleFS, and SPIFFS. The macros WITH_SPIFFS, WITH_FAT,
// WITH_LITTLEFS, and WITH_FS, WITH_SD determine which parts of the file manager are compiled in.
//
// The interface is designed to feel familiar to Linux shell users: it mimics common commands such as `ls`,
// `cat`, `mkdir`, etc.
//
// We intentionally do NOT use the `chdir()/getcwd()` API to track the current working directory. We avoid
// touching newlib’s CWD because it may interfere with the user’s sketch. For example, if the sketch does
// `chdir("/ffat/preferences")` and then the user runs `cd /` in the shell, we do not want the real CWD to
// be changed — the sketch expects it to remain `/ffat/preferences`. Therefore, we maintain a separate,
// “shadow” CWD that is completely independent from newlib’s own.
//
// Command handlers are named with the `cmd_files_...` prefix; utility and helper functions use the
// `files_...` prefix.
// TODO: fix non-reentrant code. specifically: files_full_path(), may be use __thread variables
//
#if WITH_FS

// Current working directory. Must start and end with "/". We don't use newlib's chdir()/getcwd() because
// it can interfere with the sketch.
//
// CWD is a per-thread local variable, which is q_free()d before task exits (see task_finished())
//
static __thread char *Cwd = NULL;  

// Forwards
static const char *files_set_cwd(const char *cwd);

// espshell allows for simultaneous mounting up to MOUNTPOINTS_NUM partitions.
// mountpoints[] hold information about mounted filesystems.
//
static struct {
  char         *mp;            // mount point e.g. "/a/b/c/d"
  char          label[16 + 1]; // partition label e.g. "ffat", "spiffs" or "littlefs"  TODO: use idf macros or sizeof(something) to get rid of "16+1"
  unsigned char type;          // partition subtype  (e.g. ESP_PARTITION_SUBTYPE_DATA_FAT)
#if WITH_FAT
  wl_handle_t   wl_handle;     // FAT wear-levelling library handle. Only for FAT filesystem on SPI flash (not used for SD cards)
#endif
  void         *gpp;           // general purpose pointer. SD over SPI uses it to store sdmmc_card_t structure
  signed char   gpi;           // index of a SPI bus which must be deinitialized on "unmount"
} mountpoints[MOUNTPOINTS_NUM] = { 0 };


// initialize mountpoints[] array
// NOTE: /Cwd/ MUST NOT be accessed from within this function or whole thing will crash
//
static void __attribute__((constructor)) _files_init() {
  int i;
  for (i = 0; i < MOUNTPOINTS_NUM; i++) {
#if WITH_FAT
    mountpoints[i].wl_handle = WL_INVALID_HANDLE;
#endif
  }
}



// Remove trailing path separators, if any
// Writes to /p/, replacing trailing /// or \\\ with '\0'
//
static void files_strip_trailing_slash(char *p) {
  int i;
  if (p && *p && ((i = strlen(p) - 1) >= 0))
    while ((i >= 0) && (p[i] == '/' || p[i] == '\\'))
      p[i--] = '\0';
}

// is path == "/" ?
static inline bool files_path_is_root(const char *path) {
  return (path && (path[0] == '/' || path[0] == '\\') && (path[1] == '\0'));
}

// Mock GNU getline()
// Reads a full line from a text file — all bytes up to the next '\n' character.
// The '\n' line separator is consumed; '\r' and '\n' are stripped.
//
//  *buf  — Pointer to a char* variable. On the first call, *buf must be NULL.
//          The function will then allocate and manage the line buffer.
//          On subsequent calls, the previously allocated buffer is reused or resized.
//
//          The caller must call q_free(*buf) when the buffer is no longer needed.
//
//  *size — Pointer to a size_t variable holding the current buffer size.
//          It is set and updated by files_getline().
//
//  fp    — File pointer opened in binary read mode.
//
// Returns the number of valid bytes in *buf (not including the terminating '\0'),
// or -1 on error or end-of-file.
static int files_getline(char **buf, unsigned int *size, FILE *fp) {

  int c;
  char *wp, *end;

  // no buffer provided? allocate our own
  if (*buf == NULL)
    if ((*buf = (char *)q_malloc((*size = 128), MEM_GETLINE)) == NULL)
      return -1;

  if (feof(fp))
    return -1;

  wp = *buf;           // buffer write pointer
  end = *buf + *size;  // buffer end pointer

  while (true) {

    c = fgetc(fp);

    // end of line or end of file? return what was read so far
    if ((c < 0) || (c == '\n')) {
      *wp = '\0';
      return (wp - *buf);
    }

    //skip Microsoft-style line endings and save byte to the buffer

    if (c == '\r')
      continue;
    *wp++ = c;

    // do we still have space to store 1 character + '\0'?
    // if not - increase buffer size two times
    if (wp + sizeof(char) + sizeof(char) >= end) {

      char *tmp;
      int written_so_far = wp - *buf;

      if ((tmp = (char *)q_realloc(*buf, (*size *= 2), MEM_GETLINE)) == NULL)
        return -1;

      // update pointers
      *buf = tmp;
      end = tmp + *size;
      wp = tmp + written_so_far;
    }
  }
}

// Convert time_t to char * "31-01-2024 10:40:07". It is used by command "ls" when displaying
// directory listings
// 
// TODO: make it reentrant: add second arg pointer to the buffer
//
static char *files_time2text(time_t t) {
  static char buf[32]; // WARNING: not reentrant!
  struct tm *info;
  info = localtime(&t);

  // TODO: review all sprintf, strcat and strcpy's for buffer overruns
  // TODO: may be implement q_  safe versions of these
  sprintf(buf, "%u-%02u-%02u %02u:%02u:%02u", info->tm_year + 1900, info->tm_mon + 1, info->tm_mday, info->tm_hour, info->tm_min, info->tm_sec);
  //strftime(buf, sizeof(buf), "%b %d %Y", info);
  //strftime(buf, sizeof(buf), "%b %d %H:%M", info);
  return buf;
}

// Set current working directory (CWD). NOTE: changes user prompt, if called from the main task.
// /cwd/ - absolute path (starts with "/")
//
static const char *files_set_cwd(const char *cwd) {

  int len;
  static char prom[MAX_PATH + MAX_PROMPT_LEN] = { 0 };  // TODO: make it dynamic, it is too big

  // TODO: allocate Cwd buffer once. Set its size to MAX_PATH+16
  if (Cwd != cwd) {
    if (Cwd) {
      q_free(Cwd);
      Cwd = NULL;
    }
    if (cwd)
      if ((len = strlen(cwd)) > 0)
        if ((Cwd = (char *)q_malloc(len + 2, MEM_PATH)) != NULL) {
          strcpy(Cwd, cwd);
          len--;
          // append "/" if not there
          if (Cwd[len] != '/' && cwd[len] != '\\')
            strcat(Cwd, "/"); // TODO: Cwd[len] = '/' maybe?
        }
  }

  // Regenerate prompt
  // No we can't use "<i>" and "</>" here to colorize path in the prompt: this prompt is displayed by TTYshow()
  //
  const char *ret = files_get_cwd();
  snprintf(prom, sizeof(prom), PROMPT_FILES,
                (Color ? tag2ansi('i') : ""),
                ret,
                (Color ? tag2ansi('n') : ""));

  prompt_set(prom);
  

  return ret;
}

// return current working directory or "/" if there were memory allocation errors
// pointer returned is always valid.
static inline const char *files_get_cwd() {
  return Cwd ? Cwd : "/";
}

// Convert "*" to spaces in paths. Spaces in paths are entered as asterisk
// "path" must be writeable memory.
// "Program*Files*(x64)" gets converted to "Program Files (x64)"
//
// This is an old code. Now, filenames with spaces can be enclosed in double quotes
// but I keep this code here for compatibility
//
// TODO: remove support for asteriks from the code before 1.0.0 release
//
static void files_asterisk2spaces(char *path) {
  if (path) {
    while (*path != '\0') {
      if (*path == '*')
        *path = ' ';
      path++;
    }
  }
}


// Subtype of Flash partition "DATA" entries in human-readable form
//
static const char *files_subtype2text(unsigned char subtype) {

  switch (subtype) {

    // Supported filesystems:
    case ESP_PARTITION_SUBTYPE_DATA_FAT: return " FAT/exFAT ";
    case ESP_PARTITION_SUBTYPE_DATA_SPIFFS: return "    SPIFFS ";
    case ESP_PARTITION_SUBTYPE_DATA_LITTLEFS: return "  LittleFS ";

    // Not supported file systems:
    case ESP_PARTITION_SUBTYPE_DATA_OTA: return "  OTA data ";
    case ESP_PARTITION_SUBTYPE_DATA_PHY: return "  PHY data ";
    case ESP_PARTITION_SUBTYPE_DATA_NVS: return " NVStorage ";
    case ESP_PARTITION_SUBTYPE_DATA_COREDUMP: return " Core dump ";
    case ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS: return "  NVS keys ";
    case ESP_PARTITION_SUBTYPE_DATA_EFUSE_EM: return " eFuse emu ";
    case ESP_PARTITION_SUBTYPE_DATA_UNDEFINED: return " Undefined ";
    case ESP_PARTITION_SUBTYPE_DATA_ESPHTTPD: return " ESP HTTPD ";

    default: return " *Unknown* ";
  }
}
// -- Mountpoints: allocation & query --
// These are represented by mountpoints[MOUNTPOINTS_NUM] global array which contains mount point path,
// partition label name and some other information
//
// Mountpoints are referenced by their **index** rather than pointers.


// Find a mountpoint (index in mountpoints[] array) by partition label (accepts shortened label names)
//
// /label/ - partition label name, may be shortened (e.g. "ff" instead of "ffat")
//           If /label/ is NULL, then this function returns first unused entry in mountpoints[] array.
//
// returns mountpoint index on success or -1 on failure
//
static int files_mountpoint_by_label(const char *label) {
  int i;
  for (i = 0; i < MOUNTPOINTS_NUM; i++)
    if ((!label && !mountpoints[i].label[0]) || (label && !q_strcmp(label, mountpoints[i].label)))
      return i;
  return -1;
}

// Find mountpoint index by arbitrary path.
//
// /path/     must be absolute (starts with "/")
// /reverse/  if set to true, then this function tries to resolve mountpoint even if supplied path
//            is shorter than mountpoint path length. This can happen if "shortened" arguments are
//            used for "unmount"
// returns index to mountpoints[] array or -1 on failure
//
static int files_mountpoint_by_path(const char *path, bool reverse) {
  int i;
  for (i = 0; i < MOUNTPOINTS_NUM; i++)
    if ((!path && !mountpoints[i].mp) || (path && mountpoints[i].mp && !q_strcmp(mountpoints[i].mp, path)) || (reverse && path && mountpoints[i].mp && !q_strcmp(path, mountpoints[i].mp)))
      return i;
  return -1;
}

// All this code is just to make esp_partition_find() be able to
// find a partition by incomplete (shortened) label name
//
// TODO: rewrite partition lookup code to use this function
// TODO: is q_strcmp ok here? What if we have /ffat and /ffat2 but ffat2 is enumerated first?
// TODO: add -e flag to "mount" to force strict strcmp
const esp_partition_t *files_partition_by_label(const char *label) {

  esp_partition_iterator_t it;

  if ((it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL)) != NULL)
    do {
      const esp_partition_t *part = esp_partition_get(it);
      if (part && (part->type == ESP_PARTITION_TYPE_DATA) && !q_strcmp(label, part->label)) {
        esp_partition_iterator_release(it);
        return part;
      }
    } while ((it = esp_partition_next(it)) != NULL);
  return NULL;
}

//
// Normalize "full path":
// Full paths are formed from relative paths by concatinatig the Cwd and 
// the relative portion.
//
// However, the "relative" portion can contain things like "../.././" which must be resolved.
// For example: we have CWD == "/ffat/configs/" and then user tries to create a file "../settings/a.txt"
// First, user supplied path (i.e. "../settings/a.txt") is added to CWD to get "/ffat/configs/../settings/a.txt".
// Then this "full path" (containing ../) is processed by normalize_path() to /ffat/settings/a.txt
//
// In ESPShell the CWD always has "/" at the beginning and "/" at the end (can be the same symbol if CWD == "/"):
// "/ffat/" or "/ffat/settings/" and even "/", so we assume that path[0] is "/" always.
//
static char *normalize_path(char *path) {

  char *src = path, 
       *dst = path;

  if (path[0] != '/' && path[0] != '\\') {
    q_printf("%% Internal error (normalize path \"%s\" failed)\r\n", path);
    return NULL;
  }

  while (*src) {
    // although src points at the beginning of out it is safe to have src-1 here:
    // the "if" condition below can not happen at the beginning of the path: the first symbol is always '/'
    //
    // Process "/..": remove the last path element in dst
    //
    if (*src == '.' && src[1] == '.' && *(src - 1) == '/') {
      src += 2;
      // remove last element from dst
      int j = 0;
      while (dst > path) {
        dst--;
        if (*dst == '/')
          j++;
        if (j == 2)
          break;
      }
    } else if (*src == '.' && src[1] == '/' && *(src - 1) == '/') {
    // Process "/./": ignore "./"
        src += 2;
    } else
    // Process all other symbols: just copy them
        *dst++ = *src++;
  }
  
  *dst = 0;
  return path;
}

// Build a full path from the given path and return a pointer to the resulting string.
// This function uses a static buffer to store the result, so it must not be called
// recursively unless the caller copies the result to a local buffer first.
// TODO: get rid of this static buffer: add 1 more out arg
//
// /path/       — an absolute or relative path
// /do_asterisk/ — whether '*' characters should be converted to spaces
//
// Returns a pointer to an internal buffer (auto-expandable up to 16 bytes).  
// On error, the function returns "/".
//
// TODO: there are multiple potential buffer overruns in this function: when CWD is long and path is long too
// TODO: must be refactored -> get rid of strcat and strcpy, replace them with strlcat and strlcpy
#define PROCESS_ASTERISK true
#define IGNORE_ASTERISK false

static char *files_full_path(const char *path, bool do_asterisk) {

  static char out[MAX_PATH + 16];
  int len, cwd_len;
  // default /full path/ when something fails is "/": the reason for that
  // is that it is not possible to do any damage to the root path "/"
  out[0] = '/';
  out[1] = '\0';

  // is cwd ok?
  if ((Cwd == NULL) && (files_set_cwd("/") == NULL))
    return out;

  len = strlen(path);

  if (path[0] == '/' || path[0] == '\\') {  // path is absolute. nothing to do - just return a copy
    if (len < sizeof(out))
      strcpy(out, path);
    else {
      VERBOSE(q_print("% Path is too long\r\n"));
    }

  } else {  // path is relative. add CWD
    cwd_len = strlen(Cwd);
    if ((len + cwd_len) < sizeof(out)) {
      strcpy(out, Cwd);
      strcat(out, path);
    }
  }

  if (do_asterisk)
    files_asterisk2spaces(out);

  normalize_path(out);

  return out;
}

// check if given path (directory or file) exists
// FIXME: spiffs allows for a/b/c/d and says its a valid path: it has then many consequences :(
static bool files_path_exist(const char *path, bool directory) {

  // LittleFS & FAT have proper stat() while SPIFFs doesn't
  // that why we do double check here
  struct stat st;
  DIR *d;

  if (!(path && *path))
    return false;

  // report that directory "/" does exist (actually it doesn't)
  if (files_path_is_root(path))
    return directory;

  int len = strlen(path);
  char path0[len + 1];

  strcpy(path0, path);
  files_strip_trailing_slash(path0);

  // try stat().. (FAT & LittleFS)
  if (0 == stat(path0, &st))
    return directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode);

  // try opendir()..(SPIFFS workaround: stat(path_to_directory) returns crap on SPIFFS)
  if (directory && (d = opendir(path0)) != NULL) {
    closedir(d);
    return true;
  }
  return false;
}

#define files_path_exist_file(X) files_path_exist((X), false)
#define files_path_exist_dir(X) files_path_exist((X), true)

 // TODO: make a special "SD card" flag for mount points instead of using gpp
 //       Moreover, move from wl_handle member to gpp as well
static INLINE bool files_mountpoint_is_sdspi(int mpi) {
  return mountpoints[mpi].gpp != NULL;
}

static int files_show_mountpoint(const char *path) {
  int mpi;
  if ((mpi = files_mountpoint_by_path(path, true)) >= 0) {

    q_printf("%% Mount point \"%s\", %s, (partition label is \"%s\")\r\n",mountpoints[mpi].mp,files_subtype2text(mountpoints[mpi].type), mountpoints[mpi].label);
#if WITH_FAT
    q_printf("%% Wear-levelling layer is %sactive on this media\r\n",mountpoints[mpi].wl_handle == WL_INVALID_HANDLE ? "NOT " : "");
#endif
#if WITH_SD
    if (files_mountpoint_is_sdspi(mpi)) {
      q_print("% Filesystem is located on a SD card (SPI bus)\r\n");
      sdmmc_card_t *card = (sdmmc_card_t *)mountpoints[mpi].gpp;
      MUST_NOT_HAPPEN(card == NULL);
      // TODO: write our own print_info
      sdmmc_card_print_info(stdout, card);
    } else
#endif  
    q_print("% Filesystem is located on internal SPI FLASH\r\n");
    // TODO: display sizes/usage
    // TODO: for FAT partition display sector size, number of FAT's
    return 0;
  } else
    q_printf("%% Can't find anything simialr to \"%s\"\r\n",path);
  return -1;
}


// return total bytes available on mounted filesystem index i (index in mountpoints[i])
//
static unsigned int files_space_total(int i) {

  switch (mountpoints[i].type) {
#if WITH_FAT
    case ESP_PARTITION_SUBTYPE_DATA_FAT:

      FATFS *fs;
      DWORD free_clust, tot_sect, sect_size;
      BYTE pdrv = 0; 

      if (files_mountpoint_is_sdspi(i)) {
//#warning "Developer reminder #1"

#if 1 // Set to 0 if it does not compile. This code works with newer versions of ESP-IDF
      // 
        vfs_fat_sd_ctx_t *ctx = get_vfs_fat_get_sd_ctx((sdmmc_card_t *)mountpoints[i].gpp);
        MUST_NOT_HAPPEN(ctx == NULL); // Or should we just return 0?
        pdrv = ctx->pdrv;
#else
        //return 0;
#endif        
      } else
        pdrv = ff_diskio_get_pdrv_wl(mountpoints[i].wl_handle);

      char drv[3] = { (char)(48 + pdrv), ':', 0 };
      if (f_getfree(drv, &free_clust, &fs) != FR_OK)
        return 0;
      tot_sect = (fs->n_fatent - 2) * fs->csize;
      sect_size = CONFIG_WL_SECTOR_SIZE;
      return tot_sect * sect_size;
#endif
#if WITH_LITTLEFS
    case ESP_PARTITION_SUBTYPE_DATA_LITTLEFS:
      size_t total, used;
      if (esp_littlefs_info(mountpoints[i].label, &total, &used))
        return 0;
      return total;
#endif
#if WITH_SPIFFS
    case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:
      if (esp_spiffs_info(mountpoints[i].label, &total, &used))
        return 0;
      return total;
#endif
    default:
  }
  return 0;
}

// return amount of space available for allocating
//
static unsigned int files_space_free(int i) {
  switch (mountpoints[i].type) {
#if WITH_FAT
    case ESP_PARTITION_SUBTYPE_DATA_FAT:
      FATFS *fs;
      DWORD free_clust, free_sect, sect_size;
      BYTE pdrv = 0;

      if (files_mountpoint_is_sdspi(i)) {
#if 1 // Set to 0 if it does not compile. TODO: #if (ESP_VERSION...)
        vfs_fat_sd_ctx_t *ctx = get_vfs_fat_get_sd_ctx((sdmmc_card_t *)mountpoints[i].gpp);
        MUST_NOT_HAPPEN(ctx == NULL);
        pdrv = ctx->pdrv;
#else
        //return 0;
#endif        
      } else
        pdrv = ff_diskio_get_pdrv_wl(mountpoints[i].wl_handle);
      char drv[3] = { (char)(48 + pdrv), ':', 0 };
      if (f_getfree(drv, &free_clust, &fs) != FR_OK)
        return 0;

      free_sect = free_clust * fs->csize;
      sect_size = files_mountpoint_is_sdspi(i) ? 512 : CONFIG_WL_SECTOR_SIZE;
      return free_sect * sect_size;
#endif
#if WITH_LITTLEFS
    case ESP_PARTITION_SUBTYPE_DATA_LITTLEFS:
      size_t total, used;
      if (esp_littlefs_info(mountpoints[i].label, &total, &used))
        return 0;
      return total - used;
#endif
#if WITH_SPIFFS
    case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:
      if (esp_spiffs_info(mountpoints[i].label, &total, &used))
        return 0;
      return total - used;
#endif
    default:
  }
  return 0;
}

// handy macro
#define files_space_used(I) (files_space_total(I) - files_space_free(I))

// callback which is called by files_walkdir() on every entry it founds.
typedef int (*files_walker_t)(const char *path, void *aux);

// Walk thru the directory tree starting at /path/ (i.e. /path/ itself and all its subdirs)
// on every file entry file_cb() is called, on every directory entry dir_cb() is called
// Callbacks (if not NULL) must return an integer value, which is accumulated and then returned as a result of this
// function
static unsigned int files_dirwalk(const char *path0, files_walker_t files_cb, files_walker_t dirs_cb, void *arg, int depth) {

  char *path = NULL;
  int len;
  DIR *dir;
  unsigned int processed = 0;

  if (depth < 1)
    return 0;

  // figure out full path, if needed
  if ((path = q_strdup256(files_full_path(path0, PROCESS_ASTERISK), MEM_PATH)) == NULL)
    return 0;

  if ((len = strlen(path)) > 0) {
    // directory exists?
    if (files_path_exist_dir(path)) {
      // append "/"" to the path if it was not there already
      if (path[len - 1] != '\\' && path[len - 1] != '/') {
        path[len++] = '/';
        path[len] = '\0';
      }

      // Walk through the directory, entering all subdirs in a recursive way
      if ((dir = opendir(path)) != NULL) {
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {

          // path buffer has 256 bytes extra space
          if (strlen(de->d_name) < MAX_FILENAME) {
            path[len] = '\0';          // cut off previous addition
            strcat(path, de->d_name);  // add entry name to our path

            // if its a directory - call recursively
            if (de->d_type == DT_DIR)
              processed += files_dirwalk(path, files_cb, dirs_cb, NULL, depth - 1);
            else if (files_cb)
              processed += files_cb(path, arg);
          }
        }
        closedir(dir);
        path[len] = '\0';
        if (dirs_cb)
          processed += dirs_cb(path, arg);
      }
    }
  }
  if (path)
    q_free(path);
  return processed;
}

// Callback to be used by files_remove() when it calls files_dirwalk()
// This callback gets called for every file that needs to be removed
//
static int remove_file_callback(const char *path, UNUSED void *aux) {
  if (0 != unlink(path)) {
    HELP(q_printf("%% <e>Failed to delete: \"%s\"</>\r\n", path));
    return 0;
  }
  HELP(q_printf("%% Deleted file: \"<g>%s</>\"\r\n", path));
  return 1;
}

// Callback to be used by files_remove() when it calls to files_dirwalk()
// This callback gets called for every DIRECTORY that needs to be removed
//
static int remove_dir_callback(const char *path, UNUSED void *aux) {
  if (rmdir(path) == 0) {
    HELP(q_printf("%% Directory removed: \"<i>%s</>\"\r\n", path));
    return 1;
  }
  HELP(q_printf("%% <e>Failed to delete: \"%s\"</>\r\n", path));
  return 0;
}

// Remove file/directory with files recursively
// /path/  is file or directory path
// /depth/ is max recursion depth
// Returns number of items removed (files+directories)
//
static int files_remove(const char *path0, int depth) {
  char path[MAX_PATH + 16];  // TODO: use dynamic memory

  if (depth < 1)
    return 0;

  // make a copy of full path as files_full_path()'s buffer is not reentrant (static)
  strcpy(path, files_full_path(path0, PROCESS_ASTERISK));

  if (files_path_exist_file(path))  // a file?
    return unlink(path) == 0 ? 1 : 0;
  else if (files_path_exist_dir(path))  // a directory?
    return files_dirwalk(path, remove_file_callback, remove_dir_callback, NULL, DIR_RECURSION_DEPTH);
  else  // bad path
    q_printf("%% <e>File/directory \"%s\" does not exist</>\r\n", path);
  return 0;
}


// Callback to be used by files_size() when it calls to files_dirwalk()
// This callback gets called for every FILE which size was requested
// TODO: size is unsigned!!
static int size_file_callback(const char *p, UNUSED void *aux) {
  struct stat st;
  if (stat(p, &st) == 0)
    return st.st_size;
  return 0;
}

// get file/directory size in bytes
// /path/ is the path to the file or to the directory
//
static unsigned int files_size(const char *path) {

  struct stat st;
  char p[MAX_PATH + 16] = { 0 };

  strcpy(p, files_full_path(path, PROCESS_ASTERISK));

  // size of a file requested
  if (files_path_exist_file(p)) {
    if (stat(p, &st) == 0)
      return st.st_size;
    q_printf("files_size() : stat() failed on an existing file \"%s\"\r\n", p);
    return 0;
  }

  // size of a directory requested
  if (files_path_exist_dir(p)) {
    return files_dirwalk(path, size_file_callback, NULL, NULL, DIR_RECURSION_DEPTH);
  }

  q_printf("%% <e>Path \"%s\" does not exist\r\n", p);
  return 0;
}

// display (or send over uart interface) binary file content starting from byte offset "line"
// "count" is either 0xffffffff (means "whole file") or data length
//
// When displayed the file content is formatted as a table so it is easy to read. When file is
// transferred to another UART raw content is sent instead so file can be saved on the remote side
//
// /device/ is either uart nunmber (to send raw data) or -1 to do fancy human readable output
// /path/ is the full path
//
static int files_cat_binary(const char *path0, unsigned int line, unsigned int count, unsigned char device) {

  unsigned int size, sent = 0, plen = 5 * 1024;  //TODO: use 64k blocks if we have SPI RAM
  unsigned char *p;
  char path[MAX_PATH + 16];
  FILE *f;
  size_t r;

  if (strlen(path0) > MAX_PATH) {
    q_printf("%% Path is too long. Maximum length is %u characters\r\n",MAX_PATH);
    return 0;
  }

  strcpy(path,path0);

  if ((size = files_size(path)) > 0) {
    if (line < size) {
      if (size < plen)
        plen = size;
      if ((p = (unsigned char *)q_malloc(plen + 1, MEM_TMP)) != NULL) {
        if ((f = fopen(path, "rb")) != NULL) {
          if (line) {
            if (fseek(f, line, SEEK_SET) != 0) {
              q_printf("%% <e>Can't position to offset %u (0x%x)\r\n", line, line);
              goto fail;  //TODO: rewrite
            }
          }
          while (!feof(f) && (count > 0)) {
            if ((r = fread(p, 1, count < plen ? count : plen, f)) > 0) {
              count -= r;
              if (device == (unsigned char)(-1))
                q_printhex(p, r);
              else
                uart_write_bytes(device, p, r);
              sent += r;
            }
          }
          HELP(q_printf("%% EOF (%u bytes)\r\n", sent));
fail:
          fclose(f);
        } else q_printf("%% <e>Failed to open \"%s\" for reading</>\r\n", path);
        q_free(p);
      } else q_print("%% Out of memory\r\n");
    } else q_printf("%% <e>Offset %u (0x%x) is beyound the file end. File size is %u</>\r\n", line, line, size);
  } else q_printf("%% Empty file (size = %u)\r\n",size);
  return 0;
}

// read file 'path' line by line and display it as that
// /line/ & /count/ here stand for starting line and line count to display
// if /numbers/ is true then line numbers are added to output stream
//
static int files_cat_text(const char *path, unsigned int line, unsigned int count, unsigned char device, bool numbers) {

  FILE *f;
  char *p = NULL;
  unsigned int plen = 0, cline = 0;
  int r;

  if ((f = fopen(path, "rb")) != NULL) {
    while (count && (r = files_getline(&p, &plen, f)) >= 0) {
      cline++;
      if (line <= cline) {
        count--;
        if (device == (unsigned char)(-1)) {
          if (numbers)
            q_printf("%4u: ", cline);
          q_print(p);
          q_print(CRLF);
        } else {
          char tmp[16];
          if (numbers) {

            sprintf(tmp, "%4u: ", cline);

            uart_write_bytes(device, tmp, strlen(tmp));
          }
          uart_write_bytes(device, p, r);
          tmp[0] = '\n';
          uart_write_bytes(device, tmp, 1);
        }
      }
    }
    if (p)
      q_free(p);
    fclose(f);
  } else
    q_printf("%% <e>Can not open file \"%s\" for reading</>\r\n", path);
  return 0;
}

// for given path "some/path/with/directories/and/files.txt" creates all the directories
// if they do not exist.
//
// /path0/         - relative or absolute path
// /last_is_file/  - if /true/ then last component of the path will be ignored
//
#define PATH_HAS_FILENAME true
#define PATH_HAS_ONLY_DIRS false
static int files_create_dirs(const char *path0, bool last_is_file) {

  int argc, len, i, ret = 0, created = 0;
  char **argv = NULL, *path;
  char buf[MAX_PATH + 16] = { 0 };

  // don't process asterisk now: this will interfere with argify() as argify() uses spaces
  // as token separator. Convert asterisks later.
  if ((len = strlen((path = files_full_path(path0, IGNORE_ASTERISK)))) > 0) {

    // replace all path separators with spaces: this way we can use argify()
    // to split it to components.
    // This is bad but.. Replace all spaces (yes spaces can present in one single argv since 0.99.9) with asterisks.
    // Thankgs god we didn't process "*" yet
    for (i = 0; i < len; i++)
      if (path[i] == '/' || path[i] == '\\')
        path[i] = ' ';
      else if (path[i] == ' ')
        path[i] = '*';

    // argify and strip last component if it is a file
    if ((argc = argify((unsigned char *)path, (unsigned char ***)&argv)) > 0) {
      if (last_is_file)
        argc--;
      if (argc > 0) {
        // walk thru all path components and create them if do not exist
        for (i = 0; i < argc; i++) {
          strcat(buf, "/");
          files_asterisk2spaces(argv[i]);
          strcat(buf, argv[i]);
          if (!files_path_exist_dir(buf)) {
            if (mkdir(buf, 0777) != 0) {
              ret = -1;
              HELP(q_printf("%% <e>Failed to create directory \"%s\"</>\r\n", buf));
              goto fail;  // more readable than "break" (both compile to the same code)
            }
            HELP(q_printf("%% Created directory: \"<i>%s</>\"\r\n", buf));
            created++;
          }
        }
      }
    }
  }

fail:
  if (argv)
    q_free(argv);
  return ret;
}
#if 0
static const char *files_relative_path(const char *path) {
  if (path) {
    if (*path == '/' || *path == '\\') {
      if (!q_strcmp(files_get_cwd(),path))
        return &path[strlen(files_get_cwd())];
      else
        // path is absolute but doesn't match CWD.
        return "/";
    }
  }
  return path;
}
#endif

static const char *files_path_last_component(const char *path) {
  if (path && *path) {
    int plen = strlen(path) - 1;

    // remove trailing path separators
    while (plen >= 0 && (path[plen] == '/' || path[plen] == '\\'))
      plen--;

    // find rightmost path separator and return pointer to the path component next to it
    while (plen >= 0) {
      if (path[plen] == '/' || path[plen] == '\\')
        return &path[plen + 1];
      plen--;
    }
  }
  return path;
}


static bool files_copy(const char *src, const char *dst) {
  const int blen = 5 * 1024;
  ssize_t rd = -1, wr = -1;

  if (src && dst && (*src == '/' || *src == '\\') && (*dst == '/' || *dst == '\\')) {
    char *buf;
    int s, d;

    if (files_create_dirs(dst, PATH_HAS_FILENAME) >= 0) {
      if ((s = open(src, O_RDONLY)) > 0) {
        if ((d = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666)) > 0) {
          if ((buf = (char *)q_malloc(blen, MEM_TMP)) != NULL) {
            do {
              if ((rd = read(s, buf, blen)) > 0)
                if (rd == (wr = write(d, buf, rd))) {
                  // TODO: update_copy_progress()
                  // TODO: ctrlc_pressed() 
                  // TODO: kill 
                  q_yield();  //make WDT happy
                  continue;
                }
              // errors during copy :(
            } while (rd > 0 && wr > 0);
            q_free(buf);
          }
          close(d);
          if (rd < 0 || wr < 0) {
            q_printf("%% There were errors (rd=%d, wr=%d) , removing incomplete file \"%s\"\r\n", rd, wr, dst);
            unlink(dst);
          }
        } else
          q_printf("%% <e>Failed to open \"%s\" for writing</>\r\n", dst);
        close(s);
      } else
        q_printf("%% <e>Failed to open \"%s\" for reading</>\r\n", src);
    } else
      q_printf("%% <e>Failed replicate directory structure for \"%s\"</>\r\n", dst);
  }
  return rd >= 0 && wr >= 0;
}

#if WITH_SD

#  if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
#    define dma_for_spi(_X) (_X)// TODO:look into esp-idf sources. why not CH_AUTO?
#  else
#    define dma_for_spi(_X) SPI_DMA_CH_AUTO
#  endif


// Mount an SD card (FAT filesystem is implied).
// /mp/  - asciiz mountpoint 
// /mpi/ - index in mountpoints[] which is responsible for *this* mount operation. mountpoints[mpi].mp is NULL at this point (use /mp/ arg instead)
// /spi/ - one of SPI1_HOST, SPI2_HOST or SPI3_HOST
//
static int sd_mount(const char *mp, int mpi, int spi, int miso, int mosi, int clk, int cs, int freq_khz) {


  esp_err_t             ret;
  sdmmc_card_t         *card;
  sdmmc_host_t          host = SDSPI_HOST_DEFAULT();
  sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
  spi_bus_config_t      bus = { 0 };
  esp_vfs_fat_sdmmc_mount_config_t mount_options = { 0 };


  mountpoints[mpi].gpi = -1;

  // initialize SPI bus. Work around the case when SPI bus is already initialized.
  bus.mosi_io_num = mosi,
  bus.miso_io_num = miso,
  bus.sclk_io_num = clk,
  bus.quadwp_io_num = -1,
  bus.quadhd_io_num = -1,
  bus.max_transfer_sz = 4000,
    
  ret = spi_bus_initialize(spi, &bus, dma_for_spi(spi));
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
      q_printf("%% <e>Failed to initialize SPI%d bus</>\r\n",spi);
      return -1;
  }

  // free spi bus later or not? if it was initialized before then we dont deinit it (ret == ESP_INVALID_STATE)
  mountpoints[mpi].gpi = (ret == ESP_OK) ? spi : -1;

  // initialize data structures which are used by mount routine
  mount_options.format_if_mount_failed = false;
  mount_options.max_files = 2;
  mount_options.allocation_unit_size = 16 * 1024;

  host.slot = spi;
  host.max_freq_khz = freq_khz > 0 ? freq_khz : 20000;
 
  device.gpio_cs = cs;
  device.host_id = spi;

  // try to mount
  if (( ret  = esp_vfs_fat_sdspi_mount(mp, &host, &device, &mount_options, &card)) != ESP_OK) {
    if (mountpoints[mpi].gpi >= 0) {
      spi_bus_free(mountpoints[mpi].gpi);
      mountpoints[mpi].gpi = -1;
    }
    q_print(Failed);
    return -1;
  }

  // save device-specific information for later: we need it for "format" and "unmount"
  mountpoints[mpi].gpp = card;
  mountpoints[mpi].type = ESP_PARTITION_SUBTYPE_DATA_FAT;

  return 0;
}


static int sd_unmount(int mpi) {

  if (mpi >= 0 && mpi < MOUNTPOINTS_NUM && mountpoints[mpi].mp && mountpoints[mpi].gpp ) {
    esp_vfs_fat_sdcard_unmount(mountpoints[mpi].mp, (sdmmc_card_t *)mountpoints[mpi].gpp);
    mountpoints[mpi].gpp = NULL;
    if (mountpoints[mpi].gpi >= 0) {
      spi_bus_free(mountpoints[mpi].gpi);
      mountpoints[mpi].gpi = -1;
    }
    return 0;
  }
  return -1;
}

// SD card type 
static const char *sd_type(int mpi) {
  sdmmc_card_t *card = (sdmmc_card_t *)mountpoints[mpi].gpp;
  return  card == NULL ? "????"
                       : (card->is_sdio ? "SDIO"
                                        : (card->is_mmc ? "eMMC"
                                                        : "SDHC"));
}

// SD capacity in MB. TODO: files_total_size() does not work correctly for FAT on SDSPI. Probably sector size issue
static unsigned int sd_capacity_mb(int mpi) {
  sdmmc_card_t *card = (sdmmc_card_t *)mountpoints[mpi].gpp;
  return card ? (unsigned int)(((uint64_t) card->csd.capacity) * card->csd.sector_size / (1024 * 1024))
              : 0;
}

#endif //WITH_SD

// Twin brother of alias_exec() but for files.
// Supposed to be called by the shell handlers. If called directly, then will have no effect on user prompt
//
// Return codes: 0 on success, <0 if file is not accessible, >0 number of errors during execution
//
static int files_exec(const char *name) {

  FILE *f;
  char *p = NULL, *path;
  unsigned int plen = 0, cline = 0;
  int r, errors = 0;

  path = files_full_path(name, PROCESS_ASTERISK); 

  // Open the file. Read it line by line and execute. Count lines and errors
  if ((f = fopen(path, "rb")) != NULL) {
    // We don't want the history to be updated with these commands
    // TODO: make better mechanism to disable history on demand or, at least, make access to the History
    // TODO: here to be atomic; or may be better add 1 extra arg to espshell_command( ... , bool dont_add_to_history)
    int h = History;
    History = 0;
    while (!feof(f) && (r = files_getline(&p, &plen, f)) >= 0) {
      cline++;
      if (r > 0 && p && *p) {
        char *pp = q_strdup(p, MEM_TMP); //espshell_command frees the buffer
        if (pp && espshell_command(pp, NULL) != 0)
          errors++;
      }
    }
    History = h;
    if (p)
      q_free(p);
    fclose(f);
    q_printf("%% file %s:, %d lines, %d errors\r\n",name,cline,errors);
  } else
    q_printf("%% file %s: failed to open\r\n",name);

  return f ? errors  // 0 == "Success" or >0 == "number of errors"
           : -1;     // "File is not accessible"
}

// <TAB> (Ctrl+I) handler. Jump to next argument
// until end of line is reached. start to jump back
//
// In file manager mode try to perform basic autocomplete (TODO: not implemented yet)
//
static EL_STATUS tab_pressed() {

  if (Point < End)
    return do_forward(CSmove);
  else {
    if (Point) {
      Point = 0;
      return CSmove;
    }
    return CSstay;
  }
}

// API for "cd .."
// Changes CWD (cwd is thread-local)
//
static bool files_cdup() {

  int i;
  char *p;

  if ((i = strlen(Cwd)) < 3)  //  minimal path "/a/". root directory reached
    return true;


  // remove trailing path separator and reverse search
  // for another one. It MUST be at least 2 of them: "/root_path/"
  files_strip_trailing_slash(Cwd);

  if (NULL == (p = strrchr(Cwd, '/')))
    MUST_NOT_HAPPEN(NULL == (p = strrchr(Cwd, '\\')));

  
  p[1] = '\0';             // strip everything after it
  if (Cwd[0] == '\0')      // not much have left: change to "/"
    files_set_cwd("/");
  else {
      
    // partition can be mounted under /a/b/c but /a, /a/b do not exist so
    // "cd .." from "/a/b/c/" should not end up at "/a/b/":
    // repeat "cd .." until we reach a path that exists.
    if (!files_path_exist_dir(Cwd))
      return files_cdup();

  }
  files_set_cwd(Cwd);  //update prompt
  return true;
}

// API for "cd"
// Change CWD to filesystem's mount point
//
static bool files_cd_mount_point() {
  int i;
  // find the filesystem mountopint. Change to "/" if mountpoint is not found
  if ((i = files_mountpoint_by_path(files_get_cwd(), false)) < 0)
    files_set_cwd("/");
  else
    files_set_cwd(mountpoints[i].mp);
  return true;
}
// bool files_cd(const char *path)
//
// API for "cd", "cd ..", and "cd /asdasd/asdasd/.././../../ddfsdf"
//
// If /path/==NULL, then CWD is set to filesystem's mount point
// If /path/=="" this functions does nothing
// If /path/ is a valid absolute or relative path then this function extracts 1 element from the path, performs cd to that path element
// and calls files_cd() recursively with rest of the path:
//
// E.g. input path argument is "/some/path/../other/path", then the execution sequence will be:
// 1. Change to "/"
// 2. Change to "some"
// 3. Change to "path"
// 4. Change to ".."
// 5. Change to "other" and so on
//
// Max depth of recursion is limited to DIR_RECURSION_DEPTH
//
#define files_cd(_Path) files_rcd(_Path, 0)

// Actual code, "recursive cd"
//
static bool files_rcd(const char *path, int recursion_depth) {

  int i = 0;
  char *element = NULL;

  if (++recursion_depth > DIR_RECURSION_DEPTH) {
    HELP(q_print("% Path is too long (>" xstr(DIR_RECURSION_DEPTH) " dirs).\r\n"
                 "% Increase DIR_RECURSION_DEPTH macro in ESPShell"));
    return false;
  }

  // path == NULL? change to mountpoint
  if (path == NULL)
    return files_cd_mount_point();

  // if we were called with empty path - just do nothing
  if (path[0] == '\0')
    return true;
  
  // Dots in the path.
  // Change up on "..", do nothing on "."
  //
  if (path[0] == '.') {
    if (path[1] == '.') {
      if (path[2] == '/' || path[2] == '\\' || path[2] == '\0') {
        //VERBOSE(q_printf("%% CD ..\r\n"));
        files_cdup(); // Ignore errors
        return files_cd(path + (path[2] ? 3 : 2));
      }
    } else if (path[1] == '/' || path[1] == '\\') { // "./"
      //VERBOSE(q_printf("%% CD \"%s\"\r\n",path + 2));
      return files_cd(path + 2);
    }
  }

  // Absolute path: change to root and call files_cd() for the rest of the path
  if (path[0] == '/' || path[0] == '\\') {
    //VERBOSE(q_printf("%% CD \r\n%% CD \"%s\"\r\n",path+1));
    files_set_cwd("/");
    return files_cd(path + 1);
  }

  // Copy 1 path element (text up to a path separator) ("path/to/the/file.txt" has 4 elements)
  // to the /element[]/. Since we don't know the element size we assume MAX_PATH, which is 256 symbols: having 
  // it as an array on a stack results in stack overflow if recursion depth is > 10-20, thats why we have to use malloc here.
  // It CAN be declared global array despite the recursion, however this will not go well with multiple tasks performing
  // "cd" operation at the same time. Having a global buffer protected with mutex is an overkill.
  ///
  element = (char *)q_malloc(MAX_PATH, MEM_TMP);
  if (!element)
    return false;

  for (i = 0; i < MAX_PATH; i++) {

    // last path element? unwind our recursive call stack
    if (path[i] == '\0') {
      element[i] = '\0';
      char *full = files_full_path(element, 0);
      if (full && files_path_exist_dir(full)) {
          files_set_cwd(full);
          //VERBOSE(q_printf("%% Changing directory to %s\r\n", full));
          q_free(element);
          return true;
      } else {
        HELP(q_printf("%% cd : path element \"%s\" does not exist\r\n", full));
        q_free(element);
        return false;
      }
    }

    // A path separator? call files_cd(element) 
    // (will be treated as "the last ppath element", because /element/ has no path separator in it)
    //
    // cd /dev/logs/test
    //
    // will result in series of calls to files_cd():
    //
    // files_cd("/")
    // files_cd("dev")
    // files_cd("logs")
    // files_cd("test")
    //
    if (path[i] == '/' || path[i] == '\\') {
      element[i] = '\0';
      //VERBOSE(q_printf("%% CD \"%s\"\r\n%% CD \"%s\"\r\n",element,path+i+1));
      bool ok = files_cd(element);
      q_free(element);
      
      return ok ? files_cd(path + i + 1) : false;
    }

    // Just text - add it up to /element/
    element[i] = path[i];
  }
  HELP(q_print("% Path is too long. Must be <" xstr(MAX_PATH) "\r\n"));
  q_free(element);
  return false;
}

// Open file for reading and/or writing. Create file if does not exist. Create
// all directories in the path
//
static FILE *files_fopen(const char *name, const char *mode) {

  FILE *fp;

  if (*mode == 'a' || *mode == 'w')
    if (files_create_dirs(name, PATH_HAS_FILENAME) < 0) {
      q_print("% <e>Failed to create path for a file</>\r\n");
      return NULL;
    }

  name = files_full_path(name, PROCESS_ASTERISK); // TODO: not reentrant!
  if ((fp = fopen(name,mode)) == NULL)
    q_printf("%% Failed to open \"%s\"\r\n",name);
  return fp;
}



// Create file if does not exist. If it does - update the timestamp
//
static int files_touch(const char *name) {

  FILE *fp;
  if ((fp = files_fopen(name,"a+")) != NULL) {
    fclose(fp);
    return 0;
  }
  q_printf("%% <e>Can't touch \"%s\" (errno=%d)</>\r\n", name, errno);
  return -1;
}

// "files"
// switch to the FileManager commands subtree
//
static int cmd_files_if(int argc, char **argv) {

  // file manager actual prompt is set by files_set_cwd()
  change_command_directory(0, KEYWORDS(files), PROMPT, "filesystem");

  // Initialize CWD if not initialized previously, update user prompt.
  if (Cwd == NULL)
    files_set_cwd("/");
  else
    files_set_cwd(Cwd); // Special case (new buffer == internal buffer). Regenerate the prompt;
  return 0;
}


// "unmount /Mount_point"
// "unmount"
//
// Unmount a filesystem
//
static int cmd_files_unmount(int argc, char **argv) {

  int i;
  esp_err_t err = -1;
  char *path;
  char path0[MAX_PATH + 16];

  // no mountpoint provided:
  // use CWD to find out mountpoint
  if (argc < 2) {
    if ((path = (char *)files_get_cwd()) == NULL)
      return 0;
    MUST_NOT_HAPPEN(strlen(path) >= sizeof(path0));
    strcpy(path0, path);
    path = path0;
  } else
    path = argv[1];

  // mount/unmount fails if path ends with slash
  files_strip_trailing_slash(path);

  // expand name if needed
  path = files_full_path(path, PROCESS_ASTERISK);

  // find a corresponding mountpoint
  if ((i = files_mountpoint_by_path(path, true)) < 0) {
    q_printf("%% <e>Unmount failed: nothing is mounted on \"%s\"</>\r\n", path);
    return 0;
  }

  // Process "unmount" depending on filesystem type
  switch (mountpoints[i].type) {
#if WITH_FAT
    case ESP_PARTITION_SUBTYPE_DATA_FAT:
#if WITH_SD
      if (files_mountpoint_is_sdspi(i)) {
        if (sd_unmount(i) == 0)
          goto finalize_unmount;
        goto failed_unmount;
      }
#endif    
      if (mountpoints[i].wl_handle != WL_INVALID_HANDLE)
        if ((err = esp_vfs_fat_spiflash_unmount_rw_wl(mountpoints[i].mp, mountpoints[i].wl_handle)) == ESP_OK)
          goto finalize_unmount;
      goto failed_unmount;
#endif
#if WITH_SPIFFS
    case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:
      if (esp_spiffs_mounted(mountpoints[i].label))
        if ((err = esp_vfs_spiffs_unregister(mountpoints[i].label)) == ESP_OK)
          goto finalize_unmount;
      goto failed_unmount;
#endif
#if WITH_LITTLEFS
    case ESP_PARTITION_SUBTYPE_DATA_LITTLEFS:
      if (esp_littlefs_mounted(mountpoints[i].label))
        if ((err = esp_vfs_littlefs_unregister(mountpoints[i].label)) == ESP_OK)
          goto finalize_unmount;
          // FALLTHRU
#endif
    default:
      // FALLTHRU
  }

failed_unmount:
  q_printf("%% <e>Unmount failed, error code is \"0x%x\"</>\r\n", err);
  return 0;

finalize_unmount:

  HELP(q_printf("%% Unmounted %s partition \"%s\"\r\n", files_subtype2text(mountpoints[i].type), mountpoints[i].mp));

#if WITH_FAT
  mountpoints[i].wl_handle = WL_INVALID_HANDLE;
#endif
  q_free(mountpoints[i].mp);
  mountpoints[i].mp = NULL;
  mountpoints[i].label[0] = '\0';

  // adjust our CWD after unmount: our working directory may be not existent anymore
  if (!files_path_exist_dir(files_get_cwd()))
    files_set_cwd("/");
  return 0;
}

#if WITH_SD
// "mount vspi|hspi|fspi|spi1|spi2|spi3 MISO MOSI CLK CS [FREQ_KHZ] [/MOUNTPOINT]"
//
static int cmd_files_mount_sd(int argc, char **argv) {

  int i,bus, mosi, miso, clk, cs, tmp;
  unsigned int freq = 20000; // 20MHz 
  char mp0[16];
  char *mp = NULL;
  esp_err_t err = 0;

  // enough arguments?
  if (argc < 6)  //mount vspi 19 23 18 4 400 /mountpoint
    return CMD_MISSING_ARG;

  // SPI bus to use.
  // valid keywords are: hspi, vspi, fspi, spi1, spi2, spi3
  if (strlen(argv[1]) != 4)
    goto bad_spi_keyword;

  switch (argv[1][0]) {
    case 'v': bus = SPI3_HOST; break;
    case 'f': bus = SPI1_HOST; break;
    case 'h': bus = SPI2_HOST; break;
    case 's': switch(argv[1][3]) {
                case '1': bus = SPI1_HOST; break;
                case '2': bus = SPI2_HOST; break;
                case '3': bus = SPI3_HOST; break;
                default : goto bad_spi_keyword; 
              };
              break;
  bad_spi_keyword:
    default : HELP(q_print("% Use \"fspi\", \"hspi\", \"vspi\", \"spi1\", \"spi2\" and \"spi3\" as the SPI bus name\r\n"));
              return 1;
  };
  // TODO: define non existent SPIx_HOST as 255 
  if (bus == 255) {
    q_printf("%% SPI bus \"%s\" is not available on this SoC\r\n",argv[1]);
    return 0;
  }

  if (!pin_exist((miso = q_atol(argv[2],999)))) return 2;
  if (!pin_exist((mosi = q_atol(argv[3],999)))) return 3;
  if (!pin_exist((clk  = q_atol(argv[4],999)))) return 4;
  if (!pin_exist((cs   = q_atol(argv[5],999)))) return 5;

  i = 6;
  // Two optional arguments: frequency (in kHz) and a mountpoint
  // If argument is numeric then it is a frequency
  // If argument starts with "/" then its a mountpoint
  // Report syntax error in arg otherwhise
  
  // default mountpoint
  mp = mp0;
  sprintf(mp0, "/sdcard%d",cs);

  while (i < argc) {
    if (isnum(argv[i]))
      freq = atol(argv[i]);
    else 
    if (argv[i][0] == '/')
      mp = argv[i];
    else
      return i;
    i++;
  }

  if (freq < 400 || freq > 20000)
    q_printf("%% warning: frequency %u is out of [400..20000] range (400kHz..20MHz)\r\n",freq);

  files_strip_trailing_slash(mp);  // or mount fails :-/
  if (!*mp) {
    HELP(q_print("% <e>Directory name required: can't mount to \"/\"</>\r\n"));
    return 2;
  }

  // due to VFS internals there are restrictions on mount point length.
  // longer paths will work for mounting but fail for unmount so we just
  // restrict it here
  if (strlen(mp) >= sizeof(mp0)) {
    q_printf("%% <e>Mount point path max length is %u characters</>\r\n", sizeof(mp0) - 1);
    return 0;
  }

  // find free slot in mountpoints[] to mount a new partition
  if ((i = files_mountpoint_by_path(NULL, false)) >= 0) {
    // check if selected mount point is not used
    if ((tmp = files_mountpoint_by_path(mp, false)) < 0) {
      if (sd_mount(mp,i,bus,miso,mosi,clk,cs,freq) == 0) {
        if ((mountpoints[i].mp = q_strdup(mp, MEM_PATH)) != NULL) {
          // create fake label name: "sdvspi4", "sdhspi2" or "sdfspi39" made up of "sd" + spi name + CS pin number
          sprintf(mountpoints[i].label,"sd%s%d",argv[1],cs);   
          HELP(q_printf("%% %s : FAT on SD card is mounted under \"%s\" (SPI%d)\r\n",mountpoints[i].label, mp, bus));
#if 0          
          if (mountpoints[i].gpi > 0)
            HELP(q_printf("%% SPI%d bus will be de-initialized later, on \"unmount\"\r\n",bus));
#endif            
          return 0;
        } else
          return 0;
      } else
          q_print(Failed); // sd_mount() has its own report
    } else
      q_printf("%% <e>Mount point \"%s\" is already used by partition \"%s\"</>\r\n", mp, mountpoints[tmp].label);  
  } else
    q_print("% <e>Too many mounted filesystems, increase MOUNTPOINTS_NUM</>\r\n"); 

  q_printf("%% <e>SD card mount (over %s bus) failed (error: %d)</>\r\n", argv[1], err);
  if (i >=0 )
    mountpoints[i].wl_handle = WL_INVALID_HANDLE;
  return 0;

}
#endif //WITH_SD

// "mount LABEL [/MOUNTPOINT"]
// TODO: "mount sdmmc PIN_CMD PIN_CLK PIN_D0 [D1 D2 D3]
// TODO: "mount [sdspi|spi] PIN_MISO PIN_MOSI PIN_CLK PIN_CS
//
// mount a filesystem. filesystem type is defined by its label (see partitions.csv file).
// supported filesystems: fat, littlefs, spiffs
//

static int cmd_files_mount(int argc, char **argv) {

  int i;
  char mp0[ESP_VFS_PATH_MAX * 2];  // just in case
  char *mp = NULL;
  const esp_partition_t *part = NULL;
  esp_partition_iterator_t it;
  esp_err_t err = 0;

  // enough arguments?
  if (argc < 2)
    return CMD_MISSING_ARG;

  // is mountpoint specified?
  // is mountpoint starts with "/"?
  if (argc > 2) {
    mp = argv[2];
    if (mp[0] != '/') {
      HELP(q_print("% <e>Mount point must begin with \"/\"</>\r\n"));
      return 2;
    }
  } else {
    // mountpoint is not specified: use partition label and "/"
    // to make a mountpoint
    if (strlen(argv[1]) > ESP_VFS_PATH_MAX) {
      HELP(q_print("% <e>Invalid partition name (too long)</>\r\n"));
      return 1;
    }

    // following is wrong for cases when user enters incomplete label name. it is
    // fixed later
    snprintf(mp0,sizeof(mp0), "/%s", argv[1]);
    mp = mp0;
  }

  files_strip_trailing_slash(mp);  // or mount fails :-/
  if (!*mp) {
    HELP(q_print("% <e>Directory name required: can't mount to \"/\"</>\r\n"));
    return 2;
  }

  // due to VFS internals there are restrictions on mount point length.
  // longer paths will work for mounting but fail for unmount so we just
  // restrict it here
  if (strlen(mp) >= sizeof(mp0)) {
    q_printf("%% <e>Mount point path max length is %u characters</>\r\n", sizeof(mp0) - 1);
    return 0;
  }

  // find free slot in mountpoints[] to mount new partition
  if ((i = files_mountpoint_by_path(NULL, false)) < 0) {
    q_print("% <e>Too many mounted filesystems, increase MOUNTPOINTS_NUM</>\r\n");
    return 0;
  }

  // find requested partition on flash and mount it:
  // run through all partitions
  it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it) {

    part = esp_partition_get(it);

    // skip everything except for DATA-type partitions
    if (part && (part->type == ESP_PARTITION_TYPE_DATA))
      // label name match?
      if (!q_strcmp(argv[1], part->label)) {

        int tmp;

        // We have found the partition user wants to mount.
        // reassign 1st arg to real partition name (the case when user enters shortened label name)
        argv[1] = (char *)part->label;
        if (mp == mp0)
          sprintf(mp0, "/%s", argv[1]);

        // check if selected mount point is not used
        if ((tmp = files_mountpoint_by_path(mp, false)) >= 0) {
          q_printf("%% <e>Mount point \"%s\" is already used by partition \"%s\"</>\r\n", mp, mountpoints[tmp].label);
          goto mount_failed;
        }



        // Mount/Format depending on FS type
        switch (part->subtype) {
#if WITH_FAT
          // Mount FAT partition
          case ESP_PARTITION_SUBTYPE_DATA_FAT:
            esp_vfs_fat_mount_config_t conf = { 0 };

            conf.format_if_mount_failed = true;
            conf.max_files = 2;
            conf.allocation_unit_size = CONFIG_WL_SECTOR_SIZE;

            if ((err = esp_vfs_fat_spiflash_mount_rw_wl(mp, part->label, &conf, &mountpoints[i].wl_handle)) != ESP_OK)
              goto mount_failed;
            goto finalize_mount;
#endif
#if WITH_SPIFFS
          // Mount SPIFFS partition
          case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:
            if (esp_spiffs_mounted(part->label)) {
              q_printf("%% <e>Partition \"%s\" is already mounted</>\r\n", part->label);
              goto mount_failed;
            }
            esp_vfs_spiffs_conf_t conf2 = { 0 };

            conf2.base_path = mp;
            conf2.partition_label = part->label;
            conf2.max_files = 2;
            conf2.format_if_mount_failed = true;


            if ((err = esp_vfs_spiffs_register(&conf2)) != ESP_OK)
              goto mount_failed;
            goto finalize_mount;
#endif
#if WITH_LITTLEFS
          // Mount LittleFS partition
          case ESP_PARTITION_SUBTYPE_DATA_LITTLEFS:

            if (esp_littlefs_mounted(part->label)) {
              q_printf("%% <e>Partition \"%s\" is already mounted</>\r\n", part->label);
              goto mount_failed;
            }
            esp_vfs_littlefs_conf_t conf1 = { 0 };

            conf1.base_path = mp;
            conf1.partition_label = part->label;
            conf1.format_if_mount_failed = true;
            conf1.grow_on_mount = true;

            if ((err = esp_vfs_littlefs_register(&conf1)) != ESP_OK)
              goto mount_failed;
            goto finalize_mount;
#endif
          default:
            q_print("% <e>Unsupported file system</>\r\n");
            goto mount_failed;
        }
      }
    it = esp_partition_next(it);
  }

  // Matching partition was not found
  q_printf("%% <e>Partition label \"%s\" is not found</>\r\n", argv[1]);

mount_failed:
  q_printf("%% <e>Mount partition \"%s\" failed (error: %d)</>\r\n", argv[1], err);
#if WITH_FAT
  if (i >= 0)
    mountpoints[i].wl_handle = WL_INVALID_HANDLE;
#endif
  if (it)
    esp_partition_iterator_release(it);
  return 0;

finalize_mount:
  // 'part', 'i', 'mp' are valid pointers/inidicies
  if (it)
    esp_partition_iterator_release(it);

  if ((mountpoints[i].mp = q_strdup(mp, MEM_PATH)) == NULL)
    q_print(Failed);
  else {
    mountpoints[i].type = part->subtype;
    strcpy(mountpoints[i].label, part->label);

    HELP(q_printf("%% %s on partition \"%s\" is mounted under \"%s\"\r\n", files_subtype2text(part->subtype), part->label, mp));
  }
  return 0;
}

// "mount"
// Without arguments display currently mounted filesystems and partition table
//

static int cmd_files_mount0(int argc, char **argv) {

  int usable = 0, i;
  bool mountable;
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);

  if (!it) {
    q_print("% <e>Can not read partition table</>\r\n");
    return 0;
  }

  q_print("<r>% Disk partition |M|File system| Size on |    Mounted on    |Capacity |  Free   \r\n"
          "%    label       |?|   type    |  flash  |                  |  total  |  space  </>\r\n");
  q_print("% ---------------+-+-----------+---------+------------------+---------+---------\r\n");
  while (it) {
    const esp_partition_t *part = esp_partition_get(it);
    if (part && (part->type == ESP_PARTITION_TYPE_DATA)) {

      if (part->subtype == ESP_PARTITION_SUBTYPE_DATA_FAT || part->subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS || part->subtype == ESP_PARTITION_SUBTYPE_DATA_LITTLEFS) {
        usable++;
        mountable = true;
      } else {
        if (part->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS)
          usable++;
        mountable = false;
      }

#if WITH_COLOR
      if (mountable)
        q_print("<i>");
#endif
      //"label" "fs type" "partition size"
      // TODO: UTF8 mark sign
      q_printf("%%%16s|%s|%s| %6luK | ", 
                  part->label, 
                  mountable ? "+" 
                            : (ESP_PARTITION_SUBTYPE_DATA_NVS == part->subtype ? "*"
                                                                               : " "),
                  files_subtype2text(part->subtype),
                  part->size / 1024);

      if ((i = files_mountpoint_by_label(part->label)) >= 0)
        // "mountpoint" "total fs size" "available fs size"
        q_printf("%16s | %6uK | %6uK\r\n", mountpoints[i].mp, files_space_total(i) / 1024, files_space_free(i) / 1024);
      else
        q_print("                 |         |\r\n");
#if WITH_COLOR
      if (mountable)
        q_print("</>");
#endif
    }
    it = esp_partition_next(it);
  }
#if WITH_SD
  // display mounted SD cards (SD over SPI)
  //
  for (i = 0; i < MOUNTPOINTS_NUM; i++) {
    if (files_mountpoint_is_sdspi(i)) {
      sdmmc_card_t *card = (sdmmc_card_t *)mountpoints[i].gpp;
      if (card) {
        q_printf("%% %s: <i>%9s|+|%s| %6uM | ",sd_type(i), mountpoints[i].label, files_subtype2text(mountpoints[i].type), sd_capacity_mb(i));
        // TODO: check if it works
        q_printf("%16s | %6uK | %6uK</>\r\n", mountpoints[i].mp, files_space_total(i) / 1024, files_space_free(i) / 1024);
        usable++;
      }
    }
  }
#endif  

  q_print("%\r\n");
  if (!usable)
    q_print("% <e>No usable partitions were found. Use (Tools->Partition Scheme) in Arduino IDE</>\r\n");
  else 
    HELP(q_printf("%% <i>%u</> mountable partition%s found.\r\n", PPA(usable)));

  HELP(q_print("% Legend:\r\n"
               "%  <i>+</> : mountable partition\r\n"
               "%  <i>*</> : partition accessible via the \"nvs\" command\r\n"));

  if (it)
    esp_partition_iterator_release(it);
  return 0;
}


// "show mount"
// "show mount PATH"
//
static int cmd_show_mount(int argc, char **argv) {
    if ( argc < 3) // "show mount"
      return cmd_files_mount0(1, argv);
    return files_show_mountpoint(argv[2]) == 0 ? 0 : 2;
}


// "cd"
// "cd .."
// "cd /full/../path"
// "cd relative/path/../../pum/"
//
static int cmd_files_cd(int argc, char **argv) {
  bool ret = false;

  if (argc < 2)
    ret = files_cd(NULL);
  else if (argc > 2)
    q_print("% <e>Please, use quotes (\"\") for paths with spaces</>\r\n");
  else
    ret = files_cd(argv[1]);
  return ret ? 0 : CMD_FAILED;

}

// "ls [PATH]"
// Directory listing for current working directory or PATH if specified
//
static int ls_show_dir_size = true;

static int cmd_files_ls(int argc, char **argv) {
  char path[MAX_PATH + 16], *p;
  int plen;

  p = (argc > 1) ? files_full_path(argv[1], PROCESS_ASTERISK) : files_full_path(Cwd, IGNORE_ASTERISK);

  if ((plen = strlen(p)) == 0)
    return 0;

  if (plen > MAX_PATH)
    return 0;

  strcpy(path, p);

  // if it is Cwd then it MUST end with "/" so we dont touch it
  // if it is full_path then it MAY or MAY NOT end with "/" but full_path is writeable and expandable
  if (path[plen - 1] != '\\' && path[plen - 1] != '/') {
    path[plen++] = '/';
    path[plen] = '\0';
  }

  // "ls /" -  root directory listing,
  if (files_path_is_root(path)) {
    bool found = false;
    for (int i = 0; i < MOUNTPOINTS_NUM; i++)
      if (mountpoints[i].mp) {
        if (!found) {
          q_print("%-- USED --        *  Mounted on\r\n");
          found = true;
        }
        
        q_printf("%% <b>%9u</>       MP  [<i>%s</>]\r\n", files_space_used(i), mountpoints[i].mp);
        
      }
    if (!found)
      q_printf("%% <i>Root (\"%s\") directory is empty</>: no fileystems mounted\r\n%% Use command \"mount\" to list & mount available partitions\r\n", path);
    return 0;
  }

  // real directory listing
  if (!files_path_exist_dir(path))
    q_printf("%% <e>Path \"%s\" does not exist</>\r\n", path);
  else {

    // TODO: use scandir() with alphasort
    unsigned int total_f = 0, total_d = 0, total_fsize = 0;
    DIR *dir;

    if ((dir = opendir(path)) != NULL) {
      struct dirent *ent;

      q_print("%    Size        Modified          *  Name\r\n"
              "%               -- level up --    <f> [<i>..</>]\r\n");
      while ((ent = readdir(dir)) != NULL) {

        struct stat st;
        char path0[MAX_PATH + 16] = { 0 };

        if (strlen(ent->d_name) + 1 + plen > MAX_PATH) {
          q_print("% <e>Path is too long</>\r\n");
          continue;
        }

        // d_name entries are simply file/directory names without path so
        // we need to prepend a valid path to d_name
        strcpy(path0, path);
        strcat(path0, ent->d_name);

        if (0 == stat(path0, &st)) {
          if (ent->d_type == DT_DIR) {
            unsigned int dir_size = ls_show_dir_size ? files_size(path0) : 0;
            total_d++;
            total_fsize += dir_size;
            
            q_printf("%% %9u  %s  <f>  [<i>%s</>]\r\n", dir_size, files_time2text(st.st_mtime), ent->d_name);
            
          } else {
            total_f++;
            total_fsize += st.st_size;

            q_printf("%% %9u  %s     <g>%s</>\r\n", (unsigned int)st.st_size, files_time2text(st.st_mtime), ent->d_name);

          }
        } else
          q_printf("<e>stat() : failed %d, name %s</>\r\n", errno, path0);
      }
      closedir(dir);
    }
    q_printf("%%\r\n%% <i>%u</> director%s, <i>%u</> file%s, <i>%u</> byte%s\r\n",
             total_d, total_d == 1 ? "y" : "ies",
             PPA(total_f),
             PPA(total_fsize));
  }
  return 0;
}

// "rm PATH1 [PATH2 ... PATHn]"
// removes file or directory with its content (recursively)
//
static int cmd_files_rm(int argc, char **argv) {

  if (argc < 2) return CMD_MISSING_ARG;
  if (argc > 2) HELP(q_print(MultipleEntries));

  int i, num;
  for (i = 1, num = 0; i < argc; i++) {
    files_asterisk2spaces(argv[i]);
    num += files_remove(argv[i], DIR_RECURSION_DEPTH);
  }
  if (num)
    q_printf("%% <i>%d</> files/directories were deleted\r\n", num);
  else
    HELP(q_print("% No changes to the filesystem were made\r\n"));

  // change to the maintpoint dir if path we had as CWD does not exist anymore.
  // TODO: may be it is better to change up until existing directory is reached?
  if (!files_path_exist_dir(files_get_cwd()))
#if 0  
    espshell_exec("cd .."); // "go to the first existing dir" strategy
#else
    cmd_files_cd(1, NULL);  // "go to the mountpoint" startegy
#endif

    return 0;
}

// "write FILENAME [TEXT]"
// "append FILENAME [TEXT]"
//
// Write/append TEXT to file FILENAME. For the "write" command file is created if does not exist
// while "append" requires file to be created before. If TEXT is omitted then single \n byte is
// written
//
static int cmd_files_write(int argc, char **argv) {

  int fd, size = 1;
  char empty[] = { '\n', '\0' };
  char *path, *out = empty;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (argc > 2)
    size = userinput_join(argc, argv, 2, &out); // join arguments starting from argv[2], allocate buffer and copy joined data there

  unsigned flags = O_CREAT | O_WRONLY;

  // are we running as "write" or as "append" command?
  if (!q_strcmp(argv[0], "append"))
    flags |= O_APPEND;
  else
    flags |= O_TRUNC;

  if (size > 0)
    // create path components (if any)
    if (files_create_dirs(argv[1], PATH_HAS_FILENAME) >= 0) {

      // files_create_dirs() destroys /path/ as it is static buffer of files_full_path
      // instead of q_strdup just reevaluate it
      path = files_full_path(argv[1], PROCESS_ASTERISK);

      // ceate file and write TEXT
      if ((fd = open(path, flags)) > 0) {
        size = write(fd, out, size);
        if (size < 0)
          q_printf("%% <e>Write to file \"%s\" has failed, errno is %d</>\r\n", path, errno);
        else
          HELP(q_printf("%% <i>%u</> bytes written to <g>%s</>\r\n", size, path));
        close(fd);
        goto free_and_exit;
      }
    }
  HELP(q_print("%% <e>Failed to create file or path component</>\r\n"));
free_and_exit:
  if (out && (out != empty))
    q_free(out);

  return 0;
}

// "insert FILENAME LINE_NUMBER [TEXT]"
// "delete FILENAME LINE_NUMBER [COUNT]"
//
// insert TEXT before line number LINE_NUMBER
// delete lines LINE_NUMBER..LINE_NUMBER+COUNT
static int cmd_files_insdel(int argc, char **argv) {

  char *path, *upath = NULL, *text = NULL, empty[] = { '\n', '\0' };
  FILE *f = NULL, *t = NULL;
  unsigned char *p = NULL;
  unsigned int plen, tlen = 0, cline = 0, line;
  bool insert = true;  // default action is insert
  int count = 1;

  if (argc < 3)
    return CMD_MISSING_ARG;

  // insert or delete?
  insert = q_strcmp(argv[0], "delete");

  if ((line = q_atol(argv[2], (unsigned int)(-1))) == (unsigned int)(-1)) {
    HELP(q_printf("%% Line number expected instead of \"%s\"\r\n", argv[2]));
    return 2;
  }

  if (!files_path_exist_file((path = files_full_path(argv[1], PROCESS_ASTERISK)))) {
    HELP(q_printf("%% <e>Path \"%s\" does not exist</>\r\n", path));  //TODO: Path does not exist is a common string.
    return 1;
  }

  if ((f = fopen(path, "rb")) == NULL) {
    HELP(q_printf("%% <e>File \"%s\" does exist but failed to open</>\r\n", path));
    return 0;
  }

  // path is the files_full_path's buffer which has some extra bytes beyound MAX_PATH boundary which are
  // safe to use.
  strcat(path, "~");
  upath = q_strdup(path, MEM_PATH);
  if (!upath)
    goto free_memory_and_return;

  int tmp = strlen(path);
  path[tmp - 1] = '\0';  // remove "~""

  if ((t = fopen(upath, "wb")) == NULL) {
    q_printf("%% <e>Failed to create temporary file \"<g>%s\"</>\r\n", upath);
    goto free_memory_and_return;
  }


  if (insert) {
    if (argc > 3) {
      tlen = userinput_join(argc, argv, 3, &text);
      if (!tlen)
        goto free_memory_and_return;
    } else {
      tlen = 1;
      text = empty;
    }
  } else
    count = q_atol(argv[3], 1);  //TODO: check if argc > 3

  while (!feof(f)) {
    int r;
    if ((r = files_getline((char **)&p, &plen, f)) >= 0) {
      // end of file reached?
      if ((r == 0) && feof(f)) break;
      // current line is what user looking for?
      if (++cline == line) {
        if (!insert) {
          HELP(q_printf("%% Line %u deleted\r\n", line));
          // line range is processed?
          if (--count > 0)
            line++;
          continue;
        }
        fwrite(text, 1, tlen, t);
        if (text != empty) // compare pointers, not strings!
          fwrite("\n", 1, 1, t);  // Add \n only if it was not empty string
        HELP(q_printf("%% Line %u inserted\r\n", line));
      }
      fwrite(p, 1, r, t);
      fwrite("\n", 1, 1, t);
    }
  }
  // have to close files so unlink() and rename() could do their job
  fclose(f);
  fclose(t);
  t = f = NULL;

  unlink(path);
  if (rename(upath, path) == 0) {
    q_printf("%% Failed to rename files. File saved as \"%s\", rename it\r\n", upath);
    q_free(upath);
    upath = NULL;
  }

free_memory_and_return:
  if (p) q_free(p);
  if (f) fclose(f);
  if (t) fclose(t);
  if (text && text != empty) q_free(text);
  if (upath) {
    unlink(upath);
    q_free(upath);
  }

  return 0;
}

// "mkdir PATH1 [PATH2 ... PATHn]"
// Create new directory PATH
//
static int cmd_files_mkdir(int argc, char **argv) {

  int i, failed = 0;
  if (argc < 2) return CMD_MISSING_ARG;

  if (argc > 2)
    HELP(q_print(MultipleEntries));


  for (i = 1; i < argc; i++) {
    files_strip_trailing_slash(argv[i]);
    if (argv[i][0] == '\0')
      return i;
    if (files_create_dirs(argv[i], PATH_HAS_ONLY_DIRS) < 0)
      failed++;
  }

  if (failed) {
    HELP(q_printf("%% <e>There were errors during directory creation. (%d fail%s)</>\r\n", PPA(failed)));
    return CMD_FAILED;
  }

  return 0;
}



// "touch PATH1 [PATH2 ... PATHn]"
// Create new files or update existing's timestamp
//
static int cmd_files_touch(int argc, char **argv) {

  int i, err = 0;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (argc > 2)
    HELP(q_print(MultipleEntries));

  for (i = 1; i < argc; i++) {
    if (files_touch(argv[i]) < 0)
      err++;
    else
      q_printf("%% Touched: \"%s\"\r\n",argv[i]);
  }

  if (err) {
    HELP(q_printf("%% <e>There were errors during the process. (%d error%s)</>\r\n", PPA(err)));
    return CMD_FAILED;
  }

  return 0;
}

// "format [LABEL]"
// Format partition (current partition or LABEL partition if specified)
// If LABEL is not given (argc < 2) then espshell attempts to derive
// label name from current working directory
//
static int cmd_files_format(int argc, char **argv) {

  int i;
  esp_err_t err = ESP_OK;
  const char *label;
  const esp_partition_t *part;
  char path0[32] = { 0 };
  const char *reset_dir = "/";

  // this will be eliminated at compile time if verything is ok
  if (sizeof(path0) < sizeof(part[0].label))
    abort();

  // find out partition name (label): it is either set as 1st argument of "format"
  // command, or must be derived from current working directory
  if (argc > 1)
    label = argv[1];
  else {
    const char *path;

    if ((path = files_get_cwd()) == NULL)
      return 0;

    if (files_path_is_root(path)) {
      q_print("% <e>Root partition can not be formatted, \"cd\" first</>\r\n");
      return 0;
    }

    // disable reverse lookup on this: we don't want wrong partition to be formatted
    if ((i = files_mountpoint_by_path(path, false)) < 0) {
      // normally happen when currently used partition is unmounted: reset CWD to root directory
      files_set_cwd("/");
      return 0;
    }
    label = mountpoints[i].label;
    reset_dir = mountpoints[i].mp;
  }

  // find partition user wants to format
  if ((part = files_partition_by_label(label)) == NULL) {
    q_printf("%% <e>Partition \"%s\" does not exist</>\r\n", label);
    return argc > 1 ? 1 : 0;
  }

  // handle shortened label names
  label = part->label;


  HELP(q_printf("%% Formatting partition \"%s\", file system type is \"%s\"\r\n", label, files_subtype2text(part->subtype)));

  switch (part->subtype) {
#if WITH_FAT
    case ESP_PARTITION_SUBTYPE_DATA_FAT:
      sprintf(path0, "/%s", label);
      // TODO: SDcard support
      err = esp_vfs_fat_spiflash_format_rw_wl(path0, label);
      break;
#endif
#if WITH_LITTLEFS
    case ESP_PARTITION_SUBTYPE_DATA_LITTLEFS:
      err = esp_littlefs_format(label);
      break;
#endif
#if WITH_SPIFFS
    case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:
      err = esp_spiffs_format(label);
      break;
#endif
    default:
      q_printf("%% <e>Unsupported filesystem type 0x%02x</>\r\n", part->subtype);
  }
  if (err != ESP_OK)
    q_printf("%% <e>There were errors during formatting (code: %u)</>\r\n", err);
  else
    q_print("% done\r\n");

  // update CWD if we were formatting current filesystem
  if (!files_path_exist_dir(files_get_cwd()))
    files_set_cwd(reset_dir);

  return 0;
}

// "mv FILENAME1 FILENAME2"
// "mv DIRNAME1 DIRNAME2"
// "mv FILENAME DIRNAME" --> "cp FILENAME DIRNAME/FILENAME", "rm FILENAME"
// Move/rename files or directories
//
static int cmd_files_mv(int argc, char **argv) {
  q_print("% Not implemented yet\r\n");
  // TODO: implement "mv" command
#if 0
  bool src_file, dst_file, same_fs;
  ...
  if (dst_file && !src_file) {
    q_print("% Can not move directory to a file\r\n");
    return CMD_FAILED;
  }

  // If both src and dst are on the same file system and we are moving
  // 1. dir -> dir
  // 2. file -> file
  // then we can use old good rename() here
  if ((src_file == dst_file) && same_fs) {
    rename(src, dst);
  } else {
    // Either different filesystems or src and dst types mismatch (file->dir)
    // Emulate mv via cp & rm
  }
    
#endif  
  return 0;
}

static int UNUSED file_cp_callback(const char *src, void *aux) {

  //char *dst = (char *)aux;
  return 0;
}

// "cp FILENAME1 FILENAME2"
// "cp FILENAME DIRNAME"
// "cp DIRNAME1 DIRNAME2"
//
// Copy files/directories
static int cmd_files_cp(int argc, char **argv) {

  if (argc < 3)
    return CMD_MISSING_ARG;

  char spath[MAX_PATH], dpath[MAX_PATH];
  strcpy(spath, files_full_path(argv[1], PROCESS_ASTERISK));
  strcpy(dpath, files_full_path(argv[2], PROCESS_ASTERISK));
  files_strip_trailing_slash(spath);
  files_strip_trailing_slash(dpath);

  // determine copy algorithm:
  // file to file, file to dir or dir to dir
  if (files_path_exist_file(spath)) {    // src is a file?
    if (!files_path_exist_dir(dpath)) {  // dst is a file?
                                         // file to file copy
file_to_file:
      unlink(dpath);                                   // remove destination file if it exists
      q_printf("%% Copy %s to %s\r\n", spath, dpath);  // report
      files_copy(spath, dpath);                        // copy src->dst (single file)

    } else {                                            // dst is a directory?
                                                        // file to directory copy
      strcat(dpath, "/");                               // add the filename (last component of the src path)
      strcat(dpath, files_path_last_component(spath));  // to destination path.
      goto file_to_file;                                // proceed with file to file algorithm
    }
  } else if (files_path_exist_dir(spath)) {  // src is a directory
    if (files_path_exist_dir(dpath)) {
      q_printf("%% copy dir to dir is not implemented yet\r\n");
#if 1
      strcat(dpath, "/");
      strcat(dpath, files_path_last_component(spath));
      if (mkdir(dpath, 0777) == 0) {

      } else
        q_printf("%% Failed to create directory \"%s\"\r\n", dpath);
#endif
      // TODO: Copy directory to directory

    } else {
      q_printf("%% Path \"%s\" is not a directory\r\n", dpath);
      return 2;
    }
  } else {
    q_printf("%% Path \"%s\" does not exist\r\n", spath);
    return 1;
  }
  return 0;
}

// "cat [-n|-b] PATH [START [COUNT]] [uart NUM]
// cat /wwwroot/text_file.txt
// cat /wwwroot/text_file.txt 10
// cat /wwwroot/text_file.txt 10 10
// cat /wwwroot/text_file.txt uart 1
// cat /wwwroot/text_file.txt 10 10 usb 0
//
// Display a text file content
//
static int cmd_files_cat(int argc, char **argv) {

  int binary = 0, numbers = 0;
  char *path;
  int i = 1;
  unsigned int line = (unsigned int)(-1), count = (unsigned int)(-1);
  unsigned char device = (unsigned char)(-1);

  if (argc < 2) return CMD_MISSING_ARG;

  // -b & -n options; -b here is for "binary", not for "number only non-blank lines"
  if (!strcmp("-b", argv[i]))
    binary = ++i;
  else if (!strcmp("-n", argv[i]))
    numbers = ++i;

  if (i >= argc)
    return CMD_MISSING_ARG;

  if (!files_path_exist_file((path = files_full_path(argv[i], PROCESS_ASTERISK)))) {
    q_printf("%% File not found:\"<e>%s</>\"\r\n", path);
    return 1;
  }
  i++;

  // pickup other arguments.
  // these are: one or two numbers (start & count) and possibly an uart interface number (a number as well)
  while (i < argc) {
    if (isnum(argv[i]) || ishex(argv[i])) {
      if (line == (unsigned int)(-1))
        line = q_atol(argv[i], 0);
      else if (count == (unsigned int)(-1))
        count = q_atol(argv[i], 0xffffffff);
      else {
        HELP(q_print("% Unexpected 3rd numeric argument\r\n"));
        return i;
      }
    } else
      // "uart" keyword? must be a valid uart interface number in the next argument then
      if (!q_strcmp(argv[i], "uart")) {
        if (i + 1 >= argc) {
          HELP(q_print("% <e>UART number is missing</>\r\n"));
          return i;
        }
        i++;
        if (!isnum(argv[i])) {
          HELP(q_print("% <e>Numeric value (UART number) is expected</>\r\n"));
          return i;
        }

        if (!uart_isup((device = atol(argv[i])))) {
          q_printf("%% <e>UART%d is not initialized</>\r\n", device);  // TODO: common string
          HELP(q_printf("%% Configure it by command \"uart %d\"</>\r\n", device));
          return 0;
        }
      } else
        // unexpected keyword
        return i;

    // to the next keyword
    i++;
  }

  // line number (or file offset) was omitted?
  if (line == (unsigned int)(-1))
    line = 0;

  if (binary)
    files_cat_binary(path, line, count, device);
  else
    files_cat_text(path, line, count, device, numbers);

  return 0;
}
#endif  //WITH_FS
#endif // #if COMPILING_ESPSHELL

