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
#
# Output:
#   $proc_pid
#     PID of the started process.
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
# The same Perl block also reads the PID file back and writes a tiny
# `--let $proc_pid = N` snippet that we then --source. That keeps
# start_proc_in_background.inc free of any SQL path, so callers can
# use it while the MySQL server is down (for example between
# include/shutdown_mysqld.inc and include/start_mysqld.inc), where
# include/read_file_to_var.inc could not run at all.

--let $proc_pid_file = $MYSQL_TMP_DIR/start_proc_in_background.pid
--let $proc_pid_snippet = $MYSQL_TMP_DIR/start_proc_in_background.pid.inc
--let $proc_spawn_command_line = $proc_command_line & echo \$! > $proc_pid_file
--let EXPORTED_PROC_COMMAND_LINE = $proc_spawn_command_line
--let EXPORTED_PROC_PID_FILE = $proc_pid_file
--let EXPORTED_PROC_PID_SNIPPET = $proc_pid_snippet
--perl
  use strict;
  use warnings;
  my $cmd = $ENV{'EXPORTED_PROC_COMMAND_LINE'};
  system("$cmd");
  open my $in, '<', $ENV{'EXPORTED_PROC_PID_FILE'}
    or die "cannot read PID file $ENV{'EXPORTED_PROC_PID_FILE'}: $!";
  my $pid = <$in>;
  close $in;
  chomp $pid if defined $pid;
  die "spawned process PID not captured"
    unless defined $pid && $pid =~ /^\d+$/;
  open my $out, '>', $ENV{'EXPORTED_PROC_PID_SNIPPET'}
    or die "cannot write PID snippet $ENV{'EXPORTED_PROC_PID_SNIPPET'}: $!";
  print $out "--let \$proc_pid = $pid\n";
  close $out;
EOF

--source $proc_pid_snippet
--remove_file $proc_pid_file
--remove_file $proc_pid_snippet
