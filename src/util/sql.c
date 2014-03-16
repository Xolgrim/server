/*
Copyright (c) 1998-2010, Enno Rehling <enno@eressea.de>
                         Katja Zedel <katze@felidae.kn-bremen.de
                         Christian Schlittchen <corwin@amber.kn-bremen.de>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
**/

#include <platform.h>
#include "sql.h"

#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *sqlstream = NULL;
static char *sqlfilename = NULL;

void sql_init(const char *filename)
{
  if (sqlfilename != NULL)
    free(sqlfilename);
  sqlfilename = _strdup(filename);
}

void _sql_print(const char *format, ...)
{
  if (!sqlstream && sqlfilename) {
    sqlstream = fopen(sqlfilename, "wt+");
    free(sqlfilename);
    sqlfilename = NULL;
  }
  if (sqlstream != NULL) {
    va_list marker;
    va_start(marker, format);
    vfprintf(sqlstream, format, marker);
    va_end(marker);
  }
}

void sql_done(void)
{
  if (sqlstream)
    fclose(sqlstream);
  if (sqlfilename)
    free(sqlfilename);
  sqlstream = NULL;
  sqlfilename = NULL;
}

const char *sqlquote(const char *str)
{
#define BUFFERS 4
#define BUFSIZE 1024
  static char sqlstring[BUFSIZE * BUFFERS];     /* STATIC_RESULT: used for return, not across calls */
  static int index = 0;         /* STATIC_XCALL: used across calls */
  char *start = sqlstring + index * BUFSIZE;
  char *o = start;
  const char *i = str;
  while (*i && o - start < BUFSIZE - 1) {
    if (*i != '\'' && *i != '\"') {
      *o++ = *i++;
    } else
      ++i;
  }
  *o = '\0';
  index = (index + 1) % BUFFERS;
  return start;
}
