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

// Clean --------------------------------------------------------------------

static unsigned long long clean_recurse(const HashSet *needed, 
                                        const string &path);

// Perform cleanup, taking ARGV as list of live indexes
void do_clean(int argc, char **argv) {
  HashSet *needed;
  uint8_t h[HASH_SIZE];
  map<string,string> details;
  list<const char *> badfiles;

  if(repo == "") fatal("no repository specified");
  if(argc == 0) fatal("no index files specified");
  // construct the set of files that do exist
  needed = new HashSet();
  for(int n = 0; n < argc; ++n) {
    if(verbose)
      fprintf(stderr, "checking %s\n", argv[n]);
    File *f = backupfs->open(argv[n], ReadOnly);
    try {
      try {
        while(readIndexLine(f, details)) {
          if(const string *hash = getdetail(details, HASH_NAME)) {
            hashdecode(*hash, h);
            needed->insert(h);
          }
        }
      } catch(BadHex) {
        badfiles.push_back(argv[n]);
      } catch(BadHexDigit) {
        badfiles.push_back(argv[n]);
      } catch(BadIndexFile &bif) {
        error("%s", bif.what());
        badfiles.push_back(argv[n]);
      }
    } catch(...) {
      delete f;
      throw;
    }
    delete f;
  }
  if(badfiles.size()) {
    for(list<const char *>::const_iterator it = badfiles.begin();
        it != badfiles.end();
        ++it)
      fprintf(stderr, "%s\n", *it);
    fatal("%d bad input files", (int)badfiles.size());
  }
  // report hash stats
  if(verbose)
    needed->stats();
  // delete everything not in the set
  if(verbose)
    fprintf(stderr, "looking for obsolete files\n");
  const unsigned long long deleted = clean_recurse(needed,
                                                   repo + "/" + HASH_NAME);
  if(verbose)
    fprintf(stderr, "found %llu obsolete files\n", deleted);
}

// Recurse through repository, listing/delete obsolete files
static unsigned long long clean_recurse(const HashSet *needed, 
                                        const string &path) {
  list<string> files;
  uint8_t h[HASH_SIZE];
  bool keep;                            // keep this file?
  unsigned long long deleted = 0;

  backupfs->contents(path, files);
  for(list<string>::const_iterator it = files.begin();
      it != files.end();
      ++it) {
    const string &name = *it;
    const string fullname = path + "/" + name;

    switch(backupfs->type(fullname)) {
    case RegularFile:
      try {
        hashdecode(name, h);
        keep = needed->has(h);
        if(detectbogus) {
          // see if the file actually has the right hash
          uint8_t actual_hash[HASH_SIZE];
          hashfile(backupfs, fullname, actual_hash);
          if(memcmp(actual_hash, h, HASH_SIZE))
            keep = false;
        }
      } catch(...) {
        // not a valid hash
        keep = false;
      }
      if(!keep) {
        if(deleteclean) {
          try {
            backupfs->remove(fullname);
          } catch(FileError &e) {
            // don't crap out if we run into a file we cannot delete
            error("%s", e.what());
          }
        } else
          if(puts(fullname.c_str()) < 0)
            fatal("error writing to stdout: %s", strerror(errno));
        ++deleted;
      }
      break;
    case Directory:
      // clean subdirectory
      deleted += clean_recurse(needed, fullname);
      break;
    default:
      // do nothing
      break;
    }
  }
  return deleted;
}

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
