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
#include <vector>

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

// Hints ----------------------------------------------------------------------

struct hint {
  uint8_t hash[HASH_SIZE];
  struct stat statdata;
};

static map<string,hint> *hints;
static File *newhints;

static void load_hints() {
  if(!local.exists(hintfile))
     return;
  if(verbose)
    fprintf(stderr, "Loading hints from %s\n", hintfile.c_str());
  File *f = local.open(hintfile, ReadOnly);
  map<string,string> hintdetails;

  assert(hints != 0);  
  while(readIndexLine(f, hintdetails)) {
    const string name = hintdetails["name"];
    struct hint h;

    hashdecode(hintdetails[HASH_NAME], h.hash);
    h.statdata.st_ctime = strtoull(hintdetails["ctime"].c_str(), 0, 10);
    h.statdata.st_mtime = strtoull(hintdetails["mtime"].c_str(), 0, 10);
    h.statdata.st_size = strtoull(hintdetails["size"].c_str(), 0, 10);
    (*hints)[name] = h;
  }
  delete f;
}

// Backup ---------------------------------------------------------------------

static HashSet *inrepo;                 // hashes known to be in repo
static void backup_dir(const string &root, const string &dir,
                       File *index);

// Perform a backup
void do_backup() {
  string newhintfile;

  if(repo == "") fatal("no repository specified");
  if(root == "") fatal("no root specified");
  if(indexfile == "") fatal("no index specified");

  assert(hostfs == &local);             // remote not supported (yet)
  if(!overwrite_index && backupfs->exists(indexfile))
    fatal("index file %s already exists", indexfile.c_str());
  if(!inrepo)
    inrepo = new HashSet();
  if(hintfile != "") {
    hints = new map<string,hint>;
    load_hints();
    newhintfile = hintfile + ".tmp";
    newhints = local.open(newhintfile, Overwrite);
  }
  File *o = backupfs->open(overwrite_index ? indexfile : indexfile + ".tmp",
                           Overwrite);
  backup_dir(root, ".", o);
  o->put("[end]\n");
  o->flush();
  delete o;
  
  if(hints) {
    newhints->put("[end]\n");
    newhints->flush();
    delete newhints;
    local.rename(newhintfile, hintfile);
  }
  
  if(!overwrite_index) backupfs->rename(indexfile + ".tmp", indexfile);
}

struct hashable {
  const string path;
  const string hp;
  uint8_t hash[HASH_SIZE];
  hashable(const string &p, const string &h, uint8_t hash_[]): path(p), hp(h) {
    memcpy(hash, hash_, HASH_SIZE);
  }
};

// Back up DIR
static void backup_dir(const string &root, const string &dir,
                       File *index) {
  list<string> c, dirs;
  vector<string> ci;
  map<string,struct stat> s;
  const string fulldir = root + (dir == "." ? "" : "/" + dir);
  bool first = true;
  list<hashable> hashables;
  
  hostfs->contents(fulldir, c);
  // preallocate space for list of filenames
  ci.reserve(c.size());
  // stat all the files and exclude ones we're not going to consider
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
    struct stat sb; 
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
    // keep this one
    ci.push_back(name);
    // remember the stat data
    s[name] = sb;
  }
  // Put remaining filenames into order.  The main effect of this is to ensure
  // that two backups of the same set of files produce the same index file, so
  // that diffs are easier to follow.
  sort(ci.begin(), ci.end());
  // Now process all the files
  for(vector<string>::const_iterator it = ci.begin();
      it != ci.end();
      ++it) {
    const string &name = *it;
    const string localname = dir == "." ? name : dir + "/" + name;
    const string fullname = root + "/" + localname;
    const struct stat &sb = s[name];
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
        map<string,hint>::const_iterator it;

        if(hints
           && (it = hints->find(fullname)) != hints->end()
           && it->second.statdata.st_size == sb.st_size
           && it->second.statdata.st_mtime == sb.st_mtime
           && it->second.statdata.st_ctime == sb.st_ctime) {
          // file hasn't changed since last time we hash it
          memcpy(h, it->second.hash, HASH_SIZE);
          ++hints_used;
        } else {
          hashfile(hostfs, fullname, h, sb.st_size >= MINMAP);
        }
        if(hints) {
          // If we're saving hints, stash this one (regardless of whether we
          // hashed or not!)
          newhints->putf("name=%s&%s=%s&ctime=%llu&mtime=%llu&size=%llu\n",
                         urlencode(fullname).c_str(),
                         HASH_NAME,
                         hexencode(h, HASH_SIZE).c_str(),
                         (unsigned long long)sb.st_ctime,
                         (unsigned long long)sb.st_mtime,
                         (unsigned long long)sb.st_size);
        }
        // see if we've got it
        if(!inrepo->has(h)) {
          // We don't know for sure that the repo already has this file.  Check
          // it directly.
          const string hp = repo + "/" + HASH_NAME + "/" + hashpath(h);
          backupfs->prefigure_exists(hp);
          hashables.push_back(hashable(fullname, hp, h));
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
        struct timeval tv[2];

        // TODO subsecond timestamps
        memset(tv, 0, sizeof tv);
        tv[0].tv_sec = sb.st_atime;
        tv[1].tv_sec = sb.st_mtime;

        if(utimes(fullname.c_str(), tv) < 0)
          throw FileError("restoring access time", fullname, errno);
      }
      ++total_regular_files;
    } else if(S_ISDIR(sb.st_mode)) {
      index->putf("&type=dir\n");
      if(crossfs || !hostfs->ismount(fullname))
        dirs.push_back(localname);
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
  // Add any new hashes we need.  For an SFTP filesystem the existence tests
  // will have be "in flight" and so we save the round trip; not much of a
  // saving if we do in fact copy the file but a significant one in the case
  // that we don't.
  for(list<hashable>::const_iterator it = hashables.begin();
      it != hashables.end();
      ++it) {
    if(!backupfs->exists(it->hp)) {
      const string &tmpname = it->hp + ".tmp";
      // The repo doesn't have this file.  Copy it in.
      File *f = hostfs->open(it->path, ReadOnly), *dst;
      int n;
      static char buffer[4096];
      Hash hashctx;
      
      // In the long term the directories will usually exist, so try for
      // the file open first.
      try {
        dst = backupfs->open(tmpname, Overwrite);
      } catch(FileError &e) {
        if(e.error() != ENOENT) throw;
        backupfs->makedirs(string(it->hp, 0, it->hp.rfind('/')));
        // TODO use dirname() above
        dst = backupfs->open(tmpname, Overwrite);
      }
      while((n = f->getbytes(buffer, sizeof buffer, false))) {
        dst->put(buffer, n);
        if(recheckhash)
          hashctx.write(buffer, n);
      }
      if(recheckhash) {
        const uint8_t *const actual_hash = hashctx.value();
        if(memcmp(actual_hash, it->hash, HASH_SIZE))
          fatal("%s changed hash between test and write", it->path.c_str());
      }
      dst->flush();
      delete f;
      delete dst;
      backupfs->rename(tmpname, it->hp);
      ++new_hashes;
    }
  }
  // And now deal with the subdirectories.  The consequence of doing the
  // directories last is that if you know the start of a directory's contents
  // in an index file, you just have to read up to the point where you find a
  // file from any other directory, or hit eof, and you know that you've
  // enumerated all the files in that directory.
  //
  // The next step, which hasn't been implemented yet, is to provide a fast
  // means of mapping a directory name, or for that matter an arbitrary
  // filename, to its location in the index file.  At that point navigation
  // within an index can become as fast as the lookup mechanism.
  for(list<string>::const_iterator it = dirs.begin();
      it != dirs.end();
      ++it)
    backup_dir(root, *it, index);
}

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
