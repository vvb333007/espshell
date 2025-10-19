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

//  -- RMT Sequences --
//
// THERMINOLOGY: A "level" : either logic 1 or logic 0 for a duration of X ticks
//               A "pulse" : two levels: e.g. logic 0 for X ticks THEN logic 1 for Y ticks
//               "bits"    : user defined asciiz string consisting of "1" and "0"
//               "bytes"   : user defined asciiz string representing a byte string: "1af4c675..."
//                 "one"   : what is "1" - can be a simple level or a pulse
//                 "zero"  : what is "0" -        -- // --
//               "levels"  : manually entered levels (instead of entering "bits", "one" and "zero")
//                           NOTE: when used, "bits"/"bytes" are compiled to "levels" automatically
//           "modulation"  : levels can be modulated with a carrier frequency. either 1's or 0's gets modulated
//                  "eot"  : End OF Transmission behaviour - hold line HIGH or LOW after the transmission is done
//
// Sequences: the data structure defining a sequence of pulses (levels) to
// be transmitted. Sequences are used to generate an user-defined LOW/HIGH
// pulses train on an arbitrary GPIO pin using ESP32's hardware RMT peri
//
// These are used by "sequence X" command (and by a sequence subdirectory
// commands as well) (see seq_...() functions and cmd_sequence_if() function)
//

// TODO: When padding, use zero rmt item, so user input will not be altered
// TODO: RMT RX, RMT decode
// TODO: profiles (nec, lg, samsung)

#if COMPILING_ESPSHELL

#define SEQUENCE_MAX (SEQUENCES_NUM - 1) // max number that can be used as sequence id 

// structure holding a sequence description
struct sequence {

  float tick;                  // uSeconds.  1000000 = 1 second.   0.1 = 0.1uS
  float mod_duty;              // modulator duty
  unsigned int mod_freq : 30;  // modulator frequency
  unsigned int mod_high : 1;   // modulate "1"s or "0"s
  unsigned int eot : 1;        // end of transmission level
#define SEQ_LOOP_INFINITE ((unsigned int )(-1))
#define SEQ_LOOP_NONE     1
  unsigned int loop_count;     // only used if >1, specifies loop count for the sequence
                               // value of (unsigned int)(-1) means loop infinitely
  int seq_len;                 // how many rmt_data_t items is in "seq"
  rmt_data_t *seq;             // array of rmt_data_t TODO: preallocate +2 symbols for, possible, tail & head
  rmt_data_t alph[2];          // alphabet. representation of "0" and "1"
  rmt_data_t ht[2];            // "head" [0] and "tail" [1]. TODO: make 2 members with proper names
  
  char *bits;                  // asciiz "100101101"
  char *bytes;                 // asciiz "2309 ab ffff" //TODO: make char *user_data instead which can keep bits or bytes
};

// Sets _Name pointer to the sequence being edited (struct sequence *)
#define THIS_SEQUENCE(_Name) \
  struct sequence * _Name = &sequences[context_get_uint()]; \
  if (unlikely(context_get_uint() >= SEQUENCES_NUM)) { \
    q_print("% THIS_SEQUENCE() : disrupted Context\r\n"); \
    return CMD_FAILED; \
  }

// sequences
static struct sequence sequences[SEQUENCES_NUM] = { 0 };

// calculate frequency from the tick length
// 0.1uS = 10Mhz
unsigned long __attribute__((const)) seq_tick2freq(const float tick_us) {

  return tick_us ? (unsigned long)((float)1000000 / tick_us) : 0;
}

static void seq_drop_levels(int seq) {
  if (seq >= 0 && seq < SEQUENCES_NUM)
    if (sequences[seq].seq) {
      q_free(sequences[seq].seq);
      sequences[seq].seq = NULL;
    }
}

// free memory buffers associated with the sequence:
// ->"bits" and ->"seq"
static void seq_freemem(int seq) {

  if (sequences[seq].bits) {
    q_free(sequences[seq].bits);
    sequences[seq].bits = NULL;
  }
  if (sequences[seq].bytes) {
    q_free(sequences[seq].bytes);
    sequences[seq].bytes = NULL;
  }
  if (sequences[seq].seq) {
    q_free(sequences[seq].seq);
    sequences[seq].seq = NULL;
  }
}

// initialize/reset sequences to default values
//
static void __attribute__((constructor)) _seq_init() {
  rmt_data_t zero = { 0 };
  for (int i = 0; i < SEQUENCES_NUM; i++) {
    sequences[i].tick = 1;
    seq_freemem(i);
    sequences[i].alph[0] = zero;
    sequences[i].alph[1] = zero;
    sequences[i].ht[0] = zero;
    sequences[i].ht[1] = zero;
    sequences[i].loop_count = SEQ_LOOP_NONE;
  }
}

// used by "show" command, sends output to the user
//
static void seq_show_rmt_symbol(rmt_data_t *sym) {
  if (likely(sym)) {
    if (sym->duration0) {
      if (sym->duration1)
        q_printf("%d/%d %d/%d", sym->level0, sym->duration0, sym->level1, sym->duration1);
      else
        q_printf("%d/%d", sym->level0, sym->duration0);
      return ;
    }
  }
  q_print("not set");
}

// Used by "save" command, sends output to the file
// TODO: add count arg to fprintf arrays of rmt symbols
static void seq_fprintf_rmt_symbol(FILE *fp, rmt_data_t *sym) {
  if (likely(sym && fp))
    if (sym->duration0) {
      if (sym->duration1)
        fprintf(fp,"%d/%d %d/%d", sym->level0, sym->duration0, sym->level1, sym->duration1);
      else
        fprintf(fp,"%d/%d", sym->level0, sym->duration0);
    }
}


// dump sequence content
static void seq_show(unsigned int seq) {

  struct sequence *s;

  if (seq >= SEQUENCES_NUM) {
    q_printf("%% <e>Sequence %u does not exist</>\r\n", seq);
    return;
  }

  s = &sequences[seq];

  q_printf("%%\r\n%% Sequence #%d:\r\n%% Resolution : %.4fuS per tick  (Frequency: %lu Hz)\r\n", seq, s->tick, seq_tick2freq(s->tick));
  if (s->seq) {
    int i;
    unsigned long total = 0;
    q_print("% Levels are ");
    for (i = 0; i < s->seq_len; i++) {
      if (!(i & 3))
        q_print("\r\n% ");
      q_printf("%d/%d, %d/%d, ", s->seq[i].level0, s->seq[i].duration0, s->seq[i].level1, s->seq[i].duration1);
      total += s->seq[i].duration0 + s->seq[i].duration1;
    }
    q_printf("\r\n%% Total: %d levels, duration: %lu ticks, (~%lu uS)\r\n", s->seq_len * 2, total, (unsigned long)((float)total * s->tick));
  } else
    q_print("% <i>Incomplete</>: levels are not set. (use \"levels\", \"bits\" or \"bytes\")\r\n");

  if (s->bits || s->bytes) {
    if (s->bits)
      q_printf("%% Bits sequence: (%d bits) \"%s\"\r\n", strlen(s->bits), s->bits);
    else {
      int tmp = strlen(s->bytes);
      if (tmp & 1)
        tmp++;
      tmp >>= 1;
      q_printf("%% Byte sequence: (%d byte%s) \"%s\"\r\n", PPA(tmp),s->bytes);
    }

    q_print("% Alphabet:\r\n");
    q_print("%   Zero (\"0\") is <i>");
    seq_show_rmt_symbol(&s->alph[0]);
    q_print("</>\r\n");

    q_print("%   One (\"1\") is <i>");
    seq_show_rmt_symbol(&s->alph[1]);
    q_print("</>\r\n");

    if (!s->alph[0].duration0 || !s->alph[1].duration0)
      q_print("% <i>Incomplete</>: alphabet is not defined (use \"zero\" and \"one\")\r\n");
  }

  q_print("% Optional header : <i>");
  seq_show_rmt_symbol(&s->ht[0]);
  q_print("</>\r\n");

  q_print("% Optional trailer : <i>");
  seq_show_rmt_symbol(&s->ht[1]);
  q_print("</>\r\n");

  q_print("% Looping : "); 
  if (s->loop_count == SEQ_LOOP_INFINITE)
    q_print("<i>yes, continuous</>\r\n"); 
  else if (s->loop_count <= SEQ_LOOP_NONE)
    q_print("disabled (no looping)\r\n"); 
  else
    q_printf("<i>enabled, %u repeats</>\r\n",s->loop_count); 

  q_print("% Modulation ");
  if (s->mod_freq)
    q_printf(" : yes, \"%s\" are modulated at %luHz, duty %.2f%%\r\n", s->mod_high ? "HIGH" : "LOW", (unsigned long)s->mod_freq, s->mod_duty * 100);
  else
    q_print("is not used\r\n");


  q_printf("%% End of transmission: <i>%s</>\r\n", s->eot ? "HIGH" : "LOW");
}



// convert a level string to numerical values:
// "1/500" gets converted to level=1 and duration=500
// level is either 0 or 1, duration IS IN RANGE  0..32767. If duration is "/" then
// it is set to the maximum value of 32767
//
// called with first two arguments set to NULL performs
// syntax check on arguments only
//
// returns 0 on success, <0 - syntax error
//

static int seq_atol(int *level, int *duration, char *p) {
  if (p && (p[0] == '0' || p[0] == '1') && (p[1] == '/' || p[1] == '\\')) {
    unsigned int d;
    if (p[2] == p[1]) d = 32767;
    else if (isnum(p + 2)) {
      if ((d = atol(p + 2)) > 32767) d = 32767;
    } else return -1;
    if (level) *level = *p - '0';
    if (duration) *duration = d;
    return 0;
  }
  return -1;
}


// check if sequence is configured and an be used
// to generate pulses. The criteria is:
// ->seq must be initialized
// ->tick must be set
static inline bool seq_isready(unsigned int seq) {
  return  (seq < SEQUENCES_NUM) && 
          (sequences[seq].seq != NULL) && 
          (sequences[seq].tick != 0.0f);
}

// compile 'bits' or 'bytes' to 'seq'
// compilation is done when following conditions are met:
//   1. both 'zero' and 'one' are set. Both must be of the
//      same type: either pulse (long form) or level (short form)
//   2. 'bits' or 'bytes' are set
//   3. 'seq' is NULL : i.e. is not compiled yet
//
// 'head' and 'tail' are optional
//
static int seq_compile(int seq) {

  struct sequence *s = &sequences[seq];
  int ht = 0; // how many extra RMT symbols we need to add Head and Tail?

  if (s->seq)  //already compiled
    return 0;

  // If used, both 'tail' and 'head' are expected to be set
  //
  if ((s->ht[0].duration0 && !s->ht[1].duration0) ||
      (!s->ht[0].duration0 && s->ht[1].duration0))
      return -7;

  // Add extra 2 RMT symbols if we have head/tail
  if (s->ht[0].duration0)
    ht = 2;

  if (s->alph[0].duration0 && s->alph[1].duration0 && s->bits) {


    // if "0" is defined as a "pulse" (i.e. both parts of rmt_data_t used
    // to define a symbol (IR protocols-type encoding) then we need "1" to
    // be defined as a "pulse" as well.
    //
    // allocate strlen(bits) items for a 'seq', initialize 'seq' items with
    // either "alph[0]" or "alph[1]" according to "bits"
    if (s->alph[0].duration1) {

      //long form. 1 rmt symbol carries 1 bit of data
      if (!s->alph[1].duration1) {
        q_print("% <e>\"One\" is defined as a level, but \"Zero\" is a pulse</>\r\n");
        return -1;
      }

      int j = 0, i = strlen(s->bits) + ht, k = 0;

      if (!i)
        return -2;

      if ((s->seq = (rmt_data_t *)q_malloc(sizeof(rmt_data_t) * i, MEM_SEQUENCE)) == NULL)
        return -3;

      // first element is a 'head' symbol
      if (ht)
        s->seq[j++] = s->ht[0];  

      // s->seq[j] = s->alph[0] or s->alph[1], depending on s->bits[j] value ('0' or '1')
      for (s->seq_len = i; s->bits[k] != 0; j++, k++)
        s->seq[j] = s->alph[s->bits[k] - '0'];  

      // last element is a 'tail' symbol
      if (ht)
        s->seq[j++] = s->ht[1];  

    } else {
      // short form (1 rmt symbol carry 2 bits of data)
      if (s->alph[1].duration1) {
        q_print("% <e>\"One\" is defined as a pulse, but \"Zero\" is a level</>\r\n");
        return -4;
      }
      int k, j, i = strlen(s->bits);

      // 1 rmt symbol can carry 2 bits so we want our "bits" string to be of even size.
      // if number of bits user had entered is not divisible by 2 then:
      //     TODO: if there is "tail" set, then we just add tail (always 1 level) to the bitstring
      //     TODO: if not - then pad with 0/0 0/0 rmt symbol
      if (ht)
        HELP(q_print("% \"head\" and \"tail\" are ignored for a short-form level sequence\r\n"));

      if (i & 1) {
        char *r = (char *)q_realloc(s->bits, i + 2, MEM_SEQUENCE);
        if (!r)
          return -5;
        s->bits = r;
        s->bits[i + 0] = s->bits[i - 1];
        s->bits[i + 1] = '\0';
        // TODO: must be padded with 0/0 RMT symbol!!!
        q_printf("%% Bit string was padded with one extra \"%c\" (must be even number of bits)\r\n", s->bits[i]);
        i++;
      }

      s->seq_len = i / 2;
      s->seq = (rmt_data_t *)q_malloc(sizeof(rmt_data_t) * s->seq_len, MEM_SEQUENCE);

      if (!s->seq)
        return -6;

      j = 0; // index of a bit from /.bits/ string
      k = 0; // index of a corresponding RMT entry

      while (j < i) {

        if (s->bits[j] == '1') {
          s->seq[k].level0 = s->alph[1].level0;
          s->seq[k].duration0 = s->alph[1].duration0;
        } else {
          s->seq[k].level0 = s->alph[0].level0;
          s->seq[k].duration0 = s->alph[0].duration0;
        }

        j++;

        if (s->bits[j] == '1') {
          s->seq[k].level1 = s->alph[1].level0;
          s->seq[k].duration1 = s->alph[1].duration0;
        } else {
          s->seq[k].level1 = s->alph[0].level0;
          s->seq[k].duration1 = s->alph[0].duration0;
        }

        j++;
        k++;
      }
    }  // short form end
  }    //if compilation criteria are met
  return 0;
}


//Send sequence 'seq' using GPIO 'pin'
//Sequence is fully configured
static int seq_send(unsigned int pin, unsigned int seq) {

  struct sequence *s = &sequences[seq];

  // S2 and C3 have only 2 TX blocks, others CPU have more
  if (!rmtInit(pin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_2, seq_tick2freq(s->tick))) {
    HELP(q_print("% RMT failed to initialize\r\n"));
    return -1;
  }

  if (!rmtSetCarrier(pin, s->mod_freq ? true : false,
                          s->mod_high == 1 ? false : true,
                          s->mod_freq,
                          s->mod_duty)) {
    HELP(q_print("% RMT failed to set carrier (bad frequency / duty range?)\r\n"));
    return -1;
  }

  if (!rmtSetEOT(pin, s->eot))
    return -1;

  if (s->loop_count > 1) {
    if (s->loop_count == SEQ_LOOP_INFINITE) {
      if (!rmtWriteLooping(pin, s->seq, s->seq_len)) {
        HELP(q_print("% RMT failed (rmtWriteLooping)\r\n"));
        return -1;
      }
      // success!
    } else {
#if ESP_ARDUINO_VERSION > ESP_ARDUINO_VERSION_VAL(3,3,0)
       if (!rmtWriteRepeated(pin, s->seq, s->seq_len, s->loop_count)) {
         HELP(q_print("% RMT failed (rmtWriteLoopingCount)\r\n"));
         return -1;
       }
#else       
      q_print("% Update your Arduino Core to use this feature\r\n");
      goto run_non_looping;
#endif      
    }
    // success!
  } else {
#if ESP_ARDUINO_VERSION > ESP_ARDUINO_VERSION_VAL(3,3,0)
run_non_looping:    
#endif
    if (!rmtWrite(pin, s->seq, s->seq_len, RMT_WAIT_FOR_EVER)) {
      HELP(q_print("% RMT failed (rmtWrite)\r\n"));
      return -1;
    }
    // success!
  }

  return 0;
}

// "sequence X"
// Save context, switch command list, change the prompt
//
static int cmd_seq_if(int argc, char **argv) {

  unsigned char seq;
  static char prom[MAX_PROMPT_LEN];
  if (argc < 2)
    return CMD_MISSING_ARG;

  if ((seq = q_atol(argv[1], SEQUENCES_NUM)) >= SEQUENCES_NUM) {
    HELP(q_print("% <e>Sequence numbers are 0.." xstr(SEQUENCE_MAX) "</>\r\n"));
    return 1;
  }

  // embed sequence number into the prompt
  sprintf(prom, PROMPT_SEQ, seq);
  change_command_directory(seq, KEYWORDS(sequence), prom, "pulse sequence");
  return 0;
}


//TAG:eot
//eot high|low
//
// set End-Of-Transmission line status. Once transmission is finished
// the esp32 hardware will pull the line either HIGH or LOW depending
// on the EoT setting. Default is LOW
//
static int cmd_seq_eot(int argc, char **argv) {

  THIS_SEQUENCE(s);

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!q_strcmp(argv[1], "high") || argv[1][0] == '1')
    s->eot = 1;
  else
    s->eot = 0;

  return 0;
}

//TAG:modulation
//
//modulation FREQ [DUTY [low|high]]
//
#define SEQ_MODULATION_FREQ_MAX 40000000 //TODO: find out real boundaries

static int cmd_seq_modulation(int argc, char **argv) {

  int high = 1;
  float duty = 0.33;
  unsigned int freq;

  THIS_SEQUENCE(s);

  // at least FREQ must be provided
  if (argc < 2)
    return CMD_MISSING_ARG;

  // if first argument is a keyword ("off") then q_atol returns 0
  // which means "modulation off" for the RMT peri
  freq = q_atol(argv[1], 0);
  if (freq > SEQ_MODULATION_FREQ_MAX) {
    HELP(q_print("% Modulation frequency must be between 0 and " xstr(SEQ_MODULATION_FREQ_MAX) " Hz\r\n"
                 "% Most IR receivers use 38kHz, some use 56kHz\r\n"
    ));  
    return 1;
  }

  // More arguments are available?
  if (argc > 2) {

    // read DUTY.
    // Duty cycle is a float number on range [0..1]
    duty = q_atof(argv[2], 2.0f);

    if (duty < 0.0f || duty > 1.0f) {
      HELP(q_print("% <e>Duty cycle is a number in range [0..1] (0.01 means 1% duty)</>\r\n"));
      return 2;
    }
  }
  //third argument: "high" or "1" means modulate when line is HIGH (modulate 1's)
  // "low" or "0" - modulate when line is LOW (modulate zeros)
  if (argc > 3) {
    if (!q_strcmp(argv[3], "low") || argv[3][0] == '1')
      high = 0;
    else if (!q_strcmp(argv[3], "high") || argv[3][0] == '0')
      high = 1;
    else
      return 3;  // 3rd argument was not understood
  }

  
  s->mod_freq = freq;
  s->mod_duty = duty;
  s->mod_high = high;

  return 0;
}

// one 1/100 [0/10]
// zero 1/100 [0/10]
// head 1/100 [0/10]
// tail 1/100
//
// Setup the alphabet to be used when encoding "bits". there are
// two symbols in the alphabet: 0 and 1. Both of them can be defined
// as a level (short form) or a pulse (long form):
//
// short scheme example:
// one 1/50 , zero 0/50 : 1 is HIGH for 50 ticks, 0 is LOW for 50 ticks
// one RMT symbol is used to transmit 2 bits
//
// long scheme example:
// one 1/50 0/10 , zero 1/100 0/10 : 1 is "HIGH/50ticks then LOW for 10 ticks"
// and 0 is "HIGH for 100 ticks then LOW for 10 ticks"
// one RMT symbol is used to transmit 1 bit.
//
static int cmd_seq_zeroone(int argc, char **argv) {

  THIS_SEQUENCE(s);                      // s is the pointer to the sequence

  bool recompile = false;                // should we drop old levels and recompile the sequence?
  int level, duration;
  rmt_data_t *alph;
  unsigned int seq = context_get_uint(); // seq is the sequence ID

  // which alphabet entry to set? should we recompile?
  // Head and Tail are implemented as the alphabet symbols, just like "one" and "zero"
  // Changing Head or Tail always lead to recompilation, while One and Zero only trigger
  // recompilation if sequence was successfully compiled before. 
  //
  // If there are only levels set (i.e. manually), then changing One and Zero will not 
  // lead to recompilation
  //
  if (!q_strcmp(argv[0], "one")) {
    alph = &s->alph[1];
    if (s->bits != NULL || s->bytes != NULL)
      recompile = true;
  } else if (!q_strcmp(argv[0], "zero")) {
    alph = &s->alph[0];
    if (s->bits != NULL || s->bytes != NULL)
      recompile = true;
  } else if (!q_strcmp(argv[0], "head")) {
    alph = &s->ht[0];
    recompile = true;
  } else if (!q_strcmp(argv[0], "tail")) {
    alph = &s->ht[1];
    recompile = true;
  } else
    return CMD_NOT_FOUND;

  //entry is short form by default
  alph->level1 = 0;
  alph->duration1 = 0;

  switch (argc) {
    // two arguments = a pulse
    // (long form)
    case 3:
      if (seq_atol(&level, &duration, argv[2]) < 0)
        return 2;
      alph->level1 = level;
      alph->duration1 = duration;
    //FALLTHRU
    // single value = a level
    // (short form)
    case 2:
      if (seq_atol(&level, &duration, argv[1]) < 0)
        return 1;
      alph->level0 = level;
      alph->duration0 = duration;
      break;

    default:
      return CMD_MISSING_ARG;  // wrong number of arguments
  };

  if (recompile && seq_isready(seq)) {
    VERBOSE(q_print("%% Recompilation triggered, clearing levels\r\n"));
    seq_drop_levels(seq);
  }
  seq_compile(seq);
  return 0;
}

// tick TIME
// sets resolution of a sequence:
// the duration of pulses and levels are measured in 'ticks':
// level of "1/100" means "hold line HIGH for 100 ticks".
// ticks are measured in microseconds and can be <1: lower limit
// is 0.0125 microsecond/tick which corresponds to RMT hardware
// frequency of 80MHz
//
// TODO: calculate min/max values from the APB frequency, we definitely need a hook on APB_freq_change..
//       
static int cmd_seq_tick(int argc, char **argv) {
  float tick;
  THIS_SEQUENCE(s);

  if (argc < 2)
    return CMD_MISSING_ARG;

  if (!isfloat(argv[1]))
    return 1;

  tick = q_atof(argv[1],-1.0f);

  if (tick < 0.0125 || tick > 3.2f) {
    HELP(q_print("% <e>Tick must be in range 0.0125..3.2 microseconds</>\r\n"));
    return 1;
  }

  s->tick = tick;

  seq_compile(context_get_uint());

  return 0;
}



//TAG:bits
//
// sets a bit string as a sequence.
// "zero" and "one" must be set as well to tell the hardware
// what 1 and 0 are
//
// depending of values of "one" and "zero" (short or long form)
// the pulse or level train will be generated
//
// long form (pulses) are used mostly for IR remote control
// or similar application where 0 and 1 is not just levels but
// complete pulses: going up AND down when transmitting one single bit
static int cmd_seq_bits(int argc, char **argv) {

  THIS_SEQUENCE(s);

  if (argc < 2)
    return CMD_MISSING_ARG;

  char *bits = argv[1];

  while (*bits == '1' || *bits == '0')
    bits++;

  if (*bits != '\0')
    return 1; // first argument is bad binary number

  seq_freemem(context_get_uint());
  s->bits = q_strdup(argv[1], MEM_SEQUENCE);

  if (!s->bits)
    return CMD_FAILED;

  seq_compile(context_get_uint());

  return 0;
}

//
//
// instead setting up an alphabet ("zero", "one") and a bit string,
// the pattern can be set as simple sequence of levels.
//
// sequence of levels does not require compiling
//
static int cmd_seq_levels(int argc, char **argv) {

  int i, j;
  THIS_SEQUENCE(s);

  if (argc < 2)
    return CMD_MISSING_ARG;

  // check if all levels have correct syntax
  for (i = 1; i < argc; i++)
    if (seq_atol(NULL, NULL, argv[i]) < 0)
      return i;

  seq_freemem(context_get_uint());

  i = argc - 1;

  if (i & 1) {
    q_print("% <e>Uneven number of levels. Please add 1 more</>\r\n");
    return CMD_FAILED;
  }


  s->seq_len = i / 2;
  s->seq = (rmt_data_t *)q_malloc(sizeof(rmt_data_t) * s->seq_len, MEM_SEQUENCE);

  if (!s->seq)
    return CMD_FAILED;


  memset(s->seq, 0, sizeof(rmt_data_t) * s->seq_len);

  // each RMT symbol (->seq entry) can hold 2 levels
  // run thru all arguments and read level/duration pairs
  // into ->seq[].
  // i - index to arguments
  // j - index to ->seq[] (RMT entries)
  for (i = 0, j = 0; i < s->seq_len * 2; i += 2) {

    int level, duration;
    if (seq_atol(&level, &duration, argv[i + 1]) < 0)
      return i + 1;

    s->seq[j].level0 = level;
    s->seq[j].duration0 = duration;

    if (seq_atol(&level, &duration, argv[i + 2]) < 0)
      return i + 2;

    s->seq[j].level1 = level;
    s->seq[j].duration1 = duration;

    j++;
  }

  return 0;
}

// display the sequence content.
// can be called either from 'sequence' command subderictory
// or from the root:
// esp32-seq#> show
// esp32#> show seq 0
//
static int cmd_show_sequence(int argc, char **argv) {

  unsigned int seq;

  // command executed as "show" within sequence command tree (no arguments)
  if (argc < 2) {
    seq_show(context_get_uint());
    return 0;
  }

  // command executed as "show seq NUMBER", two arguments (argc=3)
  if (argc != 3)
    return CMD_MISSING_ARG;

  if ((seq = q_atol(argv[2], SEQUENCES_NUM)) >= SEQUENCES_NUM)
    return 2;

  seq_show(seq);
  return 0;
}

static int cmd_seq_bytes(int argc, char **argv) {
  q_print("% Not implemented yet, use \"bits\"\r\n");
  return 0;
}

// "loop [NUM|none]"
//
static int cmd_seq_loop(int argc, char **argv) {
  THIS_SEQUENCE(s);
  s->loop_count = (argc < 2) ? SEQ_LOOP_INFINITE : q_atol(argv[1], 1);
  return 0;
}

#if WITH_FS
// Save *this* sequence configuration to a file
//
static int cmd_seq_save(int argc, char **argv) {

  FILE *fp = NULL;

  THIS_SEQUENCE(s);

  // No arguments?
  if (argc < 2) {
    HELP(q_print("%% File name (and path) is expected (e.g. \"/ffat/My Downloads/config.txt\")\r\n"));
    return CMD_MISSING_ARG;
  }

  // More than argument may mean the filename user entered contains spaces
  if (argc > 2) {
    HELP(q_print("%% If your path contain spaces, use quotes: (<i>save \"/ffat/My Dir/1.txt\"</>)\r\n"));
    return CMD_FAILED;
  }

  // Touch the file. it will create all the required directories
  // Fortunately, the "touch PATH" and "save PATH" have the PATH at the same position so
  // we can call cmd_files_touch(argc, argv) directly. TODO: this is a hack. Must have separate APIs for files
  // Note that "touch" accepts more than one argument so cut any extra arguments off
  //
  if (q_touch(argv[1]) < 0) {
    q_print("% Is filesystem mounted?\r\n");
    return CMD_FAILED;
  }
  // Append to existing file or create new.
  // By default we append, so every module can write its configuratuion into single config file
  if ((fp = files_fopen(argv[1],"a")) == NULL)
    return CMD_FAILED;

  fprintf(fp,"\r\n// Sequence configuration\r\n");
  fprintf(fp,"sequence %u\r\n",context_get_uint());
  fprintf(fp,"  tick %.4f\r\n",s->tick);
  if (s->bits)
    fprintf(fp,"  bits %s\r\n",s->bits);
  else if (s->bytes)
    fprintf(fp,"  bytes %s\r\n",s->bytes);
  else if (s->seq) {
    fprintf(fp,"  levels");
    for (int i = 0; i < s->seq_len; i++)
      fprintf(fp," %d/%d %d/%d", s->seq[i].level0, s->seq[i].duration0, s->seq[i].level1, s->seq[i].duration1);
      // TODO: use seq_fprintf_rmt_symbol(fp,&s->seq[i]); 
    fprintf(fp,CRLF);
  }
  if (s->alph[0].duration0) {
    fprintf(fp,"  zero ");
    seq_fprintf_rmt_symbol(fp,&s->alph[0]);
    fprintf(fp,CRLF);
  }
  if (s->alph[1].duration0) {
    fprintf(fp,"  one ");
    seq_fprintf_rmt_symbol(fp,&s->alph[1]);
    fprintf(fp,CRLF);
  }
  if (s->ht[0].duration0) {
    fprintf(fp,"  head ");
    seq_fprintf_rmt_symbol(fp,&s->ht[0]);
    fprintf(fp,CRLF);
  }
  if (s->ht[1].duration0) {
    fprintf(fp,"  tail ");
    seq_fprintf_rmt_symbol(fp,&s->ht[1]);
    fprintf(fp,CRLF);
  }

  if (s->loop_count == SEQ_LOOP_INFINITE)
    fprintf(fp,"  loop\r\n");
  else if (s->loop_count > 1)
    fprintf(fp,"  loop %u\r\n",s->loop_count);

  if (s->mod_freq)
    fprintf(fp,"  modulation %u %f %s\r\n",s->mod_freq, s->mod_duty, s->mod_high ? "high" : "low");
  if (s->eot)
    fprintf(fp,"  eot high\r\n");
  fprintf(fp,"exit\r\n");  

  if (fp)
    fclose(fp);

  return 0;
}
#endif // WITH_FS

static UNUSED int cmd_seq_decode(int argc, char **argv) {
  q_print("% RTM-RX: Not implemented yet, wait for the next version\r\n");
  return 0;
}

static UNUSED int cmd_seq_capture(int argc, char **argv) {
  q_print("% RTM-RX: Not implemented yet, wait for the next version\r\n");
  return 0;
}

static UNUSED int cmd_seq_profile(int argc, char **argv) {
  q_print("% RTM-RX: Not implemented yet, wait for the next version\r\n");
  return 0;
}

#endif

