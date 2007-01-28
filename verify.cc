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

// Verify ---------------------------------------------------------------------

void do_verify() {
  if(repo == "") fatal("no repository specified");
  if(root != "") fatal("root specified for --verify");
  if(indexfile == "") fatal("no index specified");
  File *f = backupfs->open(indexfile, ReadOnly);
  map<string,string> details;
  while(readIndexLine(f, details)) {
    const string &name = details["name"];
    const string *type = getdetail(details, "type");

    if(!type) {
      if(getdetail(details, "data"))
        ;                               // ok
      else if(const string *hash = getdetail(details, HASH_NAME)) {
        uint8_t h[HASH_SIZE], actual_hash[HASH_SIZE];
        hashdecode(*hash, h);
        const string hp = repo + "/" + HASH_NAME + "/" + hashpath(h);
        try {
          hashfile(backupfs, hp, actual_hash);
          if(memcmp(h, actual_hash, HASH_SIZE)) {
            error("%s: hash mismatch for %s", name.c_str(), hp.c_str());
            if(detectbogus)
              backupfs->remove(hp);
          }
        } catch(FileError &e) {
          if(e.error() != ENOENT)
            throw;
          error("%s: cannot find %s", name.c_str(), hp.c_str());
        }
      } else
        error("%s: no known hash", name.c_str());
    }
  }
  delete f;
}

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
