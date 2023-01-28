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
#include <stdio.h>
#include <assert.h>

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
  static char string[16][50];
  static int i;
  int hours, mins = seconds / 60;
  seconds -= mins * 60;
  hours = mins / 60;
  mins -= hours * 60;
  i = (i+1) & 15;
  sprintf(string[i], "%02i:%02i:%05.2f", hours, mins, seconds);
  return string[i];
}

static double soxi_total;
static sox_format_t * in, * out;
static double find_target();

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

/*
 * Adapted from example0.c in the libsox package
 */
int main(int argc, char * argv[])
{
  int sox_result, action, done = 0;
  sox_effects_chain_t * chain;
  sox_effect_t * e;
  char * args[10];

  if (argc < 2) {
    printf("Please specify a target directory.");
    exit(EXIT_SUCCESS);
  }

  /* All libSoX applications must start by initializing the SoX library */
  sox_result = sox_init();
  if (sox_result != SOX_SUCCESS)
  {
    report_error(SOX_LIB_ERROR, __LINE__, sox_result);
    exit(EXIT_FAILURE);
  }

  system("cls");
  GetCurrentDirectory(MAX_PATH, home_directory);
  SetCurrentDirectory(argv[1]);

  double total_duration = find_target();
  if (total_duration == 0)
  {
    cleanup();
    exit(EXIT_FAILURE);
  }

  out = sox_open_write("result.wav", &in->signal, NULL, NULL, NULL, NULL);
  if (out == NULL)
  {
    report_error(SOX_LIB_ERROR, __LINE__, sox_result);
    cleanup();
    exit(EXIT_FAILURE);
  }
  chain = sox_create_effects_chain(&in->encoding, &out->encoding);

  /* The first effect in the effect chain must be something that can source
   * samples; in this case, we use the built-in handler that inputs data
   * from an audio file */
  e = sox_create_effect(sox_find_effect("input"));
  args[0] = (char *)in, sox_effect_options(e, 1, args);
  
  /* This becomes the first 'effect' in the chain */
  sox_result = sox_add_effect(chain, e, &in->signal, &in->signal);
  if (sox_result != SOX_SUCCESS)
  {
    report_error(SOX_LIB_ERROR, __LINE__, sox_result);
    cleanup();
    exit(EXIT_FAILURE);
  }
  free(e);

  /* The last effect in the effect chain must be something that only consumes
   * samples. We will use the built-in handler that outputs data to
   * an audio file. */
  e = sox_create_effect(sox_find_effect("output"));
  args[0] = (char *)out, sox_effect_options(e, 1, args);

  sox_result = sox_add_effect(chain, e, &in->signal, &in->signal);
  if (sox_result != SOX_SUCCESS)
  {
    report_error(SOX_LIB_ERROR, __LINE__, sox_result);
    cleanup();
    exit(EXIT_FAILURE);
  }
  free(e);

  printf("Total duration: %s\n", str_time(total_duration));
  do {
    action = main_menu();
    switch ( action ) {
      case quit:
        done = 1;
        break;
      case half_sec:
        break;
      case whole_sec:
        break;
    }
    if (done == 1) break;

  } while (done == 0);

  cleanup();
  return 0;
}

void show_stats(sox_format_t * in)
{
  double secs;
  uint64_t ws;
  char const * text = NULL;

  ws = in->signal.length / max(in->signal.channels, 1);
  secs = (double)ws / max(in->signal.rate, 1);

  /* Clear the screen */
  printf("\e[1;1H\e[2J");
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
