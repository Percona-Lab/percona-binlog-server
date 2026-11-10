# ==== Purpose ====
#
# Send SIGTERM to a process and wait for it to terminate gracefully.
#
# ==== Usage ====
#
# --let $proc_pid_file = process PID file
# [--let $proc_termination_timeout = timeout in seconds]
# --source ../include/terminate_proc.inc
#
# Parameters:
#   $proc_pid_file
#     File with the PID of the process to terminate.
#
#   $proc_termination_timeout
#     Maximum number of seconds to wait. Defaults to 60.
#
if ($proc_pid_file == "")
{
  --die ERROR IN TEST: 'proc_pid_file' parameter not specified
}

--exec kill -s TERM `cat $proc_pid_file`

--source ../include/wait_for_proc_to_terminate.inc

--remove_file $proc_pid_file
