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
  LPCTSTR pszFormatWithHours = TEXT("%02i:%02i:%05.2f");
  LPCTSTR pszFormat = TEXT("%02i:%05.2f");
  int hours, mins = seconds / 60;
  seconds -= mins * 60;
  hours = mins / 60;
  mins -= hours * 60;
  i = (i+1) & 15;
  if (hours > 0)
  {
    StringCchPrintf(string[i], cchDest, pszFormatWithHours, hours, mins, seconds);
  } else {
    StringCchPrintf(string[i], cchDest, pszFormat, mins, seconds);
  }
  return string[i];
}

static double soxi_total;
static sox_format_t * in, * out;
static void trim_silence(char *);
static void show_timings();
void show_name_and_runtime(sox_format_t * in);
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
  STRSAFE_LPSTR sox_wildcard = "libSoX.tmp*"; 
  const int temp_path_length = MAX_PATH + strlen(sox_wildcard) + 1;
  TCHAR szTempFileWildcard[temp_path_length];
  TCHAR szCurrentTempFileName[temp_path_length];
  WIN32_FIND_DATA fdFile;
  HANDLE hFind = NULL;

  if (in != NULL) sox_close(in);
  if (out != NULL) sox_close(out);
  sox_quit();
  GetTempPathA(MAX_PATH, szTempFileWildcard);
  StringCbCatA(szTempFileWildcard, temp_path_length, sox_wildcard); 
  if((hFind = FindFirstFile(szTempFileWildcard, &fdFile)) != INVALID_HANDLE_VALUE)
  {
    do
    {
      /* FindFirstFile will always return "." and ".."
      * as the first two directories.*/
      if(strcmp(fdFile.cFileName, ".") != 0
        && strcmp(fdFile.cFileName, "..") != 0)
      {
        GetTempPathA(MAX_PATH, szCurrentTempFileName);
        StringCbCatA(szCurrentTempFileName, temp_path_length, fdFile.cFileName);
        DeleteFileA(szCurrentTempFileName);
      }
    }
    while(FindNextFile(hFind, &fdFile)); /* Find the next file. */
  }
  SetCurrentDirectory(home_directory);
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
  show_timings();
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

void show_name_and_runtime(sox_format_t * in)
{
  double secs;
  uint64_t ws;

  ws = in->signal.length / max(in->signal.channels, 1);
  secs = (double)ws / max(in->signal.rate, 1);

  printf_s("FILE: %s: \t\t\t%-15.15s\n", in->filename, str_time(secs));
}

void show_stats(sox_format_t * in)
{
  double secs;
  uint64_t ws;

  ws = in->signal.length / max(in->signal.channels, 1);
  secs = (double)ws / max(in->signal.rate, 1);

  printf("TYPE: %s\nRATE (samples per second): %g\nCHANNELS: %u\n"\
            "SAMPLES: %" PRIu64 "\n"\
            "DURATION: %s\n"\
            "BITS PER SAMPLE: %u\nPRECISION: %u\n",
          in->filetype,
          in->signal.rate,
          in->signal.channels,
          ws,
          str_time(secs),
          in->encoding.bits_per_sample,
          in->signal.precision);
  if (in->oob.comments) {
    tc_comments_t p = in->oob.comments;
    do printf("%s\n", *p); while (*++p);
  }

}

static void trim_silence(char * threshold)
{
  WIN32_FIND_DATA fdFile;
  HANDLE hFind = NULL;
  TCHAR szNewPath[MAX_PATH];
  unsigned long sample_count = 0L;
  double total_duration_before = 0, total_duration_after = 0;
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
      show_name_and_runtime(in);
      total_duration_before += duration_in_seconds(in);
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
      args[2] = threshold;
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
      sox_close(out);
      out = sox_open_read("temp.wav", NULL, NULL, NULL);
      total_duration_after += duration_in_seconds(out);
      sox_close(in);
      sox_close(out);
      StringCchPrintf(szNewPath, sizeof(szNewPath)/sizeof(szNewPath[0]), TEXT("%s"), fdFile.cFileName);
      CopyFile("temp.wav", szNewPath, FALSE);
      system("del temp.wav");
    }
  }
  while(FindNextFile(hFind, &fdFile)); /* Find the next file. */

  printf( "RESULTS...\n"
          "Total duration: %s\n"
          "Silence removed: %s\n",
          str_time(total_duration_after), str_time(total_duration_before - total_duration_after));

  FindClose(hFind); /* Always clean up! */
}

static void show_timings()
{
  WIN32_FIND_DATA fdFile;
  HANDLE hFind = NULL;
  TCHAR szNewPath[MAX_PATH];
  unsigned long sample_count = 0L;
  double duration = 0;
  sox_effects_chain_t * chain;
  int sox_result = SOX_SUCCESS;
  if((hFind = FindFirstFile("*.wav", &fdFile)) == INVALID_HANDLE_VALUE)
  {
    printf("No files found.\n");
    return;
  }
  do {
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
      show_name_and_runtime(in);
      sox_close(in);
    }
  }
  while(FindNextFile(hFind, &fdFile)); /* Find the next file. */
  FindClose(hFind); /* Always clean up! */
}

