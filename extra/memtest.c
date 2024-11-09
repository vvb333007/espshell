// This is part of espshell (github/vvb333007/espshell) project.
//
#if COMPILING_ESPSHELL

// -- Memory wrappers for leaks hunting --
//
// memory calls (malloc,realloc,free and strdup) are wrapped to keep track of
// allocations and report ememory usage statistics.
//
// espshell stores all allocation in a list and creates 2 bytes overwrite detection
// zone at the end of the buffer allocated. these are checked upon q_free()
//
// statistics is displayed by "mem" command
//
typedef struct list_s {
  struct list_s *next;
} list_t;

// memory record struct
typedef struct {
  list_t         li;      // list item. must be first member of a struct
  unsigned char *ptr;     // pointer to memory (as per malloc())
  unsigned int   len:19;  // length (as per malloc())
  unsigned int   type:4;  // user-defined type
} memlog_t;

// human-readable memory types
static const char *memtags[] = {

  "MEM_EDITLINE",
  "MEM_ARGV",
  "MEM_ARGCARGV",
  "MEM_LINE",
  "MEM_SCREEN",
  "MEM_HISTORY",
  "MEM_TEXT2BUF",
  "MEM_MOUNTPOINT",
  "MEM_PATH",
  "MEM_CWD",
  "MEM_CAT",
  "MEM_GETLINE",
  "MEM_SEQUENCE",
  "MEM_RMT",
  "MEM_QPRINTF",
  "MEM_VAR",
};

// allocated blocks
static memlog_t *head = NULL;

// allocated memory total, and overhead added by memory logger
static unsigned int allocated = 0, internal = 0;

// memory logger mutex to access memory records list
static xSemaphoreHandle mem_mux = NULL;

// lock/unlock memory records list
#define memlog_lock()   do { if (mem_mux) xSemaphoreTake(mem_mux, portMAX_DELAY); } while( 0 )
#define memlog_unlock() do { if (mem_mux) xSemaphoreGive(mem_mux); } while( 0 )

static void q_meminit() {
  if (!mem_mux)
    if ((mem_mux = xSemaphoreCreateMutex()) == NULL)
      // short print to keepq_print from allocating a memory buffer
      q_print("% Memory usage tracking module failed to initialize (semaphore)\r\n");
}

// memory allocated with extra 2 bytes: those are memory buffer overrun
// markers. we check these at every q_free()
static void *q_malloc(size_t size, int type) {

  unsigned char *p = NULL;
  memlog_t *ml;

  if ((type >= 0) && (type < 16) && (size < 0x80000) && (size > 0))
    if ((p = (unsigned char *)malloc(size + 2)) != NULL)
      if ((ml = (memlog_t *)malloc(sizeof(memlog_t))) != NULL) {
        ml->ptr = p;
        ml->len = size;
        ml->type = type;
        memlog_lock();
        ml->li.next = (list_t *)head;
        head = ml;
        allocated += size;
        internal += sizeof(memlog_t) + 2;
        memlog_unlock();
        p[size + 0] = 0x55;
        p[size + 1] = 0xaa;
      }
  return (void *)p;
}

// free() wrapper. check if memory was allocated by q_malloc/realloc or q_strdup. 
// does not free() memory if address is not on the list.
// ignores NULL pointers, checks buffer integrity (2 bytes at the end of the buffer)
//
static void q_free(void *ptr) {
  if (!ptr)
    q_printf("FIXME: q_free() : attempt to free(NULL) ignored\r\n");
  else {
    memlog_t *ml, *prev = NULL;
    const unsigned char *p = (const unsigned char *)ptr;
    memlog_lock();
    for (ml = head; ml != NULL; ml = (memlog_t *)(ml->li.next)) {
      if (ml->ptr == p) {
        if (prev)
          prev->li.next = ml->li.next;
        else
          head = (memlog_t *)ml->li.next;
        allocated -= ml->len;
        internal -= (sizeof(memlog_t) + 2);
        break;
      }
      prev = ml;
    }
    memlog_unlock();
    if (ml) {
      // check for memory buffer linear write overruns
      if (ml->ptr[ml->len + 0] != 0x55 || ml->ptr[ml->len + 1] != 0xaa)
        q_printf("CRITICAL: q_free() : buffer %p (length: %d, type %d), overrun detected\r\n",ptr,ml->len,ml->type);
      free(ptr);
      free(ml);
    }
    else
      printf("q_free() : address %p is not  on the list, do nothing\r\n",ptr);
  }
}

// generic realloc(). it is much worse than newlib's one because this one
// doesn't know anything about heap structure and can't simple "extend" block.
// instead straightforward "allocate then copy" strategy is used
//
static void *q_realloc(void *ptr, size_t new_size,UNUSED int type) {

	char *nptr;
  memlog_t *ml;

  // trivial cases
	if (ptr == NULL)
		return q_malloc(new_size,type);

	if (new_size == 0 && ptr != NULL) {
		q_free(ptr);
		return NULL;
	}

  memlog_lock();
  for (ml = head; ml != NULL; ml = (memlog_t *)(ml->li.next))
    if (ml->ptr == (unsigned char *)ptr)
      break;
  
  if (!ml) {
    memlog_unlock();
    printf("q_realloc() : trying to realloc pointer %p which is not on the list\r\n",ptr);
    return NULL;
  }

	if (new_size == ml->len) {
    memlog_unlock();
		return ptr;
  }

	if ((nptr = (char *)malloc(new_size + 2)) != NULL) {

    nptr[new_size + 0] = 0x55;
    nptr[new_size + 1] = 0xaa;

    memcpy(nptr, ptr, (new_size < ml->len) ? new_size : ml->len);
  	free(ptr);

    ml->ptr = (unsigned char *)nptr;
    allocated -= ml->len;
    ml->len = new_size;
    allocated += new_size;
  }

  memlog_unlock();
	return nptr;
}

// last of the family: strdup()
static char *q_strdup(const char *ptr, int type) {
  char *p = NULL;
  if (ptr != NULL) {
    int len = strlen(ptr);
    if ((p = (char *)q_malloc(len + 1,type)) != NULL)
      strcpy(p,ptr);
  }
  return p;
}

// display memory usage statistics by ESPShell
static void q_memleaks(const char *text) {
  int count = 0;

  q_printf("%s\r\n%% Dynamic memory used by ESPShell: %u (+ %u qlib overhead) bytes\r\n",text,allocated,internal);
  for (memlog_t *ml = head; ml; ml = (memlog_t *)(ml->li.next))
    q_printf("%% %u: type: %s, size: %u, ptr=%p\r\n",++count,memtags[ml->type],ml->len,ml->ptr);
  q_printf("%% %u memory block%s in total\r\n",count, count == 1 ? "" : "s");
}

#endif //COMPILING_ESPSHELL
