# ==== Purpose ====
#
# Send SIGTERM to a process and wait for it to terminate gracefully.
#
# ==== Usage ====
#
# --let $proc_pid = process PID
# [--let $proc_termination_timeout = timeout in seconds]
# --source ../include/terminate_proc.inc
#
# Parameters:
#   $proc_pid
#     PID of the process to terminate.
#
#   $proc_termination_timeout
#     Maximum number of seconds to wait. Defaults to 30.
#
if (!$proc_pid)
{
  --die ERROR IN TEST: 'proc_pid' parameter not specified
}
if (!$proc_termination_timeout)
{
  --let $proc_termination_timeout = 60
}

--exec kill -s TERM $proc_pid

--let $proc_termination_iteration = 0
--let $proc_running = 1
while ($proc_termination_iteration < $proc_termination_timeout)
{
  if ($proc_running)
  {
    --error 0, 1
    --exec kill -0 $proc_pid 2>/dev/null
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
