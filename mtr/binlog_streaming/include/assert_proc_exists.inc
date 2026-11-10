# ==== Purpose ====
#
# Assert that a process is still running.
#
# ==== Usage ====
#
# --let $proc_pid_file = process PID file
# [--let $proc_assert_text = description of the expected state]
# --source ../include/assert_proc_exists.inc
#
# Parameters:
#   $proc_pid_file
#     File with the PID of the process to check.
#
#   $proc_assert_text
#     Description of the expected state printed if the process is not
#     running (optional). It is reset at the end.
#
if ($proc_pid_file == "")
{
  --die ERROR IN TEST: 'proc_pid_file' parameter not specified
}

--error 0, 1
--exec kill -0 `cat $proc_pid_file` 2>/dev/null
if ($__error != 0)
{
  # 'die' does not expand variables, so printing the description first
  --echo ######## Process assertion failed: $proc_assert_text ########
  --die Process is not running
}

--let $proc_assert_text =
