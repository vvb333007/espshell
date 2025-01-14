/* 
 * This file is a part of ESP32Shell for the Arduino Framework by vvb333007
 * Author: Viacheslav Logunov <vvb333007@gmail.com>, 
 *
 * Latest source code is at: https://github.com/vvb333007/espshell/
 * Feel free to use it as your wish, however credits would be greatly appreciated.
 */



//  -- RMT Sequences --
//
// THERMINOLOGY: A "level" : either logic 1 or logic 0 for a duration of X ticks
//               A "pulse" : two levels: e.g. logic 0 for X ticks THEN logic 1 for Y ticks
//               "bits"    : user defined asciiz string consisting of "1" and "0"
//                 "one"   : what is "1" - can be a simple level or a pulse
//                 "zero"  : what is "0" -        -- // --
//               "levels"  : manually entered levels (instead of entering "bits", "one" and "zero")
//                           NOTE: when used, "bits"gets compiled to "levels" automatically
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

#if COMPILING_ESPSHELL


// structure holding a sequence description
struct sequence {

  float tick;                  // uSeconds.  1000000 = 1 second.   0.1 = 0.1uS
  float mod_duty;              // modulator duty
  unsigned int mod_freq : 30;  // modulator frequency
  unsigned int mod_high : 1;   // modulate "1"s or "0"s
  unsigned int eot : 1;        // end of transmission level
  int seq_len;                 // how many rmt_data_t items is in "seq"
  rmt_data_t *seq;             // array of rmt_data_t
  rmt_data_t alph[2];          // alphabet. representation of "0" and "1"
  
  char *bits;                  // asciiz "100101101"
  //unsigned char *bytes;        // TODO: use byte encoder for RMT
  //unsigned char  bcount;       
};

// sequences
static struct sequence sequences[SEQUENCES_NUM] = { 0 };

// calculate frequency from tick length
// 0.1uS = 10Mhz
unsigned long __attribute__((const)) seq_tick2freq(float tick_us) {

  return tick_us ? (unsigned long)((float)1000000 / tick_us) : 0;
}

// free memory buffers associated with the sequence:
// ->"bits" and ->"seq"
static void seq_freemem(int seq) {

  if (sequences[seq].bits) {
    q_free(sequences[seq].bits);
    sequences[seq].bits = NULL;
  }
  if (sequences[seq].seq) {
    q_free(sequences[seq].seq);
    sequences[seq].seq = NULL;
  }
}

// initialize/reset sequences to default values
//
static void seq_init() {

  for (int i = 0; i < SEQUENCES_NUM; i++) {
    sequences[i].tick = 1;
    seq_freemem(i);
    sequences[i].alph[0].duration0 = sequences[i].alph[0].duration1 = 0;
    sequences[i].alph[1].duration0 = sequences[i].alph[1].duration1 = 0;
  }
}

// dump sequence content
static void seq_dump(unsigned int seq) {

  struct sequence *s;

  if (seq >= SEQUENCES_NUM) {
    q_printf("%% <e>Sequence %d does not exist</>\r\n", seq);
    return;
  }

  s = &sequences[seq];

  q_printf("%%\r\n%% Sequence #%d:\r\n%% Resolution : %.4fuS  (Frequency: %lu Hz)\r\n", seq, s->tick, seq_tick2freq(s->tick));
  q_print("% Levels are ");
  if (s->seq) {
    int i;
    unsigned long total = 0;

    for (i = 0; i < s->seq_len; i++) {
      if (!(i & 3))
        q_print("\r\n% ");
      q_printf("%d/%d, %d/%d, ", s->seq[i].level0, s->seq[i].duration0, s->seq[i].level1, s->seq[i].duration1);
      total += s->seq[i].duration0 + s->seq[i].duration1;
    }
    q_printf("\r\n%% Total: %d levels, duration: %lu ticks, (~%lu uS)\r\n", s->seq_len * 2, total, (unsigned long)((float)total * s->tick));
  } else
    q_print(Notset);

  q_print("% Modulation ");
  if (s->mod_freq)
    q_printf(" : yes, \"%s\" are modulated at %luHz, duty %.2f%%\r\n", s->mod_high ? "HIGH" : "LOW", (unsigned long)s->mod_freq, s->mod_duty * 100);
  else
    q_print("is not used\r\n");

  q_print("% Bit sequence is ");
  if (s->bits) {

    q_printf(": (%d bits) \"%s\"\r\n", strlen(s->bits), s->bits);
    q_print("% Zero is ");
    if (s->alph[0].duration0) {
      if (s->alph[0].duration1)
        q_printf("%d/%d %d/%d\r\n", s->alph[0].level0, s->alph[0].duration0, s->alph[0].level1, s->alph[0].duration1);
      else
        q_printf("%d/%d\r\n", s->alph[0].level0, s->alph[0].duration0);
    } else
      q_print(Notset);

    q_print("% One is ");
    if (s->alph[1].duration0) {
      if (s->alph[1].duration1)
        q_printf("%d/%d %d/%d\r\n", s->alph[1].level0, s->alph[1].duration0, s->alph[1].level1, s->alph[1].duration1);
      else
        q_printf("%d/%d\r\n", s->alph[1].level0, s->alph[1].duration0);
    } else
      q_print(Notset);
  } else
    q_print(Notset);

  q_printf("%% Hold %s after transmission is done\r\n", s->eot ? "HIGH" : "LOW");
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

// compile 'bits' to 'seq'
// compilation is done when following conditions are met:
//   1. both 'zero' and 'one' are set. Both must be of the
//      same type: either pulse (long form) or level (short form)
//   2. 'bits' are set
//   3. 'seq' is NULL : i.e. is not compiled yet
static int seq_compile(int seq) {

  struct sequence *s = &sequences[seq];

  if (s->seq)  //already compiled
    return 0;

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

      int j, i = strlen(s->bits);
      if (!i)
        return -2;
      if ((s->seq = (rmt_data_t *)q_malloc(sizeof(rmt_data_t) * i, MEM_SEQUENCE)) == NULL)
        return -3;
      for (s->seq_len = i, j = 0; j < i; j++)
        s->seq[j] = s->alph[s->bits[j] - '0'];  // s->seq[j] = s->alph[0] or s->alph[1], depending on s->bits[j] value ('0' or '1')
    } else {
      // short form (1 rmt symbol carry 2 bits of data)
      if (s->alph[1].duration1) {
        q_print("% <e>\"One\" is defined as a pulse, but \"Zero\" is a level</>\r\n");
        return -4;
      }

      int k, j, i = strlen(s->bits);

      // 1 rmt symbol can carry 2 bits so we want our "bits" string to be of even size.
      // if number of bits user had entered is not divisible by 2 then the last bit of a string gets duplicated:
      // if user enters "101" it will be padded with one extra "1"

      if (i & 1) {
        char *r = (char *)q_realloc(s->bits, i + 2, MEM_SEQUENCE);
        if (!r)
          return -5;
        s->bits = r;
        s->bits[i + 0] = s->bits[i - 1];
        s->bits[i + 1] = '\0';
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

  if (!rmtInit(pin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, seq_tick2freq(s->tick)))
    return -1;
  if (!rmtSetCarrier(pin, s->mod_freq ? true : false, s->mod_high == 1 ? false : true, s->mod_freq, s->mod_duty))
    return -2;
  if (!rmtSetEOT(pin, s->eot))
    return -3;
  if (!rmtWrite(pin, s->seq, s->seq_len, RMT_WAIT_FOR_EVER))
    return -4;

  return 0;
}


//TAG:seq
//"sequence X"
// save context, switch command list, change the prompt
static int cmd_seq_if(int argc, char **argv) {

  unsigned char seq;
  static char prom[MAX_PROMPT_LEN];
  if (argc < 2)
    return -1;

  if ((seq = q_atol(argv[1], SEQUENCES_NUM)) >= SEQUENCES_NUM) {
    HELP(q_printf("%% <e>Sequence numbers are 0..%d</>\r\n", SEQUENCES_NUM - 1));
    return 1;
  }

  // embed sequence number into prompt
  sprintf(prom, PROMPT_SEQ, seq);
  change_command_directory(seq, keywords_sequence, prom, "pulse sequence");
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

  if (argc < 2)
    return -1;

  if (!q_strcmp(argv[1], "high") || argv[1][0] == '1')
    sequences[Context].eot = 1;
  else
    sequences[Context].eot = 0;

  return 0;
}

//TAG:modulation
//
//modulation FREQ [DUTY [low|high]]
//
#define SEQ_MODULATION_FREQ_MAX 40000000 //TODO: find out real boundaries

static int cmd_seq_modulation(int argc, char **argv) {

  int high = 1;
  float duty = 0.5;
  unsigned int freq;

  // at least FREQ must be provided
  if (argc < 2)
    return -1;

  freq = q_atol(argv[1], 0);
  if (!freq || freq > SEQ_MODULATION_FREQ_MAX) {
    HELP(q_print("% Frequency must be between 1 and " xstr(SEQ_MODULATION_FREQ_MAX) " Hz\r\n"));  
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

  sequences[Context].mod_freq = freq;
  sequences[Context].mod_duty = duty;
  sequences[Context].mod_high = high;

  return 0;
}

//one 1/100 [0/10]
//zero 1/100 [0/10]
//
// Setup the alphabet to be used when encoding "bits". there are
// two symbols in alphabet: 0 and 1. Both of them can be defined
// as a level (short form) or a pulse (long form):
//
// short scheme example:
// one 1/50 , zero 0/50 : 1 is HIGH for 50 ticks, 0 is LOW for 50 ticks
// one RMT symbol is used to transmit 2 bits
//
// long scheme example:
// one 1/50 0/10 , zero 1/100 0/10 : 1 is "HIGH/50ticks then LOW for 10 tiks"
// and 0 is "HIGH for 100 ticks then LOW for 10 ticks"
// one RMT symbol is used to transmit 1 bit.
//
static int cmd_seq_zeroone(int argc, char **argv) {

  struct sequence *s = &sequences[Context];

  int i = 0;
  int level, duration;

  // which alphabet entry to set?
  if (!q_strcmp(argv[0], "one"))
    i = 1;

  //entry is short form by default
  s->alph[i].level1 = 0;
  s->alph[i].duration1 = 0;


  switch (argc) {
    // two arguments = a pulse
    // (long form)
    case 3:
      if (seq_atol(&level, &duration, argv[2]) < 0)
        return 2;
      s->alph[i].level1 = level;
      s->alph[i].duration1 = duration;


    //FALLTHRU
    // single value = a level
    // (short form)
    case 2:
      if (seq_atol(&level, &duration, argv[1]) < 0)
        return 1;
      s->alph[i].level0 = level;
      s->alph[i].duration0 = duration;
      break;


    default:
      return -1;  // wrong number of arguments
  };
  seq_compile(Context);
  return 0;
}

//TAG:tick
//
// tick TIME
// sets resolution of a sequence:
// the duration of pulses and levels are measured in 'ticks':
// level of "1/100" means "hold line HIGH for 100 ticks".
// ticks are measured in microseconds and can be <1: lower limit
// is 0.0125 microsecond/tick which corresponds to RMT hardware
// frequency of 80MHz
static int cmd_seq_tick(int argc, char **argv) {
  if (argc < 2)
    return -1;

  if (!isfloat(argv[1]))
    return 1;

  sequences[Context].tick = atof(argv[1]);

  if (sequences[Context].tick < 0.0125 || sequences[Context].tick > 3.2f) {
    HELP(q_print("% <e>Tick must be in range 0.0125..3.2 microseconds</>\r\n"));
    return 1;
  }

  seq_compile(Context);

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

  struct sequence *s = &sequences[Context];

  if (argc < 2)
    return -1;

  char *bits = argv[1];

  while (*bits == '1' || *bits == '0')
    bits++;

  if (*bits != '\0')
    return 1;

  seq_freemem(Context);
  s->bits = q_strdup(argv[1], MEM_SEQUENCE);

  if (!s->bits)
    return -1;

  seq_compile(Context);

  return 0;
}

//TAG:levels
//
// instead setting up an alphabet ("zero", "one") and a bit string,
// the pattern can be set as simple sequence of levels.
//
// sequence of levels does not require compiling
//
static int cmd_seq_levels(int argc, char **argv) {

  int i, j;
  struct sequence *s = &sequences[Context];

  if (argc < 2)
    return -1;

  // check if all levels have correct syntax
  for (i = 1; i < argc; i++)
    if (seq_atol(NULL, NULL, argv[i]) < 0)
      return i;

  seq_freemem(Context);

  i = argc - 1;

  if (i & 1) {
    q_print("% <e>Uneven number of levels. Please add 1 more</>\r\n");
    return 0;
  }


  s->seq_len = i / 2;
  s->seq = (rmt_data_t *)q_malloc(sizeof(rmt_data_t) * s->seq_len, MEM_SEQUENCE);

  if (!s->seq)
    return -1;


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


//TAG:show
//
// display the sequence content.
// can be called either from 'sequence' command subderictory
// or from the root:
// esp32-seq#> show
// esp32#> show seq 0
//
static int cmd_seq_show(int argc, char **argv) {

  unsigned int seq;

  // command executed as "show" within sequence
  // command tree (no arguments)
  if (argc < 2) {
    seq_dump(Context);
    return 0;
  }

  // command executaed as "show seq NUMBER".
  // two arguments (argc=3)
  if (argc != 3)
    return -1;

  if ((seq = q_atol(argv[2], SEQUENCES_NUM)) >= SEQUENCES_NUM)
    return 2;

  seq_dump(seq);
  return 0;
}
#endif

