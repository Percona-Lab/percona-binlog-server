# Start the Binlog Server Utility pull command in background.
#
# Usage:
#   --let $binsrv_spawn_cmd_line = $BINSRV pull ... & echo \$! > $binsrv_pid_file
#   --source ../include/trigger_binsrv_pull.inc

--let EXPORTED_BINSRV_SPAWN_CMD_LINE = $binsrv_spawn_cmd_line
--perl
  use strict;
  use warnings;
  my $cmd = $ENV{'EXPORTED_BINSRV_SPAWN_CMD_LINE'};
  system("$cmd");
EOF
