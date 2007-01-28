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

static const char hexdigits[] = "0123456789abcdef";

// Utilities ------------------------------------------------------------------

void (*exitfn)(int) = exit;

void fatal(const char *msg, ...) {
  va_list ap;
  fprintf(stderr, "FATAL: ");
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fputc('\n', stderr);
  exitfn(1);
  // ..had better not return
  abort();
}

void warning(const char *msg, ...) {
  va_list ap;
  fprintf(stderr, "WARNING: ");
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fputc('\n', stderr);
  ++warnings;
}

void error(const char *msg, ...) {
  va_list ap;
  fprintf(stderr, "ERROR: ");
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fputc('\n', stderr);
  ++errors;
}

static map<uid_t, string> uid2names;
static map<gid_t, string> gid2names;

string uid2string(uid_t uid) {
  const map<uid_t, string>::const_iterator it = uid2names.find(uid);
  if(it != uid2names.end()) return it->second;
  const struct passwd *const pw = getpwuid(uid);
  if(pw)
    uid2names[uid] = pw->pw_name;
  else {
    char buffer[64];

    warning("no name for UID %lu", (unsigned long)uid);
    snprintf(buffer, sizeof buffer, "%lu", (unsigned long)uid);
    uid2names[uid] = buffer;
  }
  return uid2names[uid];
}

string gid2string(gid_t gid) {
  const map<gid_t, string>::const_iterator it = gid2names.find(gid);
  if(it != gid2names.end()) return it->second;
  const struct group *const gr = getgrgid(gid);
  if(gr)
    gid2names[gid] = gr->gr_name;
  else {
    char buffer[64];

    warning("no name for GID %lu", (unsigned long)gid);
    snprintf(buffer, sizeof buffer, "%lu", (unsigned long)gid);
    gid2names[gid] = buffer;
  }
  return gid2names[gid];
}

static map<string, uid_t> names2uid;
static map<string, gid_t> names2gid;

static bool alldigits(const string &s) {
  const size_t l = s.size();
  if(!l) return false;
  for(size_t n = 0; n < l; ++n)
    if(!isdigit((unsigned char)s[n]))
      return false;
  return true;
}

uid_t string2uid(const string &s) {
  const map<string, uid_t>::const_iterator it = names2uid.find(s);
  if(it != names2uid.end()) return it->second;
  if(!alldigits(s)) {
    const struct passwd *const pw = getpwnam(s.c_str());
    if(!pw) fatal("no UID with name %s", s.c_str());
    names2uid[s] = pw->pw_uid;
  } else
    names2uid[s] = atol(s.c_str());
  return names2uid[s];
}

gid_t string2gid(const string &s) {
  const map<string, gid_t>::const_iterator it = names2gid.find(s);
  if(it != names2gid.end()) return it->second;
  if(!alldigits(s)) {
    const struct group *const gr = getgrnam(s.c_str());
    if(!gr) fatal("no GID with name %s", s.c_str());
    names2gid[s] = gr->gr_gid;
  } else
    names2gid[s] = atol(s.c_str());
  return names2gid[s];
}

static inline int hexdigit(unsigned char c) {
  static const signed char lookup[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  };
  const int decoded = lookup[c];
  if(decoded >= 0) return decoded;
  else throw BadHexDigit();
}

string hexencode(const uint8_t *h, size_t len) {
  string s;

  s.reserve(len * 2);
  for(size_t n = 0; n < len; ++n) {
    s += hexdigits[h[n] >> 4];
    s += hexdigits[h[n] & 0x0F];
  }
  return s;
}

// url-encode a string
string urlencode(const string &s) {
  string r;
  size_t n = 0;
  const size_t limit = s.size();
  unsigned char c;

  r.reserve(3 * limit);
  while(n < limit) {
    switch(c = s[n++]) {
    case ' ':
      r += '+';
      break;
    default:
      if(c < 32 || c > 126) {
      case '+':
      case '%':
      case '&':
      case '=':
      case ';':
        // ; because python's urldecoder splits on it(!)
        r += '%';
        r += hexdigits[c >> 4];
        r += hexdigits[c & 0x0F];
        break;
      } else {
        r += c;
      }
      break;
    }
  }
  return r;
}

// url-decode a string
string urldecode(const string &s, 
                 size_t start,
                 size_t end) {
  string r;
  size_t n = start;
  const size_t limit = end == string::npos ? s.size() : end;
  
  try {
    r.reserve(limit - start);
    while(n < limit) {
      switch(s[n]) {
      case '+':
        r += ' ';
        ++n; 
        break;
      case '%':
        if(n + 2 < end) {
          r += (char)(16 * hexdigit(s[n + 1]) + hexdigit(s[n + 2]));
          n += 3;
        } else
          throw BadHex(s.substr(n, limit - n));
        break;
      default:
        r += s[n];
        ++n;
        break;
      }
    }
    return r;
  } catch(...) {
    error("invalid URL-encoded string: %s", s.c_str());
    throw;
  }
}

void hexdecode(const string &hex, string &bytes) {
  const size_t len = hex.size();

  try {
    if(len % 2 != 0)
      throw BadHex(hex);
    const size_t nbytes = len / 2;
    bytes.reserve(nbytes);
    const char *in = &hex[0];
    for(size_t n = nbytes; n > 0; --n) {
      const int a = hexdigit(*in++) * 16;
      bytes += (char)(a + hexdigit(*in++));
    }
  } catch(...) {
    // if something went wrong report the whole string
    error("invalid hex string: %s", hex.c_str());
    throw;
  }
}

void hashdecode(const string &hex, uint8_t h[HASH_SIZE]) {
  string s;

  if(hex.size() != 40)
    throw BadHex(hex);
  hexdecode(hex, s);
  memcpy(h, s.data(), HASH_SIZE);
}

// Return the path for H
string hashpath(const uint8_t *h) {
  char buffer[3 * DEPTH + 2 * HASH_SIZE + 1];

  for(int n = 0; n < DEPTH; ++n)
    snprintf(buffer + 3 * n, 4, "%02x/", h[n]);
  for(int n = 0; n < HASH_SIZE; ++n)
    snprintf(buffer + 3 * DEPTH + 2 * n, 3, "%02x", h[n]);
  return buffer;
}

// Parse an index line.
int parseIndexLine(const string &line, map<string,string> &l) {
  size_t n, sep, pos = 0;

  l.clear();
  do {
    sep = line.find('=', pos);
    n = line.find('&', pos);
    if(sep == string::npos || (n != string::npos && sep > n))
      throw BadIndexFile(line);
    l[urldecode(line, pos, sep)] = urldecode(line, sep + 1, n);
    pos = n + 1;
  } while(n != string::npos);

  return 1;
}

// Read an index line.  Return 1 on success, 0 at eof.
int readIndexLine(File *f, map<string,string> &l) {
  string line;

  if(!f->getline(line)) throw BadIndexFile("unexpected end of file");
  if(line == "[end]") return 0;
  return parseIndexLine(line, l);
}

/* Return a pointer to a detail value, or 0 if it's not there */
const string *getdetail(const map<string,string> &details,
                        const char *key) {
  map<string,string>::const_iterator it = details.find(key);

  return it != details.end() ? &it->second : 0;
}


/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
/* arch-tag:yiEOLSYXJuEUV4LUZEUSvw */
