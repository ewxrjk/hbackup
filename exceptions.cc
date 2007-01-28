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

FileError::FileError(const char *doing,
		     const string &path,
		     int errno_value) : e(errno_value) {
  snprintf(w, sizeof w, "%s %s: %s", doing, path.c_str(), strerror(errno_value));
}

const char *FileError::what() const throw() {
  return w;
}

const char *ReadOnlyFile::what() const throw() { 
  return "write to read-only file";
}

const char *WriteOnlyFile::what() const throw() {
  return "read from write-only file";
}

BadHex::BadHex(const string &s) {
  snprintf(w, sizeof w, "invalid hex value: %s", s.c_str());
}

const char *BadHex::what() const throw() { 
  return w;
}

const char *BadHexDigit::what() const throw() {
  return "invalid hex digit";
}

const char *HashError::what() const throw() {
  switch(e) {
  case shaSuccess: return "OK";
  case shaNull: return "Null pointer parameter to SHA1 routine";
  case shaInputTooLong: return "SHA1 input data too long";
  case shaStateError: return "called SHA1Input after SHA1Result";
  default: abort();
  }
}

BadIndexFile::BadIndexFile(const string &s) {
  snprintf(w, sizeof w, "bad index file: %s", s.c_str());
}

const char *BadIndexFile::what() const throw() { 
  return w;
}

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
