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
#include <csignal>
#include <sys/wait.h>

// packets types from draft-ietf-secsh-filexfer-02.txt 3
static const uint8_t SSH_FXP_INIT                =1;
static const uint8_t SSH_FXP_VERSION             =2;
static const uint8_t SSH_FXP_OPEN                =3;
static const uint8_t SSH_FXP_CLOSE               =4;
static const uint8_t SSH_FXP_READ                =5;
static const uint8_t SSH_FXP_WRITE               =6;
static const uint8_t SSH_FXP_LSTAT               =7;
static const uint8_t SSH_FXP_FSTAT               =8;
static const uint8_t SSH_FXP_SETSTAT             =9;
static const uint8_t SSH_FXP_FSETSTAT           =10;
static const uint8_t SSH_FXP_OPENDIR            =11;
static const uint8_t SSH_FXP_READDIR            =12;
static const uint8_t SSH_FXP_REMOVE             =13;
static const uint8_t SSH_FXP_MKDIR              =14;
static const uint8_t SSH_FXP_RMDIR              =15;
static const uint8_t SSH_FXP_REALPATH           =16;
static const uint8_t SSH_FXP_STAT               =17;
static const uint8_t SSH_FXP_RENAME             =18;
static const uint8_t SSH_FXP_READLINK           =19;
static const uint8_t SSH_FXP_SYMLINK            =20;
static const uint8_t SSH_FXP_STATUS            =101;
static const uint8_t SSH_FXP_HANDLE            =102;
static const uint8_t SSH_FXP_DATA              =103;
static const uint8_t SSH_FXP_NAME              =104;
static const uint8_t SSH_FXP_ATTRS             =105;
static const uint8_t SSH_FXP_EXTENDED          =200;
static const uint8_t SSH_FXP_EXTENDED_REPLY    =201;

// attribute flag bits from draft-ietf-secsh-filexfer-02.txt 5
static const uint32_t SSH_FILEXFER_ATTR_SIZE          =0x00000001;
static const uint32_t SSH_FILEXFER_ATTR_UIDGID        =0x00000002;
static const uint32_t SSH_FILEXFER_ATTR_PERMISSIONS   =0x00000004;
static const uint32_t SSH_FILEXFER_ATTR_ACMODTIME     =0x00000008;
static const uint32_t SSH_FILEXFER_ATTR_EXTENDED      =0x80000000;

// pflags from draft-ietf-secsh-filexfer-02.txt 6.3
static const uint32_t SSH_FXF_READ            =0x00000001;
static const uint32_t SSH_FXF_WRITE           =0x00000002;
static const uint32_t SSH_FXF_APPEND          =0x00000004;
static const uint32_t SSH_FXF_CREAT           =0x00000008;
static const uint32_t SSH_FXF_TRUNC           =0x00000010;
static const uint32_t SSH_FXF_EXCL            =0x00000020;

// status codes from draft-ietf-secsh-filexfer-02.txt 7
static const uint32_t SSH_FX_OK                            =0;
static const uint32_t SSH_FX_EOF                           =1;
static const uint32_t SSH_FX_NO_SUCH_FILE                  =2;
static const uint32_t SSH_FX_PERMISSION_DENIED             =3;
static const uint32_t SSH_FX_FAILURE                       =4;
static const uint32_t SSH_FX_BAD_MESSAGE                   =5;
static const uint32_t SSH_FX_NO_CONNECTION                 =6;
static const uint32_t SSH_FX_CONNECTION_LOST               =7;
static const uint32_t SSH_FX_OP_UNSUPPORTED                =8;

struct Status {
  uint32_t status;
  string message;
  string lang;
};

struct Attributes {
  uint32_t flags;
  uint64_t size;
  uint32_t uid;
  uint32_t gid;
  uint32_t permissions;
  uint32_t atime;
  uint32_t mtime;
  map<string, string> extended;
};

static inline void pack_uint8(string &s, uint8_t byte) {
  s += (char)byte;
}

static void pack_uint32(string &s, uint32_t u) {
  pack_uint8(s, (uint8_t)(u >> 24));
  pack_uint8(s, (uint8_t)(u >> 16));
  pack_uint8(s, (uint8_t)(u >> 8));
  pack_uint8(s, (uint8_t)u);
}

static void pack_uint64(string &s, uint64_t u) {
  pack_uint8(s, (uint8_t)(u >> 56));
  pack_uint8(s, (uint8_t)(u >> 48));
  pack_uint8(s, (uint8_t)(u >> 40));
  pack_uint8(s, (uint8_t)(u >> 32));
  pack_uint8(s, (uint8_t)(u >> 24));
  pack_uint8(s, (uint8_t)(u >> 16));
  pack_uint8(s, (uint8_t)(u >> 8));
  pack_uint8(s, (uint8_t)(u));
}

static void pack_string(string &s, const string &t) {
  pack_uint32(s, (uint32_t)t.size());
  s += t;
}

static void pack_bytes(string &s, const void *data, size_t n) {
  pack_uint32(s, (uint32_t)n);
  s.append((const char *)data, n);
}

static uint8_t unpack_uint8(const string &s, size_t &index) {
  return s.at(index++);
}

static uint32_t unpack_uint32(const string &s, size_t &index) {
  uint32_t n;

  n = (uint32_t)unpack_uint8(s, index) << 24;
  n += (uint32_t)unpack_uint8(s, index) << 16;
  n += (uint32_t)unpack_uint8(s, index) << 8;
  n += (uint32_t)unpack_uint8(s, index);
  return n;
}

static uint64_t unpack_uint64(const string &s, size_t &index) {
  uint64_t n;

  n = (uint64_t)unpack_uint8(s, index) << 56;
  n += (uint64_t)unpack_uint8(s, index) << 48;
  n += (uint64_t)unpack_uint8(s, index) << 40; 
  n += (uint64_t)unpack_uint8(s, index) << 32;
  n += (uint64_t)unpack_uint8(s, index) << 24;
  n += (uint64_t)unpack_uint8(s, index) << 16;
  n += (uint64_t)unpack_uint8(s, index) << 8;
  n += (uint64_t)unpack_uint8(s, index);
  return n;
}

static string unpack_string(const string &s, size_t &index) {
  uint32_t len = unpack_uint32(s, index);

  index += len;
  return string(s, index - len, len);
}

static void unpack_status(const string &s, size_t &index, Status &st) {
  st.status = unpack_uint32(s, index);
  st.message = unpack_string(s, index);
  st.lang = unpack_string(s, index);
}

static void unpack_attrs(const string &s, size_t &index, Attributes &attrs) {
  attrs.flags = unpack_uint32(s, index);
  if(attrs.flags & SSH_FILEXFER_ATTR_SIZE)
    attrs.size = unpack_uint64(s, index);
  if(attrs.flags & SSH_FILEXFER_ATTR_UIDGID) {
    attrs.uid = unpack_uint32(s, index);
    attrs.gid = unpack_uint32(s, index);
  }
  if(attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS)
    attrs.permissions = unpack_uint32(s, index);
  if(attrs.flags & SSH_FILEXFER_ATTR_ACMODTIME) {
    attrs.atime = unpack_uint32(s, index);
    attrs.mtime = unpack_uint32(s, index);
  }
  if(attrs.flags & SSH_FILEXFER_ATTR_EXTENDED) {
    uint32_t count = unpack_uint32(s, index);
    while(count > 0) {
      const string k = unpack_string(s, index);
      const string v = unpack_string(s, index);
      attrs.extended[k] = v;
    }
  }
}

// SftpFileError --------------------------------------------------------------

SftpFileError::SftpFileError(const char *doing,
                             const string &path,
                             const string &message,
                             uint32_t status_) : FileError(doing, path, 0),
                                                 status(status_) {
  // TODO convert message to local encoding (it's UTF-8 to start with)
  snprintf(w, sizeof w, "%s %s: %s", doing, path.c_str(), message.c_str());
  switch(status) {
  case SSH_FX_OK: e = 0; break;
  case SSH_FX_NO_SUCH_FILE: e = ENOENT; break;
  case SSH_FX_PERMISSION_DENIED: e = EACCES; break;
  case SSH_FX_OP_UNSUPPORTED: e = ENOSYS; break;
  default: e = -1; break;
  }
}

// SftpFile -------------------------------------------------------------------

class SftpFile : public File {
private:
  SftpFilesystem *fs;
  string path;
  string handle;

  // queue of pending read operations
  list<uint32_t> readqueue;
  // remaining bytes readable
  string readbuffer;
  // offset to read at
  uint64_t readoffset;
  // set at eof
  bool eof;

  // desired size of read queue
  static const size_t readqueuesize = 8;

  // outstanding writes
  list<uint32_t> writequeue;
  // offset to write at
  uint64_t writeoffset;

public:
  SftpFile(SftpFilesystem *fs_, const string &path_, const string &handle_):
    fs(fs_), 
    path(path_),
    handle(handle_),
    readoffset(0),
    eof(false),
    writeoffset(0) {
  }

  ~SftpFile() {
    synchronize();
    fs->ignore(fs->closehandle(handle));
  }

private:
  
  int readbytes(void *buf, int space) {
    if(!eof and readqueue.size() < readqueuesize) {
      // Keep the read queue full
      while(readqueue.size() < readqueuesize) {
        string cmd;
        const uint32_t id = fs->newid();
        pack_uint8(cmd, SSH_FXP_READ);
        pack_uint32(cmd, id);
        pack_string(cmd, handle);
        pack_uint64(cmd, readoffset);
        pack_uint32(cmd, space);
        readoffset += space;
        fs->send(cmd);
        // This looks inefficient but we hope that the local TCP implementation
        // will batch things up.
        fs->out->flush();
        readqueue.push_back(id);
      }
    }

    if(!readbuffer.size() and readqueue.size()) {
      // Nothing in the buffer but there are outstanding reads which might fill
      // it.
      const uint32_t id = readqueue.front();
      readqueue.pop_front();
      string reply;
      const uint8_t r = fs->await(id, reply);
      if(r == SSH_FXP_DATA) {
        size_t index = 5;               // skip type + id
        readbuffer = unpack_string(reply, index);
      } else {
        // Report errors
        fs->check("reading", path, reply, true);
        // We must be at EOF.  Ignore all the remaining reads and empty the
        // queue.
        while(readqueue.size()) {
          fs->ignore(readqueue.front());
          readqueue.pop_front();
        }
        eof = true;
      }
    }

    if(readbuffer.size()) {
      // There are bytes in the buffer
      if(readbuffer.size() < (size_t)space)
        space = readbuffer.size();
      memcpy(buf, readbuffer.data(), space);
      readbuffer.erase(0, space);
      return space;
    }

    // We must be at eof.
    return 0;
  }

  void writebytes(const void *buf, int nbytes) {
    if(!nbytes) return;
    string cmd;

    const uint32_t id = fs->newid();
    cmd.reserve(handle.size() + nbytes + 21);
    pack_uint8(cmd, SSH_FXP_WRITE);
    pack_uint32(cmd, id);
    pack_string(cmd, handle);
    pack_uint64(cmd, writeoffset);
    pack_bytes(cmd, buf, nbytes);
    fs->send(cmd);
    writeoffset += nbytes;
    fs->out->flush();
    writequeue.push_back(id);
    // Process any recently arrived replies.  In principle the replies might
    // arrive out of order but in practice this seems a bit unlikely.
    uint32_t ready_id;
    while((ready_id = find_ready_write()))
      synchronize_one(ready_id);
  }

  void synchronize_one(uint32_t id = 0) {
    string reply;
    
    if(!id)
      id = writequeue.front();
    // Wait for the reply to arrive
    fs->await(writequeue.front(), reply);
    list<uint32_t>::iterator it;
    for(it = writequeue.begin();
        it != writequeue.end() && *it != id;
        ++it)
      ;
    assert(it != writequeue.end());
    // Remove it from the queue
    writequeue.erase(it);
    // Check for errors
    fs->check("writing to", path, reply);
  }

  void synchronize() {
    // We don't care what order we reap the replies in
    while(writequeue.size())
      synchronize_one();
  }

  // Return the ID of a write that has been replied to, or return 0 if there
  // are none.
  uint32_t find_ready_write() const {
    for(list<uint32_t>::const_iterator it = writequeue.begin();
        it != writequeue.end();
        ++it) {
      if(fs->ready(*it))
        return *it;
    }
    // newid() never returns 0
    return 0;
  }

};

// SftpFilesystem -------------------------------------------------------------

void SftpFilesystem::send(const string &cmd) {
  // before any write we poll for responses, so the server won't be blocked on
  // buffered output.
  while(in->readable())
    poll();
  string slen;
  pack_uint32(slen, cmd.size());
  out->put(slen);
  out->put(cmd);
#if 0
  fprintf(stderr, "send:");
  for(size_t n = 0; n < cmd.size(); ++n)
    fprintf(stderr, " %02x", (uint8_t)cmd[n]);
  fputc('\n', stderr);
#endif
}

uint8_t SftpFilesystem::recv(string &reply) {
  string slen;

  const int llen = in->getbytes(slen, 4);
  if(llen != 4)
    fatal("SFTP reply too short for length word (only got %d bytes)", llen);
  size_t index = 0;
  const uint32_t len = unpack_uint32(slen, index);
  const uint32_t gotbytes = in->getbytes(reply, len);
  if(gotbytes != len)
    fatal("SFTP reply truncated (got %lu, expected %lu)",
          (unsigned long)gotbytes, (unsigned long)len);
  return (uint8_t)reply[0];
}

void SftpFilesystem::connect() {
  int inpipe[2], outpipe[2];
  int w;

  if(verbose)
    fprintf(stderr, "connecting to %s\n", userhost.c_str());
  inpipe[0] = inpipe[1] = outpipe[0] = outpipe[1] = -1;
  try {
    if(pipe(inpipe) < 0) throw FileError("creating", "pipe", errno);
    if(pipe(outpipe) < 0) throw FileError("creating", "pipe", errno);
    switch(pid = fork()) {
    case -1: fatal("fork: %s", strerror(errno));
    case 0:
      exitfn = _exit;
      signal(SIGPIPE, SIG_DFL);
      if(dup2(inpipe[1], 1) < 0 || dup2(outpipe[0], 0) < 0)
	fatal("dup2", strerror(errno));
      close(inpipe[0]);
      close(inpipe[1]);
      close(outpipe[0]);
      close(outpipe[1]);
      if(sftpserver)
        execlp("ssh", "ssh", "-x", "-T", userhost.c_str(), sftpserver, (char *)0);
      else
        execlp("ssh", "ssh", "-x", "-s", userhost.c_str(), "sftp", (char *)0);
      fatal("executing ssh client", strerror(errno));
    }
    close(inpipe[1]); inpipe[1] = -1;
    close(outpipe[0]); outpipe[0] = -1;
    in = new LocalFile(userhost, inpipe[0]); inpipe[0] = -1;
    out = new LocalFile(userhost, outpipe[1]); outpipe[1] = -1;
  } catch(...) {
    // tear it all back down
    if(inpipe[0] != -1) close(inpipe[0]);
    if(inpipe[1] != -1) close(inpipe[1]);
    if(outpipe[0] != -1) close(outpipe[0]);
    if(outpipe[1] != -1) close(outpipe[1]);
    if(in) { delete in; in = 0; }
    if(out) { delete out; out = 0; }
    if(pid != -1) { kill(pid, SIGTERM); waitpid(pid, &w, 0); pid = -1; }
    throw;
  }
}

SftpFilesystem::~SftpFilesystem() {
  int w;

  if(in) delete in;
  if(out) delete out;
  if(pid != -1) {
    if(waitpid(pid, &w, 0) < 0)
      fatal("error calling waitpid: %s", strerror(errno));
    if(w)
      error("sftp subprocess exited with wstat %#x", (unsigned)w);
  }
}

void SftpFilesystem::init() { 
  if(in) return;                        // idempotent

  connect();
  
  // Send the init command
  string cmd, reply;    
  cmd.reserve(5);
  pack_uint8(cmd, SSH_FXP_INIT);
  pack_uint32(cmd, 3);
  send(cmd);
  out->flush();
  
  // Synchronously await a reply
  uint8_t r = recv(reply);
  if(r != SSH_FXP_VERSION)
    fatal("expected SSH_FXP_VERSION, got %#x", (unsigned)r);
  size_t index = 1;                     // skip type
  const uint32_t version = unpack_uint32(reply, index);
  if(version < 3)
    fatal("expected SFTP version at least 3, got %lu", (unsigned long)version);
  // TODO record xdata
}

void SftpFilesystem::rename(const string &oldpath, const string &newpath) {
  // Ensure initialized
  init();

  // Send the command
  const uint32_t id = newid();
  string cmd;
  if(posix_rename) {
    pack_uint8(cmd, SSH_FXP_EXTENDED);
    pack_uint32(cmd, id);
    pack_string(cmd, "posix-rename@openssh.org");
    pack_string(cmd, oldpath);
    pack_string(cmd, newpath);
  } else {
    cmd.reserve(13 + oldpath.size() + newpath.size());
    pack_uint8(cmd, SSH_FXP_RENAME);
    pack_uint32(cmd, id);
    pack_string(cmd, oldpath);
    pack_string(cmd, newpath);
  }
  send(cmd);
  out->flush();

  // Synchronously await a reply
  check("renaming", oldpath, id);
}

void SftpFilesystem::remove(const string &path) {
  try {
    unlink(path);
  } catch(SftpFileError &e) {
    // unlink failed - perhaps it's a directory
    if(e.status == SSH_FX_FAILURE)
      rmdir(path);
    else
      throw;
  }
}

void SftpFilesystem::unlink(const string &path) {
  // Ensure initialized
  init();

  // Send the command
  const uint32_t id = newid();
  string cmd;
  cmd.reserve(9 + path.size());
  pack_uint8(cmd, SSH_FXP_REMOVE);
  pack_uint32(cmd, id);
  pack_string(cmd, path);
  send(cmd);
  out->flush();

  // Synchronously await a reply
  check("removing", path, id);
}

void SftpFilesystem::rmdir(const string &path) {
  // Ensure initialized
  init();

  // Send the command
  const uint32_t id = newid();
  string cmd;
  cmd.reserve(9 + path.size());
  pack_uint8(cmd, SSH_FXP_RMDIR);
  pack_uint32(cmd, id);
  pack_string(cmd, path);
  send(cmd);
  out->flush();

  // Synchronously await a reply
  check("removing directory", path, id);
}

void SftpFilesystem::check(const char *what,
                           const string &path, 
                           uint32_t id,
                           bool allow_eof) {
  string reply;

  // Await the reply
  await(id, reply);
  check(what, path, reply, allow_eof);
}

void SftpFilesystem::check(const char *what,
                           const string &path, 
                           const string &reply,
                           bool allow_eof) {
  Status st;
  size_t index = 0;
  const uint8_t r = unpack_uint8(reply, index); // get the type
  if(r != SSH_FXP_STATUS)
    fatal("expected SSH_FXP_STATUS, got %#x", (unsigned)r);
  unpack_uint32(reply, index);          // skip ID
  unpack_status(reply, index, st);
  if(st.status)
    if(st.status != SSH_FX_EOF || !allow_eof)
      throw SftpFileError(what, path, st.message, st.status);
}

File *SftpFilesystem::open(const string &path, OpenMode mode) {
  init();

  // Marshal and send the command
  string cmd, reply;
  const uint32_t id = newid();
  cmd.reserve(17 + path.size());
  pack_uint8(cmd, SSH_FXP_OPEN);
  pack_uint32(cmd, id);
  pack_string(cmd, path);
  uint32_t m;
  switch(mode) {
  case ReadOnly: m = SSH_FXF_READ; break;
  case Overwrite: m = SSH_FXF_WRITE|SSH_FXF_CREAT|SSH_FXF_TRUNC; break;
  case NoOverwrite: m = SSH_FXF_WRITE|SSH_FXF_CREAT|SSH_FXF_EXCL; break;
  default: fatal("invalid open mode %d", (int)mode);
  }
  pack_uint32(cmd, m);
  pack_uint32(cmd, 0);                  // no attrs
  send(cmd);
  out->flush();
  // Await the reply
  const uint8_t r = await(id, reply);
  if(r != SSH_FXP_HANDLE) check("opening", path, reply);
  size_t index = 5;                     // skip type+id
  return new SftpFile(this, path, unpack_string(reply, index));
}

void SftpFilesystem::mkdir(const string &path, mode_t mode) {
  // Ensure initialized
  init();

  // Send the command
  const uint32_t id = newid();
  string cmd;
  cmd.reserve(17 + path.size());
  pack_uint8(cmd, SSH_FXP_MKDIR);
  pack_uint32(cmd, id);
  pack_string(cmd, path);
  pack_uint32(cmd, SSH_FILEXFER_ATTR_PERMISSIONS);
  pack_uint32(cmd, mode);
  send(cmd);
  out->flush();

  // Synchronously await a reply
  check("creating directory", path, id);
}

int SftpFilesystem::exists(const string &path) {
  init();

  // Read any pending existence information
  while(!existence_inflight.empty()) {
    const exists_inflight &e = existence_inflight.front();
    string reply;
    const uint8_t r = await(e.id, reply);
    existence[e.path] = (r == SSH_FXP_ATTRS);
    existence_inflight.pop_front();
  }
  // Inspect the cache
  const map<string,bool>::iterator it = existence.find(path);
  if(it != existence.end()) {
    const bool e = it->second;
    existence.erase(it);
    return e;
  }
  // No cached information
  prefigure_exists(path);
  return exists(path);
}

uint32_t SftpFilesystem::closehandle(const string &handle) {
  const uint32_t id = newid();
  string cmd;
  cmd.reserve(13 + handle.size());
  pack_uint8(cmd, SSH_FXP_CLOSE);
  pack_uint32(cmd, id);
  pack_string(cmd, handle);
  send(cmd);
  return id;
}

void SftpFilesystem::contents(const string &path,
			      list<string> &c) {
  // Ensure initialized
  init();

  // Open the directory
  const uint32_t id = newid();
  string cmd, reply;
  cmd.reserve(9 + path.size());
  pack_uint8(cmd, SSH_FXP_OPENDIR);
  pack_uint32(cmd, id);
  pack_string(cmd, path);
  send(cmd);
  out->flush();
  // Await the reply
  const uint8_t r = await(id, reply);
  if(r != SSH_FXP_HANDLE) check("opening directory", path, reply);
  size_t index = 5;                     // skip type+id
  const string handle = unpack_string(reply, index);
  try {
    cmd.clear();
    pack_uint8(cmd, SSH_FXP_READDIR);
    pack_uint32(cmd, id);
    pack_string(cmd, handle);
    for(;;) {
      send(cmd);
      out->flush();
      const uint8_t r = await(id, reply);
      if(r == SSH_FXP_NAME) {
        size_t index = 5;               // skip type+id
        size_t count = unpack_uint32(reply, index);
        while(count > 0) {
          const string filename = unpack_string(reply, index);
          const string longname = unpack_string(reply, index);
          Attributes attrs;
          unpack_attrs(reply, index, attrs);
          if(filename != "." && filename != "..")
            c.push_back(filename);
          --count;
        }
      } else {
        check("reading directory", path, reply, true);
        break;                          // presumably EOF
      }
    }
  } catch(...) {
    ignore(closehandle(handle));
    throw;
  }
  ignore(closehandle(handle));
}

Filetype SftpFilesystem::type(const string &path) {
  // Ensure initialized
  init();

  // Ridiculously there does not seem to be a simple way to just get the file
  // type.  So we sent a bunch of commands and see which succeed, if any, and
  // deduce the file type from that.

  string cmd;
  const uint32_t openfileid = newid();
  cmd.reserve(17 + path.size());
  pack_uint8(cmd, SSH_FXP_OPEN);
  pack_uint32(cmd, openfileid);
  pack_string(cmd, path);
  pack_uint32(cmd, SSH_FXF_READ);
  pack_uint32(cmd, 0);                  // no attrs
  send(cmd);

  cmd.clear();
  const uint32_t opendirid = newid();
  pack_uint8(cmd, SSH_FXP_OPENDIR);
  pack_uint32(cmd, opendirid);
  pack_string(cmd, path);
  send(cmd);

  const uint32_t readlinkid = newid();
  cmd.clear();
  pack_uint8(cmd, SSH_FXP_READLINK);
  pack_uint32(cmd, readlinkid);
  pack_string(cmd, path);
  send(cmd);

  out->flush();

  // Await the replies
  string readfilereply, readdirreply, readlinkreply;
  const uint8_t openfiler = await(openfileid, readfilereply);
  const uint8_t opendirr = await(opendirid, readdirreply);
  const uint8_t readlinkr = await(readlinkid, readlinkreply);
  
  // Maybe the file doesn't exist
  if(openfiler == SSH_FXP_STATUS) {
    size_t index = 5;                   // skip type+id
    uint32_t status = unpack_uint32(readfilereply, index);
    if(status == SSH_FX_NO_SUCH_FILE)
      check("checking file type", path, readfilereply); // raise the exception
  }

  // Whichever succeeded tells us the file type
  Filetype t = UnknownFileType;
  if(openfiler == SSH_FXP_HANDLE) {
    t = RegularFile;
    size_t index = 5;
    ignore(closehandle(unpack_string(readfilereply, index)));
  }
  if(opendirr == SSH_FXP_HANDLE) {
    t = Directory;
    size_t index = 5;
    ignore(closehandle(unpack_string(readdirreply, index)));
  }
  if(readlinkr == SSH_FXP_NAME)
    t = SymbolicLink;
  return t;
}

void SftpFilesystem::ignore(uint32_t id) {
  replies.erase(id);                    // might have been received already
  ignored.insert(id);
}

uint8_t SftpFilesystem::await(uint32_t id, string &reply) {
  // Wait for the reply to arrive
  map<uint32_t, string>::iterator it;
  while((it = replies.find(id)) == replies.end())
    poll();
  reply = it->second;
  replies.erase(it);                    // reply is no longer pending
  return (uint8_t)reply.at(0);          // return the type as a convenience
}

void SftpFilesystem::poll() {
  // get the next reply
  string reply;
  recv(reply);

  // pick out the ID
  size_t index = 1;                     // skip type
  const uint32_t id = unpack_uint32(reply, index);
  set<uint32_t>::iterator it;

  if((it = ignored.find(id)) == ignored.end())
    // stash pending reply
    replies[id] = reply;
  else
    // seen ignored reply, forget the ID for now
    ignored.erase(it);
}
  
bool SftpFilesystem::ready(uint32_t id) const {
  return replies.find(id) != replies.end();
}

uint32_t SftpFilesystem::newid() { 
  if(!id)
    id++;
  return id++;
}

void SftpFilesystem::prefigure_exists(const string &path) {
  init();

  string cmd;
  const uint32_t id = newid();
  assert(id != 0);
  pack_uint8(cmd, SSH_FXP_STAT);
  pack_uint32(cmd, id);
  pack_string(cmd, path);
  send(cmd);
  out->flush();
  existence_inflight.push_back(exists_inflight(id, path));
}

bool SftpFilesystem::posix_rename;

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
