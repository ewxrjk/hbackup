
setstatus() {
  if "$@"; then
    :
  else 
    status=1
  fi
}  

snapshot() {
  local what=$1
  shift

  # make sure no leftover from a previous run
  umount /snap/$what 2>/dev/null || true
  lvremove -f /dev/$hostname/snap$what 2>/dev/null || true

  # create a snapshot of the volume and mount it
  echo " - creating snapshot"
  lvcreate --permission r --size 1000M --snapshot --name snap$what /dev/$hostname/$what
  echo " - mounting snapshot"
  mount /snap/$what

  # do the backup
  echo " - starting backup"
  setstatus "$@"

  # tear down the snapshot
  echo " - unmounting snapshot"
  umount /snap/$what
  echo " - deleting snapshot"
  lvremove -f /dev/$hostname/snap$what
}

# testdir DIR
#
# Throw an error if DIR does not exist
testdir() {
  if test "x$userhost" = x; then
    if test ! -d "$1"; then
      echo "$1 does not exist" 1>&2
      exit 1
    fi
  else
    if ! sftp -b <(echo cd "$1") "$userhost" >/dev/null; then
      echo "$userhost:$1 does not exist" 1>&2
      exit 1
    fi
  fi
}

# makedir DIR
#
# Attempt to create directory DIR
makedir() {
  local sftparg
  if test "x$userhost" = x; then
    mkdir -p "$1"
  else
    if [ ! -z "$sftpserver" ]; then
      sftparg="-s $sftpserver"
    fi
    sftp $sftparg -b <(echo mkdir "$1") "$userhost" || true
  fi
}

setup() {
  # Local hostname
  hostname=$(uname -n|sed 's/\..*//')

  # External disk had better be there
  testdir "$volume"
  # Also it had better have been initiated for use by hbackup
  testdir "$volume/indexes"

  date=$(date +%F)
  today=$volume/indexes/$hostname/$date
  for host in sfere lyonesse kakajou invenita chymax; do
    eval "$host=$volume/indexes/$host/$date"
  done
  makedir $today
}

status=0

