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

// -- NVS editor and viewer --
// This module adds support for "nvs list" and "nvs set" commands which let user to view/modify NVS content
// On ESP32 NVS is emulated by using a dedicated partition on the main flash chip. Information there is stored
// in form of key=value pairs, however there is also such thing as a namespace. It is a kind of a directory in a
// filesystem which allows us to have same key=value pairs under different namespaces


#if COMPILING_ESPSHELL


#include <nvs_flash.h>
#include <nvs.h>

#define DEF_NVS_PARTITION "nvs"



// Initialize NVS library.
// TODO: will this interfere with user sketch?
// TODO: do not erase data on error! Let user to backup all the data and manually erase it
//
static void __attribute__((constructor)) _nv_storage_init() {

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      err = nvs_flash_init();
  }
  if (err != ESP_OK)
    // q_printf() will not work if called too early
    printf("%% NV flash init failed, hostid and WiFi driver settings are lost\r\n");
}


// Save some vital configuration parameters to the NV storage:
// hostid and the timezone information
//
static bool nv_save_config(const char *nspace) {

    nvs_handle_t handle;
    esp_err_t err;

    if (!nspace)
      nspace = "espshell";

    // Open NVS storage
    if ((err = nvs_open(nspace, NVS_READWRITE, &handle)) != ESP_OK) {
        q_printf("%% Error opening NVS, namespace \"%s\": code %s",nspace, esp_err_to_name(err));
        return false;
    }

    // Write values
    nvs_set_str(handle, "hostid", PromptID);
    nvs_set_str(handle, "tz", Time.zone);
      if ((err = nvs_commit(handle)) != ESP_OK)
        q_printf("%% NVS commit failed: %s", esp_err_to_name(err));

    nvs_close(handle);
    return err == ESP_OK;
}

// Load some espshell parameters from the NV storage
//
static bool nv_load_config(const char *nspace) {

    nvs_handle_t handle;
    esp_err_t err;
    

    if (!nspace)
      nspace = "espshell";

    if ((err = nvs_open(nspace, NVS_READONLY, &handle)) != ESP_OK) {
        q_printf("%% Error opening NVS: %s", esp_err_to_name(err));
        return false;
    }

    // Read hostname and a timezone
    size_t length = sizeof(PromptID);
    nvs_get_str(handle, "hostid", PromptID, &length);

    length = sizeof(Time.zone);
    nvs_get_str(handle, "tz", Time.zone, &length);
    if (Time.zone[0])
      time_apply_zone();
    nvs_close(handle);
    return true;
}

#if WITH_NVS

// nvs cwd. Contains either a namespace name or an empty string
static char Nv_cwd[NVS_NS_NAME_MAX_SIZE];

// Temporary space to store a list of namespaces found by nv_list_namespaces()
// along with number of entries in every namespace. Allocated and freed within nv_list_namespaces
//
static struct nvsnamespace {
  struct nvsnamespace *next;                        // pointer to the next namespace or NULL
  char                 name[NVS_NS_NAME_MAX_SIZE];  // name space
  int                  count;                       // number of key/value pairs in given namespace
} *nvs_namespaces = NULL;


// Human-readable element type
// 
static __attribute__((const)) const char *nv_type_str(nvs_type_t t) {
  switch(t) {
    case NVS_TYPE_U8:   return "uint8";
    case NVS_TYPE_I8:   return "int8";
    case NVS_TYPE_U16:  return "uint16";
    case NVS_TYPE_I16:  return "int16";
    case NVS_TYPE_U32:  return "uint32";
    case NVS_TYPE_I32:  return "int32";
    case NVS_TYPE_U64:  return "uint64";
    case NVS_TYPE_I64:  return "int64";
    case NVS_TYPE_STR:  return "char*";
    case NVS_TYPE_BLOB: return "char[]";
    default:
  };
  return "undef!";
}

// Get current name space or "/"
//
static const char *nv_get_cwd() {
  return Nv_cwd[0] ? Nv_cwd : "/";
}

// Check if current namespace == "/"
//
static bool nv_cwd_is_root() {
  const char *cwd = nv_get_cwd();
  return cwd[0] == '/' && cwd[1] == '\0';
}

// Set current working directory, contains current namespace or NULL
// It is not thread safe in the sence that two tasks will share the same cwd buffer
//
static char *nv_set_cwd(const char *cwd) {

  static char prompt[sizeof(PROMPT_NVS) - 2 + NVS_NS_NAME_MAX_SIZE + 1 + 1];

  if (cwd && *cwd) {
    if (*cwd == '/')
      cwd++;
    if (*cwd == '\0')
      Nv_cwd[0] = '\0';
    else
      strlcpy(Nv_cwd, cwd, sizeof(Nv_cwd));
  } else
    Nv_cwd[0] = '\0';

  snprintf(prompt, sizeof(prompt), PROMPT_NVS, Nv_cwd);
  prompt_set(prompt);

  return Nv_cwd;
}




// Add string /name/ to the global list of strings but only if that string is unique for the list
// Returns /true/ if string was added and /false/ if it was not added (not unique or out of memory issues)
//
static bool add_unique(const char *name) {
  if (name && *name) {
    struct nvsnamespace *n = nvs_namespaces;
    while( n ) {
      if (!strcmp(n->name, name)) {
        n->count++;
        return false;
      }
      n = n->next;
    }
    
    if ((n = (struct nvsnamespace *)q_malloc(sizeof(struct nvsnamespace ), MEM_TMP)) != NULL) {

      n->count = 1;
      strlcpy(n->name, name, sizeof(n->name));
      // Link a new head
      // TODO: must be atomic operation
      n->next = nvs_namespaces;
      nvs_namespaces = n;
      return true;
    }
  }
  return false;
}

// List all namespaces available
//
//
static void nv_list_namespaces() {

  int count = 0;
  nvs_iterator_t it;
  const char *partition = context_get_ptr(const char);
  if (!partition)
    partition = DEF_NVS_PARTITION;
  
  if (nvs_entry_find(partition, NULL, NVS_TYPE_ANY, &it) == ESP_OK) {
    do {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (add_unique(info.namespace_name))
          count++;
    } while (nvs_entry_next(&it) == ESP_OK);
    nvs_release_iterator(it); // Release the iterator

    q_printf("%% NVS has <i>%d</> name spaces:\r\n", count);

    while (nvs_namespaces) {

      // TODO: unlink operation must be atomic
      struct nvsnamespace *n = nvs_namespaces;
      nvs_namespaces = nvs_namespaces->next;

      q_printf("%%  Namespace \"%s\" : %d keys\r\n", n->name, n->count);
      q_free(n);
    }
  } else
    q_printf("%% No NVS entries found on partition \"%s\", NVS looks empty\r\n", partition);
}




// List key/values for the /namespace/
// BLOBS are not displayed, strings are truncated to 42 characters
//
static void nv_list_keys(const char *namespace) {

  esp_err_t err;
  nvs_handle_t handle;
  nvs_iterator_t it;
  size_t length;
  const char *partition;

  uint64_t u64; int64_t i64;
  uint32_t u32; int32_t i32;
  uint16_t u16; int16_t i16;
  uint8_t u8;   int8_t i8;
  char val_str[256];

  if (namespace && *namespace) {

    if (NULL == (partition = context_get_ptr(const char)))
      partition = DEF_NVS_PARTITION;

    if ((err = nvs_open_from_partition(partition,namespace, NVS_READONLY, &handle)) == ESP_OK) {
      if (nvs_entry_find_in_handle(handle, NVS_TYPE_ANY, &it) == ESP_OK) {

        int count = 0;

        q_print("%<r> # |     Key name     |  Type  | Value                                         </>\r\n"
                   "% --+------------------+--------+-----------------------------------------------\r\n");

        do {
          nvs_entry_info_t info;

          count++;
          nvs_entry_info(it, &info);
          q_printf("%%%3d| %-16.16s | %-6.6s | ", count, info.key, nv_type_str(info.type));

          length = sizeof(val_str);

          switch (info.type) {
            case NVS_TYPE_U8:   nvs_get_u8(handle, info.key, &u8); q_printf("%u\r\n",u8); break;
            case NVS_TYPE_I8:   nvs_get_i8(handle, info.key, &i8); q_printf("%d\r\n",i8); break;
            case NVS_TYPE_U16:  nvs_get_u16(handle, info.key, &u16); q_printf("%u\r\n",u16); break;
            case NVS_TYPE_I16:  nvs_get_i16(handle, info.key, &i16); q_printf("%d\r\n",i16); break;
            case NVS_TYPE_U32:  nvs_get_u32(handle, info.key, &u32); q_printf("%lu\r\n",u32); break;
            case NVS_TYPE_I32:  nvs_get_i32(handle, info.key, &i32); q_printf("%ld\r\n",i32); break;
            case NVS_TYPE_U64:  nvs_get_u64(handle, info.key, &u64); q_printf("%llu\r\n",u64); break;
            case NVS_TYPE_I64:  nvs_get_i64(handle, info.key, &i64); q_printf("%lld\r\n",i64); break;

            case NVS_TYPE_STR:  nvs_get_str(handle, info.key, val_str, &length);
                                q_printf("%-42.42s%s\r\n",val_str[0] ? val_str : "<empty>", length > 42 ? "..." : ""); break;

            case NVS_TYPE_BLOB: nvs_get_blob(handle,info.key, NULL, &length);
                                q_printf("<A binary blob, not displayed>, %u bytes\r\n", length); break;

            default:            q_print("<Unknown data>\r\n"); break;
          };

        } while (nvs_entry_next(&it) == ESP_OK);
        nvs_release_iterator(it); // Release the iterator
        
        q_print("% --+------------------+--------+-----------------------------------------------\r\n");
        q_printf("%% Total: %d record%s\r\n", PPA(count));
      } else
        q_printf("%% Namespace \"%s\" (partition: \"%s\") is empty\r\n",namespace, partition);
      nvs_close(handle);
    } else
      q_printf("%% Error opening NVS partition \"%s\"\r\n",partition);
  }
}





// Switch to NVS editor.
// /Context/ is not used here, but it is used to store CWD pointer (i.e. "/" or "NAMESPACE")
//
static int cmd_nvs_if(int argc, char **argv) {
  static char partition[32]; //TODO: No magic numbers
  strlcpy( partition, 
          (argc < 2) ? "nvs" : argv[1],
          sizeof(partition));
  change_command_directory(0, KEYWORDS(nvs), PROMPT, "NVS");
  context_set(partition);  
  nv_set_cwd(NULL);        // update prompt and set cwd to "/"
  return 0;
}

// cd /|..|NAMESPACE|/NAMESPACE
//
static int cmd_nvs_cd(int argc, char **argv) {
  if (argc < 2)
    return CMD_MISSING_ARG;
  const char *p = argv[1];
  // Simply skip all ../.././//./ stuff: we don't have nested namespaces
  // Thus "cd .." and "cd /" will do the same job. Only "cd ." will do not what is expected but 
  // we just ignore it: no point in doing "cd ." - this command does nothing
  //
  while (*p == '/' || *p == '.')
    p++;
  if (strlen(p) > sizeof(nvs_namespaces->name)) {
    q_print("% Path is too long\r\n");
    return CMD_FAILED;
  }
  nv_set_cwd(*p ? p : NULL);
  return 0;
}

// ls
// ls NAMESPACE
// ls /NAMESPACE
//
static int cmd_nvs_ls(int argc, char **argv) {

  bool root;
  const char *namespace;

  if (argc > 1) {
    char *p = argv[1];
    while (*p == '/' || *p == '.')
      p++;
    root = (*p == '\0');
    namespace = p;
  } else {
    root = nv_cwd_is_root();
    namespace = nv_get_cwd();
  }

  if (root)
    nv_list_namespaces();
  else
    nv_list_keys(namespace);

  return 0;
}

//
//
//
static int cmd_nvs_rm(int argc, char **argv) {
  return 0;
}

//
//
//
static int cmd_nvs_set(int argc, char **argv) {
  return 0;
}

static int cmd_nvs_dump(int argc, char **argv) {

  nvs_handle_t handle;
  size_t length;
  const char *partition;
  const char *namespace = nv_get_cwd();
  unsigned char *mem;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (*namespace == '/')
    return CMD_FAILED;

  if (NULL == (partition = context_get_ptr(const char)))
      partition = DEF_NVS_PARTITION;

  if (nvs_open_from_partition(partition,namespace, NVS_READONLY, &handle) == ESP_OK) {
    if (ESP_OK == nvs_get_blob(handle, argv[1], NULL, &length)) {
      if ((mem = (unsigned char *)q_malloc(length, MEM_TMP)) != NULL) {
        if (ESP_OK == nvs_get_blob(handle,argv[1], mem, &length))
          q_printhex(mem, length);
        q_free(mem);
      }
    } else
      q_printf("% Key \"%s\" is not a blob, use \"ls\" to see its value\r\n", argv[1]);
    nvs_close(handle);
  }
  return 0;
}

//
//
//
static int cmd_nvs_new(int argc, char **argv) {
  return 0;
}

#if WITH_FS
//
//
//
static int cmd_nvs_export(int argc, char **argv) {
  return 0;
}

//
//
//
static int cmd_nvs_import(int argc, char **argv) {
  return 0;
}
#endif // #if WITH_FS
#endif // #if WITH_NVS
#endif // #if COMPILING_ESPSHELL
