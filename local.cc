/*
 * This file is part of hbackup.
 * Copyright (C) 2006, 2007 Richard Kettlewell
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

// Local Filesystem -----------------------------------------------------------

LocalFilesystem local;

LocalFile::LocalFile(const string &path_, OpenMode mode) :
  fd(-1), path(path_) {
  mode_t m;
  
  switch(mode) {
  case ReadOnly: m = O_RDONLY; break;
  case Overwrite: m = O_WRONLY|O_CREAT|O_TRUNC; break;
  case NoOverwrite: m = O_WRONLY|O_CREAT|O_EXCL; break;
  default: fatal("invalid open mode %d", (int)mode);
  }
  if((fd = open(path_.c_str(), m, 0666)) < 0)
    throw FileError("opening", path, errno);
  fprintf(stderr, "opened a localfile %s\n", path.c_str());
}

LocalFile::~LocalFile() {
  if(fd != -1 && close(fd) < 0) throw FileError("closing", path, errno);
}
 
int LocalFile::readbytes(void *buf, int space) {
  int n;
  if((n = read(fd, buf, space)) < 0) throw FileError("reading", path, errno);
  return n;
}

void LocalFile::writebytes(const void *buf, int nbytes) {
  int n, written = 0;

  fprintf(stderr,  "write %d bytes to %s\n", nbytes, path.c_str());
  while(written < nbytes) {
    if((n = write(fd, (const char *)buf + written, nbytes)) < 0)
      throw FileError("writing", path, errno);
    written += n;
  }
}

bool LocalFile::readable() const {
  fd_set fds;
  assert(mode != writing);
  if(next != top) return true;          // buffered data, can definitely read
  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  struct timeval tv = { 0, 0 };
  int n;
  do {
    n = select(fd + 1, &fds, 0, 0, &tv);
  } while(n < 0 && errno == EINTR);
  if(n < 0) fatal("select: %s", strerror(errno));
  return !!FD_ISSET(fd, &fds);
}

void LocalFilesystem::rename(const string &oldpath, const string &newpath) {
  if(::rename(oldpath.c_str(), newpath.c_str()) < 0)
    throw FileError("renaming", oldpath, errno);
}

void LocalFilesystem::link(const string &oldpath, const string &newpath) {
  if(::link(oldpath.c_str(), newpath.c_str()) < 0)
    throw FileError("linking", oldpath, errno);
}
  
void LocalFilesystem::remove(const string &path) {
  if(::remove(path.c_str()) < 0) 
    throw FileError("removing", path, errno);
}

File *LocalFilesystem::open(const string &path, OpenMode mode) {
  return new LocalFile(path, mode);
}

void LocalFilesystem::mkdir(const string &path, mode_t mode) {
  if(::mkdir(path.c_str(), mode) < 0)
    throw FileError("creating directory", path, errno);
}

void LocalFilesystem::mknod(const string &path, mode_t mode, dev_t dev) {
  if(::mknod(path.c_str(), mode, dev) < 0)
    throw FileError("creating device", path, errno);
}

void LocalFilesystem::lchown(const string &path, uid_t uid, gid_t gid) {
  if(::lchown(path.c_str(), uid, gid) < 0)
    throw FileError("changing ownership of", path, errno);
}

void LocalFilesystem::chmod(const string &path, mode_t mode) {
  if(::chmod(path.c_str(), mode) < 0)
    throw FileError("changing permissions of", path, errno);
}

void LocalFilesystem::symlink(const string &target, const string &path) {
  if(::symlink(target.c_str(), path.c_str()) < 0)
    throw FileError("creating symlink", path, errno);
}

void LocalFilesystem::utimes(const string &path, time_t atime, time_t mtime) {
  struct timeval times[2];
  
  times[0].tv_sec = atime;
  times[0].tv_usec = 0;
  times[1].tv_sec = mtime;
  times[1].tv_usec = 0;
  if(::utimes(path.c_str(), times) < 0)
     throw FileError("setting file times", path, errno);
}

int LocalFilesystem::exists(const string &path) {
  struct stat sb;

  if(stat(path.c_str(), &sb) < 0) {
    if(errno != ENOENT)
      throw FileError("stat", path, errno);
    return 0;
  }
  return 1;
}

void LocalFilesystem::contents(const string &path,
			       list<string> &c) {
  DIR *dp;
  struct dirent *de;

  c.clear();
  if(!(dp = opendir(path.c_str())))
    throw FileError("opening directory", path, errno);
  try {
    errno = 0;
    while((de = readdir(dp))) {
      string s = de->d_name;
      if(s != "." && s != "..")
	c.push_back(string(s));
      errno = 0;
    }
    if(errno) throw FileError("reading directory", path, errno);
  } catch(...) {
    closedir(dp);
    throw;
  }
  if(closedir(dp) < 0) throw FileError("closing directory", path, errno);
}

string LocalFilesystem::readlink(const string &path) {
  char buffer[MAXLINKSIZE];

  int n = ::readlink(path.c_str(), buffer, sizeof buffer);
  if(n < 0)
    throw FileError("reading link", path, errno);
  if((unsigned)n >= sizeof buffer)
    throw FileError("reading link", path, ENAMETOOLONG);
  buffer[n] = 0;
  return buffer;
}

bool LocalFilesystem::ismount(const string &path) {
  struct stat sb, psb;
  const string parent = path + "/..";

  if(stat(path.c_str(), &sb) < 0) throw FileError("stat", path, errno);
  if(!S_ISDIR(sb.st_mode)) return false;
  if(stat(parent.c_str(), &psb) < 0) throw FileError("stat", parent, errno);
  return sb.st_dev != psb.st_dev;
}

Filetype LocalFilesystem::type(const string &path) {
  struct stat sb;

  if(lstat(path.c_str(), &sb) < 0)
    throw FileError("lstat", path, errno);
  if(S_ISREG(sb.st_mode)) return RegularFile;
  else if(S_ISDIR(sb.st_mode)) return Directory;
  else if(S_ISLNK(sb.st_mode)) return SymbolicLink;
  else return UnknownFileType;
}

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
