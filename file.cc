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

int File::fill() {
  assert(mode != writing);
  mode = reading;
  if(!eof) {
    int n = readbytes(buffer, sizeof buffer);
    next = buffer;
    top = buffer + n;
    if(!n) eof = true;
    return n;
  }
  return 0;
}

File::~File() {
  if(mode == writing) flush();
}
  
int File::readbytes(void */*buf*/, int /*space*/) {
  throw WriteOnlyFile();
}

void File::writebytes(const void */*buf*/, int /*nbytes*/) {
  throw ReadOnlyFile();
}

void File::synchronize() {
}

int File::getline(string &r) {
  int c;
  
  r.clear();
  while((c = getch()) != '\n' && c != EOF)
    r += (char)c;
  return c != EOF || r.size() != 0;
}

int File::getbytes(void *dst, int max, bool all) {
  if(mode != reading)
    if(!fill())
      return 0;
  int total = 0;
  do {
    if(next == top && max < (int)sizeof buffer && !eof) {
      // The buffer is empty, and we're doing a small read.  Arrange for the
      // buffer not to be empty.
      if(!fill())
        break;                          // eof
    }
    int n;                              // will be # of bytes read
    if(next < top) {
      // there's data left in the buffer
      n = max > top - next ? top - next : max;
      memcpy(dst, next, n);
      next += n;
    } else {
      // bypass buffering for large reads
      n = readbytes(dst, max);
    }
    if(!n) eof = 1;
    max -= n;
    total += n;
    dst = (char *)dst + n;
  } while(all && !eof && max > 0);
  return total;
}

int File::getbytes(string &s, int n, bool all) {
  s.resize(n);                          // make space
  n = getbytes(&s[0], n, all);
  s.resize(n);                          // truncate if short answer
#if 0
  fprintf(stderr, "getbytes:");
  for(int i = 0; i < n; ++i)
    fprintf(stderr, " %02x", (uint8_t)s[i]);
  fputc('\n', stderr);
#endif
  return n;
} 

void File::put(const char *s, size_t len) {
  // Not very efficient, but simple
  while(len-- > 0)
    put(*s++);
}

void File::putf(const char *fmt, ...) {
  va_list ap;
  char *s;
  int n;
  
  va_start(ap, fmt);
  if((n = vasprintf(&s, fmt, ap)) < 0)
    fatal("error calling vasprintf: %s", strerror(errno));
  va_end(ap);
  try {
    put(s, n);
  } catch(...) {
    free(s);
    throw;
  }
  free(s);
}

void File::flush() {
  assert(mode != reading);
  if(mode == writing) {
    if(next != buffer)
      writebytes(buffer, next - buffer);
  } else
    mode = writing;
  next = buffer;
  top = buffer + sizeof buffer;
  synchronize();
}

void Filesystem::link(const string &path, const string &) {
  throw FileError("linking", path, ENOSYS);
}

void Filesystem::mknod(const string &path, mode_t, dev_t) {
  throw FileError("creating device", path, ENOSYS);
}

void Filesystem::lchown(const string &path, uid_t, gid_t) {
  throw FileError("changing ownership of", path, ENOSYS);
}

void Filesystem::chmod(const string &path, mode_t) {
  throw FileError("changing permissions of", path, ENOSYS);
}

void Filesystem::symlink(const string &, const string &path) {
  throw FileError("creating symlink", path, ENOSYS);
}

void Filesystem::utimes(const string &path, time_t, time_t) {
  throw FileError("setting file times", path, ENOSYS);
}

string Filesystem::readlink(const string &path) {
  throw FileError("reading symlink", path, ENOSYS);
}
 
bool Filesystem::ismount(const string &path) {
  throw FileError("checking mount point", path, ENOSYS);
}

void Filesystem::makedirs(const string &path) {
  string::size_type n;
  
  if(exists(path)) return;              // already exists
  for(n = 1; n < path.size(); ++n) {
    if(path[n] == '/') {
      string prefix(path, 0, n);
      if(!exists(prefix))
	mkdir(path.substr(0, n));
    }
  }
  mkdir(path);
}

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
