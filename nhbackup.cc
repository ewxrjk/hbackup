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

// Options --------------------------------------------------------------------

static const struct option longopts[] = {
  { "backup", no_argument, 0, 'b' },
  { "restore", no_argument, 0, 'r' },
  { "verify", no_argument, 0, 'c' },
  { "cleanup", no_argument, 0, 'C' },
  { "repo", required_argument, 0, 'R' },
  { "index", required_argument, 0, 'I' },
  { "root", required_argument, 0, 'F' },
  { "one-file-system", no_argument, 0, 'x' },
  { "preserve-atime", no_argument, 0, 'a' },
  { "exclude", required_argument, 0, 'X' },
  { "overwrite", no_argument, 0, 'O' },
  { "sftp", required_argument, 0, 's' },
  { "sftp-server", required_argument, 0, 'S' },
  { "delete", no_argument, 0, 'd' },
  { "detect-bogus", no_argument, 0, 'B' },
  { "speedtest", no_argument, 0, 'z' },
  { "no-permissions", no_argument, 0, 'P' },
  { "from-encoding", required_argument, 0, 'f' },
  { "to-encoding", required_argument, 0, 't' },
  { "verbose", no_argument, 0, 'v' },
  { "version", no_argument, 0, 'V' },
  { "help", no_argument, 0, 'h' },
  { 0, 0, 0, 0 }
};

// Command line ---------------------------------------------------------------

// Display usage message
static void help() {
  if(printf("nhbackup --backup|--restore|--verify OPTIONS\n"
            "nhbackup --cleanup OPTIONS INDEXES...\n"
            "\n"
            "  -b, --backup           Save from ROOT to REPO/INDEX\n"
            "  -r, --restore          Restore from REPO/INDEX to ROOT\n"
            "  -c, --verify           Verify REPO/INDEX\n"
            "  -C, --cleanup          Cleanup REPO against INDEXES\n"
            "  -R, --repo REPO        Specify repository\n"
            "  -I, --index INDEX      Specify index\n"
            "  -F, --root ROOT        Specify root\n"
            "  -x, --one-file-system  Don't cross fs boundaries (--backup)\n"
            "  -a, --preserve-atime   Don't change last read times (--backup)\n"
            "  -X, --exclude PATTERN  Exclude files (--backup)\n"
            "  -O, --overwrite        Overwrite index (--backup)\n"
            "  -s, --sftp USER@HOST   Repository is over sftp\n"
            "  -S, --sftp-server PATH  Path to remove SFTP server\n"
            "  -d, --delete           Delete obsolete files (--cleanup)\n"
            "  -B, --detect-bogus     Detect bogus files\n"
            "  -P, --no-permissions   Don't restore permissions (--restore)\n"
            "  -f, --from-encoding ENCODING\n"
            "  -t, --to-encoding ENCODING\n"
            "                         Convert filenames (--restore)\n"
            "  -v, --verbose          Verbose mode\n"
            "  -h, --help             Display usage message\n"
            "  -V, --version          Display version string\n") < 0)
    fatal("error writing to stdout: %s", strerror(errno));
}

// Display version number
static void display_version() {
  if(printf("nhbackup %s\n", version) < 0)
    fatal("error writing to stdout: %s", strerror(errno));
}

int main(int argc, char **argv) {
  int n;
  int backup = 0, restore = 0, verify = 0, clean = 0, speedtest = 0;

  // Assumption checking
  assert('0' == 48);
  assert('a' == 97);
  assert('A' == 65);
  assert(UCHAR_MAX == 255);
  while((n = getopt_long(argc, argv, "brcCR:I:F:xaX:Os:vhBSVzPf:t:",  longopts, 0))
        >= 0) {
    switch(n) {
    case 'b': backup = 1; break;
    case 'r': restore = 1; break;
    case 'c': verify = 1; break;
    case 'C': clean = 1; break;
    case 'R': repo = optarg; break;
    case 'I': indexfile = optarg; break;
    case 'F': root = optarg; break;
    case 'x': crossfs = 0; break;
    case 'a': preserve_atime = 1; break;
    case 'X': exclusions.add(optarg); break;
    case 'O': overwrite_index = 1; break;
    case 's': sftphost = optarg; break;
    case 'd': deleteclean = 1; break;
    case 'v': verbose = 1; break;
    case 'B': detectbogus = true; break;
    case 'S': sftpserver = optarg; break;
    case 'z': speedtest = 1; break;
    case 'P': permissions = 0; break;
    case 'f': from_encoding = optarg; break;
    case 't': to_encoding = optarg; break;
    case 'h': help(); exit(0);
    case 'V': display_version(); exit(0);
    default: exit(-1);
    }
  }
  if(backup + restore + verify + clean + speedtest != 1)
    fatal("inconsistent options");
  try {
    signal(SIGPIPE, SIG_IGN);
    if(sftphost != "")
      backupfs = new SftpFilesystem(sftphost);
    if(backup) {
      if(from_encoding || to_encoding)
        fatal("encoding translation not supported for backup");
      do_backup();
      if(verbose)
        fprintf(stderr, 
                "Regular files:        %8llu\n"
                "Directories:          %8llu\n"
                "Links:                %8llu\n"
                "Devices:              %8llu\n"
                "Sockets:              %8llu\n"
                "Unknown:              %8llu\n"
                "New hashes:           %8llu\n"
                "Files mapped to hash: %8llu\n"
                "Files read to hash:   %8llu\n"
                "Tiny files:           %8llu\n",
                total_regular_files, total_dirs, total_links, total_devs,
                total_socks,
                unknown_files, new_hashes, hash_mmap, hash_read, small_files);
    } else if(restore) {
      do_restore();
      if(verbose)
        fprintf(stderr,
                "Regular files:        %8llu\n"
                "Directories:          %8llu\n"
                "Links:                %8llu\n"
                "Devices:              %8llu\n"
                "Sockets:              %8llu\n"
                "Tiny files:           %8llu\n"
                "Hard links:           %8llu\n",
                total_regular_files, total_dirs, total_links, total_devs,
                total_socks,
                small_files, total_hardlinks);
    } else if(verify)
      do_verify();
    else if(clean) {
      if(indexfile != "")
        fatal("--index is not compatible with --clean");
      do_clean(argc - optind, argv + optind);
    } else if(speedtest)
      do_speedtest();
  } catch (exception &e) {
    fatal("%s", e.what());
  }
  if(warnings)
    fprintf(stderr, "*** %llu warnings\n", warnings);
  if(errors)
    fprintf(stderr, "*** %llu errors\n", errors);
  if(fclose(stdout) < 0)
    fatal("error closing stdout: %s", strerror(errno));
  return !!errors;
}

/*
Local Variables:
mode:c++
c-basic-offset:2
comment-column:40
fill-column:79
indent-tabs-mode:nil
End:
*/
