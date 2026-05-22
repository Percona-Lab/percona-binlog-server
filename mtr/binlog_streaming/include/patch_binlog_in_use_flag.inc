# Copy a binlog file and clear LOG_EVENT_BINLOG_IN_USE_F in the patched copy.
#
# Usage:
#   --let $binlog_file_to_patch = $binlog_base_dir/$binlog_name
#   --let PATCHED_BINLOG_FILE = $MYSQL_TMP_DIR/$binlog_name.patched
#   --source ../include/patch_binlog_in_use_flag.inc

--echo
--echo *** Patching the server version of the binlog file to clear the
--echo *** LOG_EVENT_BINLOG_IN_USE_F (currently in use) flag.
--copy_file $binlog_file_to_patch $PATCHED_BINLOG_FILE

--perl
  use strict;
  use warnings;
  use constant MAGIC_OFFSET => 21;
  my $binlog_file_perl = $ENV{'PATCHED_BINLOG_FILE'};

  open(my $fh, '+<:raw', $binlog_file_perl) or die "Failed to open file: $!";

  seek($fh, MAGIC_OFFSET, 0);
  my $byte;
  read($fh, $byte, 1);

  $byte = ord($byte) & 0xFE;

  seek($fh, MAGIC_OFFSET, 0);
  print $fh pack('C', $byte);

  close($fh);
EOF
