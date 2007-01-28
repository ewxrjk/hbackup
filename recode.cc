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

Recode::Recode(const char *to, const char *from) {
  if(!from && !to)
    cd = 0;
  else if(!from || !to)
    fatal("incomplete encoding version specification");
  else if(!(cd = iconv_open(from, to)))
    fatal("error calling iconv_open(%s,%s)", from, to);
}

Recode::~Recode() {
  if(cd) iconv_close(cd);
}

string Recode::convert(const string &s) {
  if(!cd) return s;                     // not converting

  const char *inbuf = s.data();
  size_t inbytesleft = s.size();
  string r;
  char buffer[1024];
  
  iconv(cd, 0, 0, 0, 0);                // return to initial state
  while(inbytesleft) {
    char *outbuf = buffer;
    size_t outbytesleft = sizeof buffer;
    
    // convert some bytes
    const size_t n = iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
    // append whatever we got
    r.append(buffer, sizeof buffer - outbytesleft);
    if(n == (size_t)-1) {
      switch(errno) {
      case EILSEQ:                      // Invalid multibyte sequence found
      case EINVAL:                      // Incomplete multibyte sequence found
        r += '?';
        ++inbuf;
        --inbytesleft;
        break;
      case E2BIG:                       // Output buffer full
        break;
      default:                          // Undocumented error
        fatal("error calling iconv: %s", strerror(errno));
      }
    }
  }
  return r;
}

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
/* arch-tag:QP6EtRFCh6JbwD+p5P6cdA */
