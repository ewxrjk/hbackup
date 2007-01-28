/*
 * This file is part of hbackup.
 * Copyright (C) 2007 Richard Kettlewell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include "nhbackup.h"

static void hexprint(const string &s) {
  size_t n;

  for(n = 0; n < s.size(); ++n)
    printf(" %02x", (unsigned char)s[n]);
  putchar('\n');
}

int main(int argc, char **argv) {
  char buffer[1024];
  assert(argc == 3);
  Recode r(argv[1], argv[2]);

  while(fgets(buffer, sizeof buffer, stdin)) {
    const string s(buffer, strlen(buffer) - 1);
    const string t = r.convert(s);
    printf("FROM:"); hexprint(s);
    printf("TO:  "); hexprint(t);
    printf("\n");
  }
}

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
/* arch-tag:++BPPAHPlrqXAMCss7YJhQ */
