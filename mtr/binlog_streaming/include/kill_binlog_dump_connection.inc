# ==== Purpose ====
#
# Kill the server-side Binlog Dump connection left by a terminated Binlog
# Server Utility process, if one is still present.
#
# ==== Usage ====
#
# --source ../include/kill_binlog_dump_connection.inc
#

--let $binlog_dump_connection_id = `SELECT ID FROM performance_schema.processlist WHERE COMMAND = 'Binlog Dump'`
if ($binlog_dump_connection_id != '')
{
  --disable_query_log
  eval KILL CONNECTION $binlog_dump_connection_id;
  --enable_query_log
}
