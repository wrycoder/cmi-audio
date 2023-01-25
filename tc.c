/* TimeCheck
 *
 * (c) 2023 Michael Toulouse
 *
 */

#include "sox.h"
#include "tc.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

static void report_error(int errcode, int line_number, int sox_error) {
  printf("ERROR %d at line %d in source file %s\n ", errcode, line_number, __FILE__);
  printf("Error Number: %d\n ", errno);
  if (sox_error != SOX_SUCCESS) {
    printf("Error Description: %s\n", sox_strerror(sox_error));
  } else {
    printf("Error Description: %s\n", strerror(errno));
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
    printf("Please specify file(s) to process.");
    exit(EXIT_SUCCESS);
  }

  /* All libSoX applications must start by initializing the SoX library */
  sox_result = sox_init();
  if (sox_result != SOX_SUCCESS)
  {
    report_error(SOX_LIB_ERROR, __LINE__, sox_result);
    exit(EXIT_FAILURE);
  }

  /* Open the input file (with default parameters) */
  in = sox_open_read(argv[1], NULL, NULL, NULL);
  if (in == NULL)
  {
    report_error(SOX_LIB_ERROR, __LINE__, sox_result);
    sox_quit();
    exit(EXIT_FAILURE);
  }
  out = sox_open_write("result.wav", &in->signal, NULL, NULL, NULL, NULL);
  if (out == NULL)
  {
    report_error(SOX_LIB_ERROR, __LINE__, sox_result);
    sox_close(in);
    sox_quit();
    exit(EXIT_FAILURE);
  }
  system("cls");
  show_stats(in);
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
    sox_close(in);
    sox_close(out);
    sox_quit();
    exit(EXIT_FAILURE);
  }
  free(e);

  e = sox_create_effect(sox_find_effect("silence"));

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

  /* All done; tidy up: */
  sox_close(in);
  sox_close(out);
  sox_quit();
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
