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

// Hashing --------------------------------------------------------------------

// Simple wrapper for hashing data
Hash::Hash() {
  int e = SHA1Reset(&c);
  if(e)
    throw HashError(e);
}

Hash::~Hash() {
}

// A set of hashes, implemented as a hashtable.
HashSet::HashSet() {
  for(size_t n = 0; n < HASHTABLE_SIZE; ++n)
    nodes[n] = 0;
}

HashSet::~HashSet() {
  struct node *np, *nq;
  for(size_t n = 0; n < HASHTABLE_SIZE; ++n) {
    np = nodes[n];
    while((nq = np)) {
      np = np->next;
      delete nq;
    }
  }
}

void HashSet::insert(const uint8_t h[HASH_SIZE]) {
  const size_t n = hash(h);
  node *np;
  
  for(np = nodes[n]; np && memcmp(h, np->h, HASH_SIZE); np = np->next)
    ;
  if(!np) {
    np = new node;
    np->next = nodes[n];
    memcpy(np->h, h, HASH_SIZE);
    nodes[n] = np;
  }
}

void HashSet::stats() const {
  size_t nnodes = 0, maxchain = 0, nonemptybuckets = 0;
  for(size_t n = 0; n < HASHTABLE_SIZE; ++n) {
    if(nodes[n]) {
      ++nonemptybuckets;
      size_t thischain = 0;
      for(const node *np = nodes[n]; np; np = np->next)
        ++thischain;
      nnodes += thischain;
      if(thischain > maxchain) maxchain = thischain;
    }
  }
  fprintf(stderr, "Nodes in hash:              %zu\n", nnodes);
  fprintf(stderr, "Nonempty hash buckets:      %zu\n", nonemptybuckets);
  fprintf(stderr, "Mean nonempty chain length: %g\n",
          (double)nnodes / nonemptybuckets);
  fprintf(stderr, "Maximum chain length:       %zu\n", maxchain);
}

bool HashSet::has(const uint8_t h[HASH_SIZE]) const {
  const size_t n = hash(h);
  node *np;
  
  for(np = nodes[n]; np && memcmp(h, np->h, HASH_SIZE); np = np->next)
    ;
  return !!np;
}

void hashfile(Filesystem *fs, const string &path, uint8_t h[HASH_SIZE],
              bool mmap_hint) {
  Hash ho;
  char buffer[4096];

  if(fs == &local && mmap_hint) {
    int fd = open(path.c_str(), O_RDONLY);
    struct stat sb;
    void *m = 0;
    off_t n, size = 0;

    if(fd < 0) throw FileError("opening", path, errno);
    try {
      if(fstat(fd, &sb) < 0) throw FileError("fstat", path, errno);
      n = 0;
      while(n < sb.st_size) {
        const off_t left = sb.st_size - n;
        size = left > MAXMAP ? MAXMAP : left;
        m = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, n);
        if(m == (void *)-1) throw FileError("mapping", path, errno);
        madvise(m, size, MADV_SEQUENTIAL);
        ho.write(m, size);
        if(munmap(m, size) < 0) throw FileError("unmapping", path, errno);
        m = 0;
        n += size;
      }
    } catch(...) {
      if(fd >= 0) close(fd);
      if(m && m != (void *)-1) munmap(m, size);
      throw;
    }
    if(close(fd) < 0) throw FileError("closing", path, errno);
    ++hash_mmap;
  } else {
    File *f = fs->open(path, ReadOnly);
    int n;

    try {
      while((n = f->getbytes(buffer, sizeof buffer, false)))
        ho.write(buffer, n);
    } catch(...) {
      delete f;
      throw;
    }
    delete f;
    ++hash_read;
  }
  memcpy(h, ho.value(), HASH_SIZE);
}

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
/* arch-tag:rEuzDrCx3Qc4W4lucX8apw */
