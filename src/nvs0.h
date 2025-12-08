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
// This module adds support for "ls", "set", "new" and "dump" commands which let user to view/modify NVS content
// On ESP32 NVS is emulated by using a dedicated partition on the main flash chip. Information there is stored
// in form of key=value pairs, however there is also such thing as a namespace. It is a kind of a directory in a
// filesystem which allows us to have same key=value pairs under different namespaces
//
// Command "nvs" has one optional argument: the NVS partition name (which is "nvs" by default)


#if COMPILING_ESPSHELL


#include <nvs_flash.h>
#include <nvs.h>

#define DEF_NVS_PARTITION "nvs"
#define DEF_ESPSHELL_NAMESPACE "espshell"

// Initialize NVS library.
// Upon startup it is called from a constructor function _nvs_init() and by that time q_print is not yet available
// so we use printf for reporting (/early/ == /true/)
// TODO: do not erase data on error! Let user to backup all the data and manually erase it
//
static void nv_init(bool early) {

  esp_err_t err;
  const char *partition;

  if (early) {
    partition = DEF_NVS_PARTITION;
  } else {
    partition = context_get_ptr(const char);
    if (!partition)
      partition = DEF_NVS_PARTITION;
  }

  err = nvs_flash_init_partition(partition);
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase_partition(partition);
      err = nvs_flash_init_partition(partition);
  }

  if (err != ESP_OK) {
    // q_printf() will not work if called too early
    const char *msg = "%% NV flash init failed, hostid and WiFi driver settings are lost\r\n";
    if (early)
      printf(msg);
    else
      q_print(msg);
  }
}

static void __attribute__((constructor)) _nv_storage_init() {
  nv_init( true );
}


// Save some vital configuration parameters to the NV storage:
// hostid and the timezone information
//
static bool nv_save_config() {

    nvs_handle_t handle;
    esp_err_t err;

    // Open NVS storage
    if ((err = nvs_open(DEF_ESPSHELL_NAMESPACE, NVS_READWRITE, &handle)) != ESP_OK) {
        q_printf("%% Error during configuration saving (err=%08x)\r\n",err);
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
static bool nv_load_config() {

    nvs_handle_t handle;
    esp_err_t err;
    

    if ((err = nvs_open(DEF_ESPSHELL_NAMESPACE, NVS_READONLY, &handle)) != ESP_OK) {
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
// NOTE: it is not a _Thread_local variable, so multiple tasks accessing NVS CWD can fall to
// undefined behaviour. Don't run multiple instances of the editor and you will be safe
//
static char Nv_cwd[NVS_NS_NAME_MAX_SIZE];

// Temporary space to store a list of namespaces found by nv_list_namespaces()
// along with number of entries in every namespace. Allocated and freed within nv_list_namespaces
//
struct nvsnamespace {
  struct nvsnamespace *next;                        // pointer to the next namespace or NULL
  char                 name[NVS_NS_NAME_MAX_SIZE];  // name space
  int                  count;                       // number of key/value pairs in given namespace
};

// Private functions

// Add string /name/ to the global list of strings but only if that string is unique for the list
// Returns /true/ if string was added and /false/ if it was not added (not unique or out of memory issues)
//
static bool add_unique( struct nvsnamespace **nvs_namespaces, const char *name) {
  if (likely(name && *name && nvs_namespaces)) {
    struct nvsnamespace *n = *nvs_namespaces;
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
      n->next = *nvs_namespaces;
      *nvs_namespaces = n;
      return true;
    }
  }
  return false;
}

// This function reads scalar C types and pointers and returns element size
// Examples of valid inputs:
// "char"
// "unsigned short int"
// "long long int"
// "char*" "char *"
// "char[123]", "char[]"
//
static size_t read_ctype(int argc, char **argv, int start, bool *is_str, bool *is_blob, bool *is_signed) {
  if ((start >= argc) ||
      !is_str || !is_blob || !is_signed)
    return 0;

  size_t size = 0;
  uint8_t ll = 0;   // how many times did we see "long" keyword

  *is_str = *is_blob = false;
  *is_signed = true;

  while (start < argc) {
    if (!q_strcmp(argv[start],"signed"))   *is_signed = true;     else
    if (!q_strcmp(argv[start],"unsigned")) *is_signed = false;    else
    if (!q_strcmp(argv[start],"char"))      size = 1;             else
    if (!q_strcmp(argv[start],"short"))     size = 2;             else
    if (!q_strcmp(argv[start],"int"))       size = size < 2 ? 4
                                                          : size; else
    if (!q_strcmp(argv[start],"long"))      size = 4 * (++ll);    else
    if (!q_strcmp("char[", argv[start]) ||
        argv[start][0] == '[' ||
        argv[start][0] == ']')             *is_blob = true;       else
    if (argv[start][0] == '*' ||
        !q_strcmp(argv[start],"char*"))    *is_str = true;
    start++;
  }

  return size;
}

_Static_assert((NVS_TYPE_U8 == 0x01) && (NVS_TYPE_I32 == 0x14), "Code review is required");

// Convert decoded C-type to NVS data type
//
static __attribute__((const)) nvs_type_t ct2nt(uint8_t size, bool is_str, bool is_blob, bool is_signed) {

  if (is_str)
    return NVS_TYPE_STR;
  if (is_blob)
    return NVS_TYPE_BLOB;

  if (size > 8)
    return NVS_TYPE_ANY; // Error

  return is_signed ? (0x10 | size) : size;
}




// Human-readable element type
// 
static __attribute__((const)) const char *nt2ct(nvs_type_t t) {
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

// public API functions


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



// List all namespaces available
// this one is used by "ls"
//
static void nv_list_namespaces() {

  int count = 0;
  nvs_iterator_t it;
  const char *partition = context_get_ptr(const char);
  if (!partition)
    partition = DEF_NVS_PARTITION;
  
  if (nvs_entry_find(partition, NULL, NVS_TYPE_ANY, &it) == ESP_OK) {
    struct nvsnamespace *nvs_namespaces = NULL;
    do {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (add_unique(&nvs_namespaces, info.namespace_name))
          count++;
    } while (nvs_entry_next(&it) == ESP_OK);
    nvs_release_iterator(it); // Release the iterator

    q_printf("%% NVS has <i>%d</> namespaces:\r\n", count);

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
// To view blobs and strings as is use command "dump"
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

        q_print("%<r> # |     Key name     |  Type  | Value (strings may be truncated. use \"dump\") </>\r\n"
                   "% --+------------------+--------+-----------------------------------------------\r\n");

        do {
          nvs_entry_info_t info;

          count++;
          nvs_entry_info(it, &info);
          q_printf("%%%3d| %-16.16s | %-6.6s | ", count, info.key, nt2ct(info.type));

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
      q_printf("%% Namespace \"%s\" (partition: \"%s\") is empty or does not exist\r\n",namespace, partition);
  }
}


static void nv_export_csv(FILE *fp, const char *namespace) {

  esp_err_t err;
  nvs_handle_t handle;
  nvs_iterator_t it;
  size_t length;
  const char *partition;

  uint64_t u64; int64_t i64;
  uint32_t u32; int32_t i32;
  uint16_t u16; int16_t i16;
  uint8_t u8;   int8_t i8;
  char *tmp = NULL;

  if (namespace && *namespace) {

    if (NULL == (partition = context_get_ptr(const char)))
      partition = DEF_NVS_PARTITION;

    if ((err = nvs_open_from_partition(partition,
                                      *namespace == '*' ? NULL : namespace,
                                      NVS_READONLY,
                                      &handle)) == ESP_OK) {

      if (nvs_entry_find_in_handle(handle, NVS_TYPE_ANY, &it) == ESP_OK) {

        int count = 0;

        fprintf(fp, "record_id,namespace,key_name,key_data_type,key_value\r\n");
        do {
          nvs_entry_info_t info;

          
          nvs_entry_info(it, &info);
          if (*namespace != '*' && q_strcmp(namespace, info.namespace_name))
            continue ;
          count++;
          fprintf(fp, "%d,%s,%s,%u,", count, namespace, info.key, info.type);

          switch (info.type) {
            case NVS_TYPE_U8:   nvs_get_u8(handle, info.key, &u8); fprintf(fp,"%u\r\n",u8); break;
            case NVS_TYPE_I8:   nvs_get_i8(handle, info.key, &i8); fprintf(fp,"%d\r\n",i8); break;
            case NVS_TYPE_U16:  nvs_get_u16(handle, info.key, &u16); fprintf(fp,"%u\r\n",u16); break;
            case NVS_TYPE_I16:  nvs_get_i16(handle, info.key, &i16); fprintf(fp,"%d\r\n",i16); break;
            case NVS_TYPE_U32:  nvs_get_u32(handle, info.key, &u32); fprintf(fp,"%lu\r\n",u32); break;
            case NVS_TYPE_I32:  nvs_get_i32(handle, info.key, &i32); fprintf(fp,"%ld\r\n",i32); break;
            case NVS_TYPE_U64:  nvs_get_u64(handle, info.key, &u64); fprintf(fp,"%llu\r\n",u64); break;
            case NVS_TYPE_I64:  nvs_get_i64(handle, info.key, &i64); fprintf(fp,"%lld\r\n",i64); break;

            case NVS_TYPE_STR:  length = 0;
                                if (nvs_get_str(handle, info.key, NULL, &length) == ESP_OK) {
                                  if ((tmp = (char *)q_malloc(length + 1, MEM_TMP)) != NULL) {
                                    if (ESP_OK == nvs_get_str(handle, info.key, tmp, &length))
                                      fprintf(fp,"\"%s\"\r\n", tmp);
                                    else
                                      fprintf(fp,"\"<invalid>\"\r\n");
                                    q_free(tmp);
                                    break;
                                  }
                                }
                                fprintf(fp,"\"<invalid>\"\r\n");
                                break;


            case NVS_TYPE_BLOB:  length = 0;
                                if (nvs_get_blob(handle, info.key, NULL, &length) == ESP_OK) {
                                  if ((tmp = (char *)q_malloc(length + 1, MEM_TMP)) != NULL) {
                                    if (ESP_OK == nvs_get_blob(handle, info.key, tmp, &length)) {
                                      for (size_t i = 0; i < length; i++) {
                                        fprintf(fp,"\\%02x", tmp[i]);
                                      }
                                      fprintf(fp,"\r\n");
                                    } else
                                      fprintf(fp,"\"<invalid>\"\r\n");
                                    q_free(tmp);
                                    break;
                                  }
                                }
                                fprintf(fp,"\"<invalid>\"\r\n");
                                break;

            default:            q_print("<Unknown data>\r\n"); break;
          };

        } while (nvs_entry_next(&it) == ESP_OK);
        nvs_release_iterator(it); // Release the iterator
      } else
        q_printf("%% Namespace \"%s\" (partition: \"%s\") is empty\r\n",namespace, partition);
      nvs_close(handle);
    } else
      q_printf("%% Namespace \"%s\" (partition: \"%s\") is empty or does not exist\r\n",namespace, partition);
  }
}



// Handlers


// Switch to NVS editor.
// /Context/ stores a pointer to the NVS partition name (default value is "nvs")
//
static int cmd_nvs_if(int argc, char **argv) {
  static char partition[32]; //TODO: No magic numbers
  strlcpy( partition, 
          (argc < 2) ? DEF_NVS_PARTITION : argv[1],
          sizeof(partition));
  // TODO: check if partition exists!
  change_command_directory(0, KEYWORDS(nvs), PROMPT, "NVS editor/viewer");
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
  if (strlen(p) >= NVS_NS_NAME_MAX_SIZE) {
    q_print("% Path is too long\r\n");
    return CMD_FAILED;
  }
  nv_set_cwd(*p ? p : NULL);
  return 0;
}

// ls
// ls NAMESPACE
// ls /NAMESPACE
// ls ../NAMESPACE etc
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

// Remove keys and namespaces
// rm .|* - remove current namespace or if executed in root - remove all namespaces
// rm ../test  - removes namespace
// rm ../ - removes entire partition
//
static int cmd_nvs_rm(int argc, char **argv) {
  
  //int ret = CMD_FAILED;
  //esp_err_t err;
  nvs_handle_t handle;
  const char *partition, *namespace;
  const char *p;
  //bool is_key = false;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (NULL == (partition = context_get_ptr(const char)))
    partition = DEF_NVS_PARTITION;

  namespace = nv_get_cwd();

  p = argv[1];

  // "rm /"
  // "rm ." in the root
  if ((*p == '/' && p[1] == '\0') ||
      (*namespace == '/' && (*p == '.' || *p == '*') && p[1] == '\0')) {
erase_all_namespaces_and_exit:    
    q_print("% Erase all namespaces and keys\r\n");
    nvs_flash_erase_partition(partition);
    nv_init( false );
    return 0;
  }

  // "rm .|*"
  if ((*p == '.' || *p == '*') && (*(p+1) == '\0'))
    p = namespace;
  else while(*p == '/' || *p == '.')
          p++;
  if (*p == '\0')
    goto erase_all_namespaces_and_exit;

  // Check if argv[1] is a key or a namespace: if CWD is "/" then argv[1] can't be a key
  // if argv[1] ahd .././../ in it - then it can't be a key
  if (*namespace == '/' ||  p != argv[1]) {
    if (nvs_open_from_partition(partition, p, NVS_READWRITE, &handle) == ESP_OK) {    
      nvs_erase_all(handle);
      nvs_commit(handle);
      nvs_close(handle);
      q_printf("%% All keys in the namespace \"%s\" were removed\r\n", p);
    } else
      q_printf("%% Can not open NVS partition \"%s\" (namespace: \"%s\")\r\n", partition, p);
  } else {
    if (nvs_open_from_partition(partition,namespace, NVS_READWRITE, &handle) == ESP_OK) {    
      if (ESP_OK == nvs_erase_key(handle, p)) {
        q_printf("%% Key \"%s\" has been erased (namespace: \"%s\", partition: \"%s\")\r\n", argv[1], namespace, partition);
        nvs_commit(handle);
      }
      nvs_close(handle);
      return 0;
    }
    q_printf("%% Can not open NVS partition \"%s\" (namespace: \"%s\")\r\n", partition, namespace);
    return CMD_FAILED;
  }

  return 0;
}

// set Name 10
// set Str "Some text"
// set Blob \11\22\33\44\55\66\aa\bb\cc\dd\ee\ff
static int cmd_nvs_set(int argc, char **argv) {

  esp_err_t err;
  nvs_handle_t handle;
  const char *partition, *namespace;

  if (argc < 3)
    return CMD_MISSING_ARG;

  if (NULL == (partition = context_get_ptr(const char)))
    partition = DEF_NVS_PARTITION;

  namespace = nv_get_cwd();

  if ((err = nvs_open_from_partition(partition, namespace, NVS_READWRITE, &handle)) == ESP_OK) {
    nvs_type_t type;
    if (ESP_OK == (err = nvs_find_key(handle, argv[1], &type))) {
      switch(type) {
        case NVS_TYPE_U8:   err = nvs_set_u8(handle,   argv[1], q_atol(argv[2], 0)); break; 
        case NVS_TYPE_I8:   err = nvs_set_i8(handle,   argv[1], q_atoi(argv[2], 0)); break; 
        case NVS_TYPE_U16:  err = nvs_set_u16(handle,  argv[1], q_atol(argv[2], 0)); break; 
        case NVS_TYPE_I16:  err = nvs_set_i16(handle,  argv[1], q_atoi(argv[2], 0)); break; 
        case NVS_TYPE_U32:  err = nvs_set_u32(handle,  argv[1], q_atol(argv[2], 0)); break; 
        case NVS_TYPE_I32:  err = nvs_set_i32(handle,  argv[1], q_atoi(argv[2], 0)); break; 
        case NVS_TYPE_U64:  err = nvs_set_u64(handle,  argv[1], q_atol(argv[2], 0)); break; 
        case NVS_TYPE_I64:  err = nvs_set_i64(handle,  argv[1], q_atoi(argv[2], 0)); break; 
        case NVS_TYPE_STR:   
        case NVS_TYPE_BLOB:
                            int siz;
                            char *text = NULL;
                            if ((siz = userinput_join(argc, argv, 2, &text)) >= 0) {
                              if (type == NVS_TYPE_STR) {
                                text[siz] = '\0'; // this buffer has extra bytes at the end which can be used to zero-terminate the string
                                err = nvs_set_str(handle,  argv[1], text);
                              } else if (siz)
                                err = nvs_set_blob(handle, argv[1], text, siz);
                              else {
                                q_print("% Blob must be at least 1 byte long\r\n");
                                err = ESP_FAIL;
                              }
                            }
                            if (text)
                              q_free(text);
                            break;
        default:            err = ESP_FAIL;
      };
    }

    if (err == ESP_OK)
      nvs_commit(handle);

    nvs_close(handle);
  }

  if (err != ESP_OK) {
    q_printf("%% Key %s does not exist (namespace \"%s\", partition \"%s\")\r\n", argv[1], namespace, partition);
    q_print("% No changes were made to the NVS\r\n");
    return CMD_FAILED;
  }

  return 0;
}

// dump str
// dump blob
//
static int cmd_nvs_dump(int argc, char **argv) {

  int ret = CMD_FAILED;
  esp_err_t err;
  nvs_handle_t handle;
  size_t length;
  const char *partition;
  const char *namespace;
  unsigned char *mem;
  bool is_str = false;

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (nv_cwd_is_root())
    return CMD_FAILED;

  namespace = nv_get_cwd();  

  if (NULL == (partition = context_get_ptr(const char)))
      partition = DEF_NVS_PARTITION;

  if (nvs_open_from_partition(partition,namespace, NVS_READONLY, &handle) == ESP_OK) {

    // Get the blob length. Try both get_str and get_blob.
    // Set /is_str/ to /true/ if data was identified as a string
    //
    if (ESP_OK != (err = nvs_get_blob(handle, argv[1], NULL, &length))) {
      if (ESP_OK != (err = nvs_get_str(handle, argv[1], NULL, &length))) {
        q_printf("%% Blob \"%s\" does not exist. Make sure the key you are trying to dump\r\n"
                 "%% <i>exists</> and has type <i>\"char *\" or \"char []\"</>\r\n",argv[1]);
        nvs_close(handle);
        return ret;
      }
      is_str = true;
    }

    // If there is data available: allocate buffer, fetch data and print it:
    // for strings it is simple q_print(), for binary data is q_printtable()
    if (length > 0) {
      if ((mem = (unsigned char *)q_malloc(length, MEM_TMP)) != NULL) {
        err = is_str ? nvs_get_str(handle,argv[1], (char *)mem, &length)
                     : nvs_get_blob(handle,argv[1], mem, &length);
        if (ESP_OK == err) {
          if (is_str) {
            // make sure the string is terminated
            mem[length - 1] = '\0';

            // print it
            q_printf("%% \"%s\" = \"%s\"\r\n", argv[1], (char *)mem);
            q_print(CRLF);
          } else
            // print as a hex table view
            q_printhex(mem, length);
          ret = 0; // Success!
        } else
          q_print("% Error fetching binary data from NVS\r\n");
        q_free(mem);
      } else
        q_print("% Blob is too big (out of memory)\r\n");
    } else
      q_print("% Empty value (length of the data is zero)\r\n");
    nvs_close(handle);
  } else
     q_printf("%% Can not open NVS partition \"%s\" (namespace: \"%s\")\r\n", partition, namespace);
  return ret;
}

// new KEY C-TYPE
// Create a zero record. For scalar types, blobs and strings it is just 0 written
//
static int cmd_nvs_new(int argc, char **argv) {

  int ret = CMD_FAILED;
  esp_err_t err;
  nvs_handle_t handle;
  const char *partition, *namespace;
  char foo[] = { 0 };
  size_t size;
  bool is_str, is_blob, is_signed;
  
  if (argc < 3)
    return CMD_MISSING_ARG;

  // Determine the flash partition where NVS is located
  if (NULL == (partition = context_get_ptr(const char)))
    partition = DEF_NVS_PARTITION;

  // Determine current namespace. If we are in the root ("/") then show a note and exit
  namespace = nv_get_cwd();
  if (*namespace == '/') {
    q_print("% Can not create keys without a namespace\r\n"
            "% Change to desired namespace (\"cd My_Preferences\") and try again\r\n");
    return CMD_FAILED;
  }

  // Read user type definition
  size = read_ctype(argc, argv, 2, &is_str, &is_blob, &is_signed);
  if (size > 8 || (size == 0 && !is_str && !is_blob)) {
    q_print("% Sorry, can not parse your type definition\r\n"
            "% Use C syntax: \"<i>char</>\", \"<i>unsigned long long int</>\", \"<i>char *</>\" and so on\r\n"
            "% Use \"<i>char *</>\" to create strings; for blobs use \"<i>char []</>\"\r\n"
);
    return CMD_FAILED;
  }

  // Open corresponding NVS partition in read/write mode
  // Convert user type definition to nvs_type_t, and call corresponding "setter" with dummy value
  if (nvs_open_from_partition(partition,namespace, NVS_READWRITE, &handle) == ESP_OK) {
    switch(ct2nt(size,is_str, is_blob, is_signed)) {
      case NVS_TYPE_U8:   err = nvs_set_u8(handle,   argv[1], 0);   break; 
      case NVS_TYPE_I8:   err = nvs_set_i8(handle,   argv[1], 0);   break; 
      case NVS_TYPE_U16:  err = nvs_set_u16(handle,  argv[1], 0);   break; 
      case NVS_TYPE_I16:  err = nvs_set_i16(handle,  argv[1], 0);   break; 
      case NVS_TYPE_U32:  err = nvs_set_u32(handle,  argv[1], 0);   break; 
      case NVS_TYPE_I32:  err = nvs_set_i32(handle,  argv[1], 0);   break; 
      case NVS_TYPE_U64:  err = nvs_set_u64(handle,  argv[1], 0);   break; 
      case NVS_TYPE_I64:  err = nvs_set_i64(handle,  argv[1], 0);   break; 
      case NVS_TYPE_STR:  err = nvs_set_str(handle,  argv[1], foo); break; 
      case NVS_TYPE_BLOB: err = nvs_set_blob(handle, argv[1], foo, 1); break;
      default:            err = ESP_FAIL;
    };

    // No errors so far? Commit changes
    if (err == ESP_OK) {
      if (ESP_OK == nvs_commit(handle)) {
        ret = 0;
        q_printf("%% Key created. Use \"set %s ...\" to set its value\r\n", argv[1]);
      } else
        q_print("% <e>Failed to commit changes (flash error?)</>\r\n");
    } else
      q_print("%% <e>Failed, no changes were made to the NVS</>\r\n");
    nvs_close(handle);
  } else
    q_printf("%% Can not open NVS partition \"%s\" (namespace: \"%s\")\r\n", partition, namespace);
  return ret;
}

#if WITH_FS
// export NAMESPACE /PATH : export all keys from the NAMESPACE
// export * /PATH         : export all NVS content
// export /PATH           : export current namespace
static int cmd_nvs_export(int argc, char **argv) {

  const char *namespace;
  const char *filename;
  FILE *fp;

  // only 1 argument: export current namespace
  if (argc < 3) {
    if (nv_cwd_is_root()) {
      HELP(q_print("% <e>No namespace selected</>\r\n"
                   "% First argument of the \"export\" is used to select a namespace\r\n"
                   "% Command \"cd\" is also used to select namespaces\r\n"));
      return CMD_FAILED;
    }

    namespace = nv_get_cwd();
    filename = argv[1];
  } else {
    namespace = argv[1];
    filename = argv[2];
  }

  if ((fp = files_fopen(filename,"a+")) == NULL) {
    q_printf("%% <e>Can not open file \"%s\" for writing</>\r\n", filename);
    return CMD_FAILED;
  } 

  nv_export_csv(fp, namespace);
  fclose(fp);

  return 0;
}

//
//
//
static int cmd_nvs_import(int argc, char **argv) {
  NOT_YET();
  return 0;
}
#endif // #if WITH_FS
#endif // #if WITH_NVS
#endif // #if COMPILING_ESPSHELL
