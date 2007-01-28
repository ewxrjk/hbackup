/*
 * This file is part of hbackup.
 * Copyright (C) 2006 Richard Kettlewell
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

#define begin(WHAT, N) do { const int count = N; timeval start, end; const char *what = #WHAT; gettimeofday(&start, 0); for(int n = 0; n < count; ++n) {
#define end() } gettimeofday(&end, 0); report(&start, &end, what, count); } while(0)

static void report(const timeval *start,
                   const timeval *end,
                   const char *what,
                   int count) {
  const double s = start->tv_sec + start->tv_usec / 1.0E6;
  const double e = end->tv_sec + end->tv_usec / 1.0E6;
  printf("%s: %g\n", what, (e - s) / count);
}

void do_speedtest() {
  {
    map<string,string> l;
    const string s = "sha1=b35b20b250f470eca9bd7e41821687233d366b40&name=share%2Fzoneinfo%2Fright%2FZulu&perms=0644&gid=root&mtime=1143984233&uid=root&atime=1145439807&inode=464592&ctime=1145439831";
    begin(parseIndexLine, 100000);
    parseIndexLine(s, l);
    end();
  }
  {
    static const uint8_t bytes[40] = {};
    begin(hexencode, 1000000);
    hexencode(bytes, sizeof bytes);
    end();
  }
  {
    static const string s1 = "b35b20b250f470eca9bd7e41821687233d366b40";
    begin(urlencode1, 1000000);
    urlencode(s1);
    end();
  }    
  {
    static const string s2 = "\xb3\x5b\x20\xb2\x50\xf4\x70\xec\xa9\xbd\x7e\x41\x82\x16\x87\x23\x3d\x36\x6b\x40";
    begin(urlencode2, 1000000);
    urlencode(s2);
    end();
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
