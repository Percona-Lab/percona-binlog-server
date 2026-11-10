# ==== Purpose ====
#
# Wait for a process to terminate.
#
# ==== Usage ====
#
# --let $proc_pid_file = process PID file
# [--let $proc_termination_timeout = timeout in seconds]
# --source ../include/wait_for_proc_to_terminate.inc
#
# Parameters:
#   $proc_pid_file
#     File with the PID of the process to wait for. The file is not
#     removed.
#
#   $proc_termination_timeout
#     Maximum number of seconds to wait. Defaults to 60.
#
if ($proc_pid_file == "")
{
  --die ERROR IN TEST: 'proc_pid_file' parameter not specified
}
if ($proc_termination_timeout == "")
{
  --let $proc_termination_timeout = 60
}

--let $proc_termination_iteration = 0
--let $proc_running = 1
while ($proc_termination_iteration < $proc_termination_timeout)
{
  if ($proc_running)
  {
    --error 0, 1
    --exec kill -0 `cat $proc_pid_file` 2>/dev/null
    --let $proc_kill_status = $__error
    if ($proc_kill_status != 0)
    {
      --let $proc_running = 0
      --let $proc_termination_iteration = $proc_termination_timeout
    }
    if ($proc_kill_status == 0)
    {
      --sleep 1
      --inc $proc_termination_iteration
    }
  }
}
if ($proc_running)
{
  --die Process did not terminate within the configured timeout
}
