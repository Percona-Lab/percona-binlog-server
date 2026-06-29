# Terminate the Binlog Server Utility and wait until the process exits.
#
# Usage:
#   --let $binsrv_pid = <pid>
#   --source ../include/terminate_binsrv.inc

--exec kill -s TERM $binsrv_pid
--let EXPORTED_BINSRV_PID = $binsrv_pid
--perl
  use strict;
  use warnings;
  use Errno;
  my $pid = $ENV{'EXPORTED_BINSRV_PID'};
  my $not_present = (!kill(0, $pid) && $! == Errno::ESRCH);
  while (!$not_present) {
    sleep(1);
    $not_present = (!kill(0, $pid) && $! == Errno::ESRCH);
  }
EOF
