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

// Exclusion ------------------------------------------------------------------

Exclusion::Exclusion(const char *s) : re(0), extra(0) {
  const char *error;
  int erroffset;

  if(!(re = pcre_compile(s,
			 PCRE_DOLLAR_ENDONLY,
			 &error, &erroffset, 0)))
    fatal("error compiling regular expression '%s': %s", s, error);
  extra = pcre_study(re, 0, &error);
  if(error) fatal("error studying regular expression '%s': %s", s, error);
}

bool Exclusion::matches(const char *s) const {
  int rc;

  if((rc = pcre_exec(re, extra, s, strlen(s), 0,
		     0/*options*/, 0, 0)) < 0)
    switch(rc) {
    case PCRE_ERROR_NOMATCH: return false;
    default: fatal("error from pcre_exec: %d", rc);
      // pcre doesn't provide a code-to-string mapping for some reason
    }
  return true;
}

void Exclusions::add(const char *s) {
  exclusions.push_back(s);
}

bool Exclusions::excluded(const string &path) const {
  const char *p = path.c_str();
  for(list<Exclusion>::const_iterator it = exclusions.begin();
      it != exclusions.end();
      ++it)
    if(it->matches(p))
      return true;
  return false;
}

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
