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

// Recording ------------------------------------------------------------------

unsigned long long total_regular_files, total_dirs, total_links, total_devs;
unsigned long long total_socks, total_hardlinks;
unsigned long long unknown_files;
unsigned long long new_hashes;
unsigned long long hash_mmap;
unsigned long long hash_read;
unsigned long long small_files;
unsigned long long hints_used;

unsigned long long errors;       // error count
unsigned long long warnings;     // warning count

string repo, indexfile, root, sftphost;
int crossfs = 1, preserve_atime, overwrite_index, deleteclean, verbose;
int permissions = 1;
bool detectbogus;
Exclusions exclusions;
const char *sftpserver;
bool recheckhash = true;

Filesystem *hostfs = &local, *backupfs = &local;
const char *from_encoding, *to_encoding;

string hintfile;

/*
Local Variables:
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
