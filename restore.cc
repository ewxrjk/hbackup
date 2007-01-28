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
#include <sys/un.h>

// Restore --------------------------------------------------------------------

struct dirstamp {
  string dir;
  time_t atime, mtime;

  inline dirstamp(const string &path, time_t atime_, time_t mtime_):
    dir(path),
    atime(atime_), 
    mtime(mtime_) {
  }
};

void do_restore() {
  list<dirstamp> dirtimes;
  string dir;
  Recode recoder(from_encoding, to_encoding);

  if(repo == "") fatal("no repository specified");
  if(root == "") fatal("no root specified");
  if(indexfile == "") fatal("no index specified");

  File *f = backupfs->open(indexfile, ReadOnly);
  map<string,string> details;
  map<ino_t, string> inodes;
  if(verbose)
    fprintf(stderr, "restoring from %s\n", indexfile.c_str());
  while(readIndexLine(f, details)) {
    string name = recoder.convert(details["name"]);

    // Deal with relative names
    if(name[0] == '.' && name[1] == '/') {
      // If the name in the file has the form ./something then it belongs to
      // the same directory as the last file
      if(dir.size() == 0) {
        error("unexpected relative name: %s", name.c_str());
        continue;
      }
      name = dir + "/" + name.substr(2,string::npos);
    } else {
      // Figure out the directory part for the benefit of the next line
      const string::size_type n = name.rfind('/');

      if(n != string::npos)
        dir = name.substr(0, name.rfind('/'));
      else
        dir.clear();
    }

    const string fullname = root + "/" + name;
    const string tmpname = fullname + "~restore~";

    // Get the temporary file out of the way
    try { hostfs->remove(tmpname); } catch(...) {}
    // Might be a link to a file we already unpacked
    const string *inode = getdetail(details, "inode");
    const ino_t inodenum = inode ?  strtoull(inode->c_str(), 0, 10) : 0;
    if(inode) {
      map<ino_t, string>::const_iterator inodepath = inodes.find(inodenum);
      if(inodepath != inodes.end()) {
        ++total_hardlinks;
        hostfs->link(inodepath->second, tmpname);
        hostfs->rename(tmpname, fullname);
        // don't mess about with permissions
        continue;
      }
    }
    
    mode_t mode = strtol(details["perms"].c_str(), 0, 8);
    const string *type = getdetail(details, "type");
    if(type) {
      if(*type == "link") {
        ++total_links;
        // Just make the symlink
        hostfs->symlink(recoder.convert(details["target"]), tmpname);
      } else if(*type == "dir") {
        ++total_dirs;
        // If the directory already exists assume that's intentional, but
        // warn about it.
        if(hostfs->exists(fullname)) {
          warning("%s already exists, leaving it alone", fullname.c_str());
          continue;
        }
        if(!permissions) mode = 0777;
        hostfs->mkdir(tmpname, mode);
      } else if(*type == "chr" or *type == "blk") {
        ++total_devs;
        const mode_t devtype = (*type == "chr" ? S_IFCHR : S_IFBLK);
        if(!permissions) mode = 0666;
        hostfs->mknod(tmpname, mode | devtype,
                      strtol(details["rdev"].c_str(), 0, 10));
      } else if(*type == "socket") {
        ++total_socks;
        if(hostfs != &local) {
          warning("%s: cannot restore socket to remote filesystem",
                  fullname.c_str());
          continue;
        }
        struct sockaddr_un sun;

        if(tmpname.size() >= sizeof sun.sun_path) {
          error("%s: socket path name too long", tmpname.c_str());
          continue;
        }
        memset(&sun, 0, sizeof sun);
        sun.sun_family= AF_UNIX;
        strcpy(sun.sun_path, tmpname.c_str());
        int fd;
        if((fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
          fatal("error creating socket: %s", strerror(errno));
        if(bind(fd, (const struct sockaddr *)&sun, sizeof sun) < 0) {
          error("error binding socket to %s: %s",
                sun.sun_path, strerror(errno));
          continue;
        }
        if(close(fd) < 0) fatal("error calling close: %s", strerror(errno));
      } else {
        error("unknown file type %s", type->c_str());
        continue;
      }
    } else {
      ++total_regular_files;
      // Regular file
      if(const string *data = getdetail(details, "data")) {
        ++small_files;
        // We have the file data to hand
        File *f = hostfs->open(tmpname, Overwrite);
        try {
          f->put(*data);
          f->flush();
        } catch(...) {
          delete f;
          throw;
        }
        delete f;
      } else if(const string *hash = getdetail(details, HASH_NAME)) {
        // File was saved by hash
        File *src = 0, *dst = 0;
        uint8_t h[HASH_SIZE];

        hashdecode(*hash, h);
        const string hp = repo + "/" + HASH_NAME + "/" + hashpath(h);

        try {
          static char buffer[4096];
          int n;

          dst = hostfs->open(tmpname, Overwrite);
          src = backupfs->open(hp, ReadOnly);
          while((n = src->getbytes(buffer, sizeof buffer, false)))
            dst->put(buffer, n);
          dst->flush();
        } catch(...) {
          if(src) delete src;
          if(dst) delete dst;
          throw;
        }
        delete src;
        delete dst;
      } else {
        // Must be from the future
        error("%s does not have a known hash", name.c_str());
        continue;
      }
      if(inode) {
        // There are other links to this file which we might encounter in the
        // future
        inodes[inodenum] = fullname;
      }
    }
    // Fix permissions and rename into place
    if(permissions)
      hostfs->lchown(tmpname, 
                     string2uid(details["uid"]),
                     string2gid(details["gid"]));
    if(!type or *type != "link") {
      if(permissions)
        hostfs->chmod(tmpname, mode);
      const time_t atime = strtoull(details["atime"].c_str(), 0, 10);
      const time_t mtime = strtoull(details["mtime"].c_str(), 0, 10);
      // Directory timestamps will be busted by creating files in them so keep
      // them around for post-hoc fixup.
      if(type and *type == "dir")
        dirtimes.push_back(dirstamp(fullname, atime, mtime));
      else
        hostfs->utimes(tmpname, atime, mtime);
    }
    hostfs->rename(tmpname, fullname);
  }
  delete f;
  // Fix up directory timestamps now that all the contents have been created
  if(verbose)
    fprintf(stderr, "fixing directory timestamps\n");
  for(list<dirstamp>::const_iterator it = dirtimes.begin();
      it != dirtimes.end();
      ++it)
    hostfs->utimes(it->dir, it->atime, it->mtime);
}

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
