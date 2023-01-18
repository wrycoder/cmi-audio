/* Simple example of using SoX libraries
 *
 * Copyright (c) 2007-8 robs@users.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "sox.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

/*
 * Reads input file, applies vol & flanger effects, stores in output file.
 * E.g. example1 monkey.au monkey.aiff
 */
int main(int argc, char * argv[])
{
  static sox_format_t * in, * out; /* input and output files */
  sox_effects_chain_t * chain;
  sox_effect_t * e;
  char * args[10];
  assert(argc == 3);
  assert(sox_init() == SOX_SUCCESS);
  return 0;
}


