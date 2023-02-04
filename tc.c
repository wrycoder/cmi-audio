/* TimeCheck
 *
 * (c) 2023 Michael Toulouse
 *
 */

#include "sox.h"
#include "tc.h"
#include <stdlib.h>
#include <errno.h>
#include <windows.h>
#include <string.h>
#include <strsafe.h>
#include <stdio.h>
#include <assert.h>

typedef struct {
  double          window;
  double          window_current;
  double          window_end;
  size_t          window_size;
  double          rms_sum;
  double          rms_threshold;
  double          max_rms_sum;
} priv_t;

static void report_error(int errcode, int line_number, int sox_error) {
  printf("ERROR %d at line %d in source file %s\n ", errcode, line_number, __FILE__);
  printf("Error Number: %d\n ", errcode);
  if (errcode == SOX_LIB_ERROR) {
    printf("Error Description: %s\n", sox_strerror(sox_error));
  } else {
    printf("Error Description: %s\n", strerror(errcode));
  }
}

static char const * str_time(double seconds)
{
  static TCHAR string[16][50];
  size_t cchDest = 50;
  static int i;
  LPCTSTR pszFormat = TEXT("%02i:%02i:%05.2f");
  int hours, mins = seconds / 60;
  seconds -= mins * 60;
  hours = mins / 60;
  mins -= hours * 60;
  i = (i+1) & 15;
  StringCchPrintf(string[i], cchDest, pszFormat, hours, mins, seconds);
  return string[i];
}

static double soxi_total;
static sox_format_t * in, * out;
static void trim_silence(char *);
static sox_bool is_louder(sox_effect_t const * effp,
                          sox_sample_t value /* >= 0 */,
                          double threshold,
                          int unit);

static void clear_rms(sox_effect_t * effp)
{
  priv_t * checker = (priv_t *) effp->priv;

  memset(&checker->window, 0,
         checker->window_size * sizeof(double));

  checker->window_current = checker->window;
  checker->window_end = checker->window + checker->window_size;
  checker->rms_sum = 0;
  checker->max_rms_sum = 0;
  /* For testing, default threshold is 85% */
  checker->rms_threshold = 85;
}

static sox_sample_t compute_rms(sox_effect_t * effp, sox_sample_t sample)
{
  priv_t * checker = (priv_t *) effp->priv;
  double new_sum;
  sox_sample_t rms;

  new_sum = checker->rms_sum;
  new_sum -= checker->window_current;
  new_sum += ((double)sample * (double)sample);

  rms = sqrt(new_sum / checker->window_size);
  return (rms);
}

static TCHAR home_directory[MAX_PATH];

/* All done; tidy up: */
static void cleanup()
{
  if (in != NULL) sox_close(in);
  if (out != NULL) sox_close(out);
  SetCurrentDirectory(home_directory);
  sox_quit();
}

enum user_actions { half_sec, whole_sec, quit };
static int main_menu()
{
  char choice;
  enum user_actions a;

  printf( "\nPlease select one of the following:\n"\
          "0 - trim half a second\n"\
          "1 - trim a whole second\n"\
          "2 - quit\n\n");
  scanf("%d", &a);
  return a;
}

static int output_handler_start(sox_effect_t * effp)
{
  priv_t * checker = (priv_t *) effp->priv;
  printf("start: window=%g\nwindow_current=%g\nwindow_end=%g\n"
            "window_size=%" PRIuPTR "\nrms_sum=%g\n",
    checker->window, checker->window_current,
    checker->window_end, checker->window_size,
    checker->rms_sum
  );
  clear_rms(effp);
  return SOX_SUCCESS;
}

/* COPIED FROM example1.c ... */
/* The function that will be called to input samples into the effects chain.
 * In this example, we get samples to process from a SoX-openned audio file.
 * In a different application, they might be generated or come from a different
 * part of the application. */
static int input_drain(
    sox_effect_t * effp, sox_sample_t * obuf, size_t * osamp)
{
  priv_t * checker = (priv_t *) effp->priv;

  /* ensure that *osamp is a multiple of the number of channels. */
  *osamp -= *osamp % effp->out_signal.channels;

  /* Read up to *osamp samples into obuf; store the actual number read
   * back to *osamp */
  *osamp = sox_read(in, obuf, *osamp);

  /* sox_read may return a number that is less than was requested; only if
   * 0 samples is returned does it indicate that end-of-file has been reached
   * or an error has occurred */
  if (!*osamp && in->sox_errno)
    fprintf(stderr, "%s: %s\n", in->filename, in->sox_errstr);
  return *osamp? SOX_SUCCESS : SOX_EOF;
}

/* COPIED FROM example1.c ... */
/* The function that will be called to output samples from the effects chain.
 * The samples will be analyzed. */
static int output_flow(sox_effect_t *effp, sox_sample_t const * ibuf,
    sox_sample_t * obuf, size_t * isamp, size_t * osamp)
{
  priv_t * checker = (priv_t *) effp->priv;
  /* len is total samples, chcnt counts channels */
  int len = 0;
  sox_sample_t t_ibuf;
  size_t chcnt;

  len = ((*isamp > *osamp) ? *osamp : *isamp);
  *isamp = 0;

  for(; len ; len--)
  {
    t_ibuf = *ibuf;
    for (chcnt = 0; chcnt < effp->in_signal.channels; chcnt++)
    {
      /* THIS IS OUR TEST */
      if ((checker->max_rms_sum == 0) || (is_louder(effp, compute_rms(effp, ibuf[chcnt]), checker->max_rms_sum, '%') == sox_true))
      {
        checker->max_rms_sum = compute_rms(effp, ibuf[chcnt]);
      }
    }
    *isamp += 1;
    ibuf++;
  }

  /* Outputting is the last `effect' in the effect chain so always passes
   * 0 samples on to the next effect (as there isn't one!) */
  *osamp = 0;
  return SOX_SUCCESS; /* All samples analyzed successfully */
}

static int output_stop(sox_effect_t * effp)
{
  priv_t * checker = (priv_t *) effp->priv;

  printf("stop: window=%g\nwindow_current=%g\nwindow_end=%g\n"
            "window_size=%" PRIuPTR "\nrms_sum=%g\n",
    checker->window, checker->window_current,
    checker->window_end, checker->window_size,
    checker->rms_sum
  );

  printf("Highest peak: %g\n", checker->max_rms_sum);
  return (SOX_SUCCESS);
}

/* COPIED FROM example1.c ... */
/* A `stub' effect handler to handle inputting samples to the effects
 * chain; the only function needed for this example is `drain' */
static sox_effect_handler_t const * input_handler(void)
{
  static sox_effect_handler_t handler = {
    "input", NULL, SOX_EFF_MCHAN, NULL, NULL, NULL, input_drain, NULL, NULL, sizeof(priv_t)
  };
  return &handler;
}

/* COPIED FROM example1.c ... */
/* A `stub' effect handler to handle outputting samples from the effects
 * chain; the only function needed for this example is `flow' */
static sox_effect_handler_t const * output_handler(void)
{
  static sox_effect_handler_t handler = {
    "output", NULL, SOX_EFF_MCHAN, NULL, output_handler_start, output_flow, NULL, output_stop, NULL, sizeof(priv_t)
  };
  return &handler;
}

/*
 * Adapted from example0.c in the libsox package
 */
int main(int argc, char * argv[])
{
  int sox_result, action, done = 0;
  sox_effects_chain_t * chain;
  sox_effect_t * e;
  TCHAR threshold[15];
  LPCTSTR pszThresholdFormat = TEXT("%s%%");
  char * args[10];

  if (argc < 3) {
    printf( "USAGE: tc [directory] [threshold]\n\n"
            " -directory  = the folder containing file(s) you want to trim.\n"
            " -threshold  = the minimum percentage of sound you want to keep.\n"
            "               Any trailing silence quieter than this will be discarded.\n"
            "               The %% sign is not required, but you MUST include\n"
            "               a decimal point and at least one digit after it.\n");
    exit(EXIT_SUCCESS);
  }

  /* All libSoX applications must start by initializing the SoX library */
  sox_result = sox_init();
  if (sox_result != SOX_SUCCESS)
  {
    report_error(SOX_LIB_ERROR, __LINE__, sox_result);
    exit(EXIT_FAILURE);
  }

  /* system("cls"); */
  GetCurrentDirectory(MAX_PATH, home_directory);
  SetCurrentDirectory(argv[1]);
  StringCchPrintf(threshold, 15, pszThresholdFormat, argv[2]);
  trim_silence(threshold);
  cleanup();
  return 0;
}

double duration_in_seconds(sox_format_t * source)
{
  double secs;
  uint64_t ws;

  ws = source->signal.length / max(source->signal.channels, 1);
  secs = (double)ws / max(source->signal.rate, 1);

  return secs;
}

void show_stats(sox_format_t * in)
{
  double secs;
  uint64_t ws;
  char const * text = NULL;

  ws = in->signal.length / max(in->signal.channels, 1);
  secs = (double)ws / max(in->signal.rate, 1);

  printf("TYPE: %s\nRATE (samples per second): %g\nCHANNELS: %u\n"\
            "SAMPLES: %" PRIu64 "\n"\
            "DURATION: %s\nDURATION (in seconds): %f\n"\
            "BITS PER SAMPLE: %u\nPRECISION: %u\n",
          in->filetype,
          in->signal.rate,
          in->signal.channels,
          ws,
          str_time(secs),
          secs,
          in->encoding.bits_per_sample,
          in->signal.precision);
  if (in->oob.comments) {
    tc_comments_t p = in->oob.comments;
    do printf("%s\n", *p); while (*++p);
  }

}

static double find_target()
{
  WIN32_FIND_DATA fdFile;
  HANDLE hFind = NULL;
  double secs, secs_after_trimming, total_duration = 0;
  uint64_t ws;
  int sox_result, max_silence = 0;
  sox_effects_chain_t * chain;
  sox_effect_t * e;
  sox_format_t * temp_file;
  char * target_file_name = NULL;
  char * args[10];

  if((hFind = FindFirstFile("*.wav", &fdFile)) == INVALID_HANDLE_VALUE)
  {
    printf("No files found.");
    return 0;
  }

  do
  {
    /* FindFirstFile will always return "." and ".."
     * as the first two directories.*/
    if(strcmp(fdFile.cFileName, ".") != 0
      && strcmp(fdFile.cFileName, "..") != 0)
    {
      /* Open the input file (with default parameters) */
      in = sox_open_read(fdFile.cFileName, NULL, NULL, NULL);
      if (in == NULL)
      {
        report_error(SOX_LIB_ERROR, __LINE__, sox_result);
        FindClose(hFind);
        return 0;
      }
      /* For testing... */
      show_stats(in);
      /* Get total duration of current audio file. */
      ws = in->signal.length / max(in->signal.channels, 1);
      secs = (double)ws / max(in->signal.rate, 1);
      total_duration += secs;

      /* Use the 'reverse' effect to get trailing silence of current audio file. */
      temp_file = sox_open_write("reverse.wav", &in->signal, NULL, NULL, NULL, NULL);
      if (temp_file == NULL)
      {
        report_error(SOX_LIB_ERROR, __LINE__, sox_result);
        FindClose(hFind);
        return 0;
      }
      chain = sox_create_effects_chain(&in->encoding, &temp_file->encoding);
      e = sox_create_effect(sox_find_effect("reverse"));
      args[0] = (char *)in, sox_effect_options(e, 0, args);
      sox_result = sox_add_effect(chain, e, &in->signal, &in->signal);
      if (sox_result != SOX_SUCCESS)
      {
        report_error(SOX_LIB_ERROR, __LINE__, sox_result);
        sox_close(temp_file);
        FindClose(hFind);
        return 0;
      }
      free(e);
      e = sox_create_effect(sox_find_effect("silence"));
      args[0] = "1";
      args[1] = SILENCE_DURATION;
      args[2] = SILENCE_THRESHOLD;
      sox_effect_options(e, 3, args);
      sox_result = sox_add_effect(chain, e, &temp_file->signal, &temp_file->signal);
      if (sox_result != SOX_SUCCESS)
      {
        report_error(SOX_LIB_ERROR, __LINE__, sox_result);
        sox_close(temp_file);
        FindClose(hFind);
        return 0;
      }
      free(e);

      /* Is it longer than max_silence? Then update that value,
       * and assign name of the audio file to *target_file_name.
       */
      ws = temp_file->signal.length / max(temp_file->signal.channels, 1);
      secs_after_trimming = (double)ws / max(temp_file->signal.rate, 1);
      if ((secs - secs_after_trimming) > max_silence)
      {
        max_silence = secs_after_trimming;
        target_file_name = fdFile.cFileName;
      }
      sox_close(temp_file);
      system("del reverse.wav");
      sox_close(in);
    }
    if ( target_file_name == NULL )
    {
      report_error(ENOENT, __LINE__, 0);
      return 0;
    }
  }
  while(FindNextFile(hFind, &fdFile)); /* Find the next file. */

  FindClose(hFind); /* Always clean up! */

  in = sox_open_read(target_file_name, NULL, NULL, NULL);
  return total_duration;
}

static void run_customized_effect(void)
{
  WIN32_FIND_DATA fdFile;
  HANDLE hFind = NULL;
  TCHAR szNewPath[MAX_PATH];
  unsigned long sample_count = 0L;
  sox_effects_chain_t * chain;
  sox_effect_t * e;
  int sox_result = SOX_SUCCESS;
  char * args[10];

  if((hFind = FindFirstFile("*.wav", &fdFile)) == INVALID_HANDLE_VALUE)
  {
    printf("No files found.\n");
    return;
  }

  do
  {
    /* FindFirstFile will always return "." and ".."
     * as the first two directories.*/
    if(strcmp(fdFile.cFileName, ".") != 0
      && strcmp(fdFile.cFileName, "..") != 0
      && strcmp(fdFile.cFileName, "temp.wav") != 0)
    {
      in = sox_open_read(fdFile.cFileName, NULL, NULL, NULL);
      if (in == NULL)
      {
        report_error(errno, __LINE__, 0);
        break;
      }
      show_stats(in);
      out = sox_open_write("temp.wav", &in->signal, NULL, NULL, NULL, NULL);
      if (out == NULL)
      {
        report_error(errno, __LINE__, 0);
        break;
      }
      chain = sox_create_effects_chain(&in->encoding, &out->encoding);
      e = sox_create_effect(sox_find_effect("input"));
      args[0] = (char *)in, assert(sox_effect_options(e, 1, args) == SOX_SUCCESS);
      assert(sox_add_effect(chain, e, &in->signal, &in->signal) == SOX_SUCCESS);
      free(e);
      e = sox_create_effect(sox_find_effect("reverse"));
      assert(sox_add_effect(chain, e, &in->signal, &in->signal) == SOX_SUCCESS);
      free(e);
      e = sox_create_effect(sox_find_effect("silence"));
      args[0] = "1";
      args[1] = "00:00:00.2";
      args[2] = "0.03%";
      assert(sox_effect_options(e, 3, args) == SOX_SUCCESS);
      sox_result = sox_add_effect(chain, e, &in->signal, &in->signal);
      if (sox_result != SOX_SUCCESS)
      {
        report_error(SOX_LIB_ERROR, __LINE__, sox_result);
        cleanup();
        break;
      }
      free(e);
      e = sox_create_effect(sox_find_effect("reverse"));
      assert(sox_add_effect(chain, e, &in->signal, &in->signal) == SOX_SUCCESS);
      free(e);
      e = sox_create_effect(sox_find_effect("output"));
      args[0] = (char *)out, assert(sox_effect_options(e, 1, args) == SOX_SUCCESS);
      assert(sox_add_effect(chain, e, &in->signal, &in->signal) == SOX_SUCCESS);
      free(e);

      /* Flow samples through the effects processing chain until EOF is reached */
      sox_flow_effects(chain, NULL, NULL);
      sox_delete_effects_chain(chain);
      sox_close(in);
      sox_close(out);
      StringCchPrintf(szNewPath, sizeof(szNewPath)/sizeof(szNewPath[0]), TEXT("%s"), fdFile.cFileName);
      CopyFile("temp.wav", szNewPath, FALSE);
      system("del temp.wav");
    }
  }
  while(FindNextFile(hFind, &fdFile)); /* Find the next file. */

  FindClose(hFind); /* Always clean up! */
}

static sox_bool is_louder(sox_effect_t const * effp,
                          sox_sample_t value /* >= 0 */,
                          double threshold,
                          int unit)
{
  sox_sample_t masked_value;
  /* When scaling low bit data, noise values got scaled way up */
  /* Only consider the original bits when looking for silence */
  masked_value = value & (-1 << (32 - effp->in_signal.precision));

  double scaled_value = (double)masked_value / SOX_SAMPLE_MAX;

  if(unit == '%')
    scaled_value *= 100;
  else if (unit == 'd')
    scaled_value = linear_to_dB(scaled_value);

  return scaled_value > threshold;
}
