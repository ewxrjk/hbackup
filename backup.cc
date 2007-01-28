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

/* It is not sensible to store small files by hash.
 *
 * Firstly, if the file contents (URL-encoded) occupy less space then the hash
 * (hex) then storing the hash not only takes up a file but also makes the
 * index file unnecessarily bigger.  No matter how many copies of the file
 * there are, storing the file contents is always a win.
 *
 * Secondly however if the file is smaller than a the minimum space occupied by
 * a file, however large that might be (some kind of disk block, plus a
 * directory entry, perhaps plus some other metadata), then it's only a win to
 * store it by hash if there are several copies.  So there is a tradeoff to be
 * had between saving duplication and saving total space.
 *
 * STORE_LIMIT embodies this tradeoff.  Any file up to this size will be stored
 * by content, not by hash.
 */
#define STORE_LIMIT 256

// Backup ---------------------------------------------------------------------

static HashSet *inrepo;                 // hashes known to be in repo
static void backup_dir(const string &root, const string &dir,
                       File *index);

// Perform a backup
void do_backup() {
  if(repo == "") fatal("no repository specified");
  if(root == "") fatal("no root specified");
  if(indexfile == "") fatal("no index specified");
  assert(hostfs == &local);             // remote not supported (yet)
  if(!overwrite_index && backupfs->exists(indexfile))
    fatal("index file %s already exists", indexfile.c_str());
  if(!inrepo)
    inrepo = new HashSet();
  File *o = backupfs->open(overwrite_index ? indexfile : indexfile + ".tmp",
                           Overwrite);
  backup_dir(root, ".", o);
  o->put("[end]\n");
  o->flush();
  delete o;
  
  if(!overwrite_index) backupfs->rename(indexfile + ".tmp", indexfile);
}

// Back up DIR
static void backup_dir(const string &root, const string &dir,
                       File *index) {
  list<string> c;
  const string fulldir = root + (dir == "." ? "" : "/" + dir);
  struct stat sb;
  bool first = true;
  
  hostfs->contents(fulldir, c);
  for(list<string>::const_iterator it = c.begin();
      it != c.end();
      ++it) {
    const string &name = *it;
    // skip infra links
    if(name == "." || name == "..") continue;
    const string localname = dir == "." ? name : dir + "/" + name;
    const string fullname = root + "/" + localname;
    // skip excluded files
    if(exclusions.excluded(localname)) continue;
    // stat the file
    if(lstat(fullname.c_str(), &sb) < 0) {
      // MacOS will return files from readdir that you cannot then stat.
      // (There's one in my Photoshop tryout install.)  Insanity, but we try to
      // cope.
      // TODO: option to fail if we encounter such files.
      if(errno == ENOENT) {
        warning("lstat %s: %s", fullname.c_str(), strerror(errno));
        continue;
      }
      throw FileError("lstat", fullname, errno);
    }
    // skip unknown file types
    if(!(S_ISREG(sb.st_mode)
         || S_ISDIR(sb.st_mode)
         || S_ISLNK(sb.st_mode)
         || S_ISCHR(sb.st_mode)
         || S_ISBLK(sb.st_mode)
         || S_ISSOCK(sb.st_mode))) {
      warning("cannot back up %s", fullname.c_str());
      ++unknown_files;
      continue;
    }
    // figure out how to represent the name
    string relname;
    if(first) {
      // At the start of a directory, or just after a subdirectory, always use
      // the full name.
      relname = localname;
    } else {
      const string::size_type n = localname.rfind('/');
      if(n != string::npos)
        // For other names use the relative name...
        relname = "./" + localname.substr(n + 1, string::npos);
      else
        // ...except in the root directory.
        relname = localname;
    }
    // generic details
    index->putf("name=%s&perms=%0#o&uid=%s&gid=%s&atime=%llu&ctime=%llu&mtime=%llu",
                urlencode(relname).c_str(),
                sb.st_mode & 07777,
                urlencode(uid2string(sb.st_uid)).c_str(),
                urlencode(gid2string(sb.st_gid)).c_str(),
                (unsigned long long)sb.st_atime,
                (unsigned long long)sb.st_ctime,
                (unsigned long long)sb.st_mtime);
    first = false;
    // type-specific details
    if(S_ISREG(sb.st_mode)) {
      if(sb.st_size <= STORE_LIMIT) {
        // The file is small so we just store the bytes
        uint8_t buffer[STORE_LIMIT];
        int n = 0, bytes;

        File *f = hostfs->open(fullname, ReadOnly);
        while(n < sb.st_size) {
          n += (bytes = f->getbytes(buffer + n, sb.st_size - n, false));
          if(!bytes)
            fatal("unexpected EOF reading %s", fullname.c_str());
        }
        index->putf("&data=%s",
                    urlencode(string((char *)buffer, sb.st_size)).c_str());
        delete f;
        ++small_files;
      } else {
        // The file is large so we store it in the filesystem by hash.
        uint8_t h[HASH_SIZE];

        hashfile(hostfs, fullname, h, sb.st_size >= MINMAP);
        // see if we've got it
        if(!inrepo->has(h)) {
          // We don't know for sure that the repo already has this file.  Check
          // it directly.
          const string hp = repo + "/" + HASH_NAME + "/" + hashpath(h);
          if(!backupfs->exists(hp)) {
            // The repo doesn't have this file.  Copy it in.
            File *f = hostfs->open(fullname, ReadOnly), *dst;
            int n;
            static char buffer[4096];

            // In the long term the directories will usually exist, so try for
            // the file open first.
            try {
              dst = backupfs->open(hp + ".tmp", Overwrite);
            } catch(FileError &e) {
              if(e.error() != ENOENT) throw;
              backupfs->makedirs(string(hp, 0, hp.rfind('/')));
              dst = backupfs->open(hp + ".tmp", Overwrite);
            }
            while((n = f->getbytes(buffer, sizeof buffer, false)))
              dst->put(buffer, n);
            dst->flush();
            delete f;
            delete dst;
            backupfs->rename(hp + ".tmp", hp);
            ++new_hashes;
          }
          // The repo now has the file either way
          inrepo->insert(h);
        }
        index->putf("&%s=%s", HASH_NAME, hexencode(h, HASH_SIZE).c_str());
      }
      // If number of links is nontrivial record the inode number so the
      // restore process can connect hard links back together
      if(sb.st_nlink > 1)
        index->putf("&inode=%llu", sb.st_ino);
      index->put('\n');
      // Restore the atime
      if(preserve_atime) {
        const struct timeval tv[2] = { sb.st_atime, sb.st_mtime };

        if(utimes(fullname.c_str(), tv) < 0)
          throw FileError("restoring access time", fullname, errno);
      }
      ++total_regular_files;
    } else if(S_ISDIR(sb.st_mode)) {
      index->putf("&type=dir\n");
      if(crossfs || !hostfs->ismount(fullname))
        backup_dir(root, localname, index);
      ++total_dirs;
      // Next filename had better have an absolute path
      first = true;
    } else if(S_ISLNK(sb.st_mode)) {
      index->putf("&target=%s&type=link\n",
                  urlencode(hostfs->readlink(fullname)).c_str());
      ++total_links;
    } else if(S_ISCHR(sb.st_mode) || S_ISBLK(sb.st_mode)) {
      index->putf("&rdev=%d&type=%s\n",
                  sb.st_rdev, S_ISCHR(sb.st_mode) ? "chr" : "blk");
      ++total_devs;
    } else if(S_ISSOCK(sb.st_mode)) {
      index->put("&type=socket\n");
      ++total_socks;
    }
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
/* arch-tag:q+bT+fqN1uBrjtEwpTHwMQ */
