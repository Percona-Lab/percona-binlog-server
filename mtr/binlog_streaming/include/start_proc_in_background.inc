# ==== Purpose ====
#
# Start a process in the background and return its PID.
#
# ==== Usage ====
#
# --let $proc_command_line = command with arguments and redirects
# --source ../include/start_proc_in_background.inc
#
# Parameters:
#   $proc_command_line
#     Complete command line to execute before the background operator.
#   $proc_pid_file
#     File where the PID of the started process should be written
#     (optional, defaults to $MYSQL_TMP_DIR/start_proc_in_background.pid)
#
if (!$proc_command_line)
{
  --die ERROR IN TEST: 'proc_command_line' parameter not specified
}

# The spawn goes through Perl's system() rather than --exec because
# mysqltest strips the shell's `$!` before it can be expanded, which
# would leave the PID file empty. Passing the command as an env var
# and executing it from Perl leaves the shell metacharacters intact.
#
if ($proc_pid_file == "")
{
  --let $proc_pid_file = $MYSQL_TMP_DIR/start_proc_in_background.pid
}
--let $proc_spawn_command_line = $proc_command_line & echo \$! > $proc_pid_file
--let EXPORTED_PROC_COMMAND_LINE = $proc_spawn_command_line
--perl
  use strict;
  use warnings;
  my $cmd = $ENV{'EXPORTED_PROC_COMMAND_LINE'};
  system("$cmd");
EOF
