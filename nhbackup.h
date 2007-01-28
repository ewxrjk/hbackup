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
#ifndef NHBACKUP_H
#define NHBACKUP_H

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>
#include <dirent.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <pcre.h>
#include <assert.h>
#include <stdarg.h>
#include <iconv.h>

#ifndef _WIN32
#include <sys/mman.h>
#include <sys/socket.h>
#include <pwd.h>
#include <grp.h>
#endif

#include <exception>
#include <string>
#include <list>
#include <map>
#include <set>

#include "sha1.h"

using namespace std;

// Configuration --------------------------------------------------------------

// These things have to be the same for everything that access a given repo:

// Details of the hash algorithm use.
#define HASH_NAME "sha1"                // had better be urlencode-idempotent
#define HASH_SIZE 20                    // size of a hash

// How deep to make repository directories.  DEPTH=2 means you get filenames of
// the form REPO/HASH_NAME/aa/bb/aabbccddee..., where aabbccddee... is the hash
// of the file.
#define DEPTH 2


// These things can be adjusted to taste without worrying about repo
// compatibility:

// Maximum size of a symlink we'll cope with.  Supporting arbitrary sizes would
// be nicer but realistically this is going to usually be enough.
#define MAXLINKSIZE 8192

// Size of the hash table.  1<<22 blows 16MB for the hash table on a 32-bit
// machine, which isn't much these days, and should only start to degrade
// performance with millions of files.  You might want to make it smaller on
// small machines.
#define HASHTABLE_SIZE (1 << 22)

// Minimum file size to mmap.  Mapping lots of small files is (at least on some
// platforms) measurably slower than just reading them.
#define MINMAP (256 * 1024)

// Maximum chunk size to mmap.  Had better fit into your address space.  On
// 32-bit machines 256Mbyte seems a reasonable value; on a 64-bit machine you
// could probably make it much bigger, though the benefit is not likely to be
// large unless mmap/munmap pairs are comparably expensive to hashing 256Mbyte.
#define MAXMAP (256 * 1024 * 1024)

// Recording ------------------------------------------------------------------

extern unsigned long long total_regular_files, total_dirs, total_links, total_devs;
extern unsigned long long total_socks, total_hardlinks;
extern unsigned long long unknown_files;
extern unsigned long long new_hashes;
extern unsigned long long hash_mmap;
extern unsigned long long hash_read;
extern unsigned long long small_files;

extern unsigned long long errors;       // error count
extern unsigned long long warnings;     // warning count

// Exceptions -----------------------------------------------------------------

class FileError : public exception {
protected:
  char w[1024];
  int e;
public:
  FileError(const char *doing,
            const string &path,
            int errno_value);
  const char *what() const throw();
  inline int error() const throw() { return e; }
};

class SftpFileError : public FileError {
public:
  SftpFileError(const char *doing,
                const string &path,
                const string &message,
                uint32_t status);
  unsigned long status;
};

class ReadOnlyFile : public exception {
public:
  inline ReadOnlyFile() {}
  const char *what() const throw();
};

class WriteOnlyFile : public exception {
public:
  inline WriteOnlyFile() {}
  const char *what() const throw();;
};

class BadHex : public exception {
  char w[1024];

public:
  BadHex(const string &s);
  const char *what() const throw();
};

class BadHexDigit : public exception {
public:
  inline BadHexDigit() {}
  const char *what() const throw();
};

class HashError : public exception {
private:
  int e;                                // errors from sha1.h
public:
  inline HashError(int e_): e(e_) {}
  const char *what() const throw();
};

class BadIndexFile: public exception {
  char w[1024];
public:
  BadIndexFile(const string &s);
  const char *what() const throw();
};

// Files ----------------------------------------------------------------------

// Generic file
class File {
protected:
  unsigned char *next, *top;
  enum {
    none,                               // next = top = 0
    reading,                            // top = end of read bytes
    writing                             // top = end of buffer
  } mode;
  bool eof;
  unsigned char buffer[4096];

  int fill();
  
  virtual int readbytes(void *buf, int space);
  // Read up to SPACE bytes.  Return actual number read.

  virtual void writebytes(const void *buf, int nbytes);
  // Write NBYTES.  Errors might be deferred until you call synchronize().

  virtual void synchronize();
  // Wait for any pending writes to complete and report any errors they
  // produce.  Required for efficient remote operation.

public:
  inline File() : next(0), top(0), mode(none), eof(0) {}

  virtual ~File();

  // Get one character, or EOF at end of file
  inline int getch() {
    if(next == top)
      if(!fill()) return EOF;
    return *next++;
  }

  // Get a line.  Returns 1 on success, 0 at end of file. 
  int getline(string &r);

  // Read (up to) MAX bytes into DST and return how many.
  // Return 0 at end of file.
  int getbytes(void *dst, int max, bool all=true);
  
  // Get (up to) N bytes into a string.  Returns the number of bytes read.
  int getbytes(string &s, int n, bool all=true);
  
  // Write a character
  inline void put(int c) {
    if(next == top) flush();
    *next++ = c;
  }

  // Write a string
  void put(const char *s, size_t len);

  // Write a string
  inline void put(const string &s) {
    put(s.data(), s.size());
  }
  
  // Write a string
  inline void put(const char *s) {
    put(s, strlen(s));
  }

  void putf(const char *fmt, ...);

  // Flush pending output.  Write errors might be deferred until a call to
  // flush().
  void flush();
};

enum Filetype {
  RegularFile,
  Directory,
  SymbolicLink,
  UnknownFileType,
};

enum OpenMode {
  ReadOnly,                             // open for reading
  Overwrite,                            // overwrite existing file
  NoOverwrite                           // don't overwrite existing file
};

// Generic filesystem
class Filesystem {
public:
  virtual ~Filesystem();

  virtual void rename(const string &oldpath, const string &newpath) = 0;
  // rename OLDPATH to NEWPATH

  virtual void link(const string &oldpath, const string &newpath);
  // link OLDPATH to NEWPATH

  virtual void remove(const string &path) = 0;
  // remove PATH

  virtual File *open(const string &path, const OpenMode mode) = 0;
  // open PATH with mode MODE

  virtual void mkdir(const string &path, mode_t mode = 0777) = 0;
  // create a directory

  virtual void mknod(const string &path, mode_t mode, dev_t dev);
  // create a device
  
  virtual void lchown(const string &path, uid_t uid, gid_t gid);
  // change ownership

  virtual void chmod(const string &path, mode_t mode);
  // change mode

  virtual void symlink(const string &target, const string &path);
  // create a link
  
  virtual int exists(const string &path) = 0;
  // return nonzero iff PATH exists

  virtual void contents(const string &path,
                        list<string> &c) = 0;
  // get directory contents

  virtual Filetype type(const string &path) = 0;
  // get file type

  virtual string readlink(const string &path);
  // read the contents of a link

  virtual bool ismount(const string &path);
  // return true if PATH is a mount point
  
  virtual void utimes(const string &path, time_t atime, time_t mtime);
  // set file times

  // make PATH and its parent directories
  void makedirs(const string &path);
};

// Local Filesystem -----------------------------------------------------------

// File on a local filesystem
class LocalFile : public File {
private:
  int fd;
  string path;
public:
  LocalFile(const string &path_, OpenMode mode);
  inline LocalFile(const string &path_, int fd_) : fd(fd_), path(path_) {}
  ~LocalFile();
  bool readable() const;                // return true if can read without
                                        // blocking
private:
  int readbytes(void *buf, int space);
  void writebytes(const void *buf, int nbytes);
};

// A local filesystem
class LocalFilesystem : public Filesystem {
public:
  inline LocalFilesystem() {}
  void rename(const string &oldpath, const string &newpath);
  void link(const string &oldpath, const string &newpath);
  void remove(const string &path);
  File *open(const string &path, OpenMode mode);
  void mkdir(const string &path, mode_t mode);
  void mknod(const string &path, mode_t mode, dev_t dev);
  void lchown(const string &path, uid_t uid, gid_t gid);
  void chmod(const string &path, mode_t mode);
  void symlink(const string &target, const string &path);
  int exists(const string &path);
  void contents(const string &path,
                list<string> &c);
  string readlink(const string &path);
  bool ismount(const string &path);
  void utimes(const string &path, time_t atime, time_t mtime);
  Filetype type(const string &path);
};

extern LocalFilesystem local;

// SFTP filesystem ------------------------------------------------------------

class SftpFilesystem : public Filesystem {
  const string &userhost;
  LocalFile *in, *out;
  pid_t pid;
  uint32_t id;
  map<uint32_t, string> replies;        // pending replies
  set<uint32_t> ignored;                // ignored IDs
public:
  inline SftpFilesystem(const string &userhost_) :
    userhost(userhost_), in(0), out(0), pid(-1), id(0) {}
  virtual ~SftpFilesystem();

  void rename(const string &oldpath, const string &newpath);
  void remove(const string &path);
  void unlink(const string &path);
  void rmdir(const string &path);
  File *open(const string &path, OpenMode mode);
  void mkdir(const string &path, mode_t mode);
  int exists(const string &path);
  void contents(const string &path,
                list<string> &c);
  Filetype type(const string &path);

private:
  void connect();
  // Connect to the SFTP server
  
  void init();
  // Initialize

  inline uint32_t newid() { return id++; }
  // Create a fresh ID

  void send(const string &cmd);
  // Send a command (does not flush).

  uint8_t recv(string &reply);
  // Receive a command.  Returns the type.

  uint8_t await(uint32_t id, string &reply);
  // Wait for the reply to ID

  void poll();
  // Read replies

  void check(const char *what,
             const string &path, 
             uint32_t id,
             bool allow_eof = false);
  // Await an SSH_FXP_STATUS reply for ID and turn it into an exception on
  // error

  void check(const char *what,
             const string &path, 
             const string &reply,
             bool allow_eof = false);
  // Turn an SSH_FXP_STATUS reply into an exception on error

  void ignore(uint32_t id);
  // Mark ID as to be ignored if a reply comes in

  uint32_t closehandle(const string &handle);
  // Close a handle and return the ID.  Doesn't flush or wait.
  
  bool ready(uint32_t id) const;
  // Return true if the reply to ID is available

  friend class SftpFile;
};

// SftpFile is hidden away in sftp.c

// Exclusion ------------------------------------------------------------------

class Exclusion {
private:
  pcre *re;
  pcre_extra *extra;
public:
  Exclusion(const char *s);

  bool matches(const char *s) const;
};

class Exclusions {
private:
  list<Exclusion> exclusions;
public:
  void add(const char *s);
  bool excluded(const string &path) const;
};


// Hashing --------------------------------------------------------------------

class Hash {
private:
  SHA1Context c;
  uint8_t result[SHA1HashSize];
public:
  Hash();
  ~Hash();
  inline void write(const void *ptr, size_t nbytes) {
    int e = SHA1Input(&c, (const uint8_t *)ptr, nbytes);
    if(e) throw HashError(e);
  }
  inline uint8_t *value() {
    int e = SHA1Result(&c, result);
    if(e) throw HashError(e);
    return result;
  }
};

void hashfile(Filesystem *fs, const string &path, uint8_t h[HASH_SIZE],
              bool mmap_hint = false);

// A set of hashes, implemented as a hashtable.
class HashSet {
private:
  struct node {
    struct node *next;
    uint8_t h[HASH_SIZE];
  };

  // Ugly hash function (relies hashes being uniformly distributed)
  static inline size_t hash(const uint8_t h[HASH_SIZE]) {
    return *(const size_t *)h % HASHTABLE_SIZE;
  }
  struct node *nodes[HASHTABLE_SIZE];
public:
  HashSet();
  ~HashSet();

  void insert(const uint8_t h[HASH_SIZE]);
  // Insert H if it is not already present

  bool has(const uint8_t h[HASH_SIZE]) const;
  // Return true if H present

  void stats() const;
  // Dump stats
};

// Encoding Conversion --------------------------------------------------------

class Recode {
public:
  Recode(const char *from_, const char *to_);
  ~Recode();
  string convert(const string &s);
private:
  iconv_t cd;
};

// Options --------------------------------------------------------------------

extern string repo, indexfile, root, sftphost;
extern int crossfs, preserve_atime, overwrite_index, deleteclean, verbose;
extern int permissions;
extern bool detectbogus;
extern Exclusions exclusions;
extern const char *sftpserver;

extern Filesystem *hostfs, *backupfs;
extern const char *from_encoding, *to_encoding;

// Utilities ------------------------------------------------------------------

void fatal(const char *msg, ...) __attribute__((noreturn));
void warning(const char *msg, ...);
void error(const char *msg, ...);
string uid2string(uid_t uid);
string gid2string(gid_t gid);
uid_t string2uid(const string &s);
gid_t string2gid(const string &s);
string hexencode(const uint8_t *h, size_t len);
void hexdecode(const string &hex, string &bytes);
string urlencode(const string &s);
string urldecode(const string &s, size_t start = 0, size_t end = string::npos);
void hashdecode(const string &hex, uint8_t h[HASH_SIZE]);
string hashpath(const uint8_t *h);
int readIndexLine(File *f, map<string,string> &l);
int parseIndexLine(const string &line, map<string,string> &l);
const string *getdetail(const map<string,string> &details,
                        const char *key);

extern void (*exitfn)(int);

// Operations -----------------------------------------------------------------

void do_backup();
void do_restore();
void do_verify();
void do_clean(int argc, char **argv);
void do_speedtest();

// Miscellaneous --------------------------------------------------------------

extern const char version[];

#endif /* NHBACKUP_H */

/*
Local Variables:
mode:c++
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
/* arch-tag:mlYy10vZkmzAOf80eUiorw */
