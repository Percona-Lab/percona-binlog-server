# ==== Purpose ====
#
# Waits until a specified network port is ready to accept connections.
#
# ==== Usage ====
#
# --let $listening_port = 3306
# --source ../include/wait_for_listening_port.inc
#
# Parameters:
#   $listening_port
#     Network port to wait for.
#

# We probe with bash's '/dev/tcp' because it uses a plain connect(2), so each
# attempt is cheap and measures exactly the "listening on the port" state we
# care about.

--let $max_wait = 300
--let $iteration = 0
--let $port_open = 0
while ($iteration < $max_wait)
{
  --error 0, 1
  --exec bash -c "echo > /dev/tcp/127.0.0.1/$listening_port" 2>/dev/null
  --let $port_status = $__error
  if ($port_status == 0)
  {
    --let $port_open = 1
    --let $iteration = $max_wait
  }
  if ($port_status != 0)
  {
    --sleep 1
    --inc $iteration
  }
}
if (!$port_open)
{
  --die The Binlog Server replication source listener did not become reachable within 300 seconds
}
