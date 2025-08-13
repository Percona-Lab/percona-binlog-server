--let $lts_series = unknown
if (`SELECT VERSION() LIKE '8.0%'`)
{
  --let $lts_series = v80
}
if (`SELECT VERSION() LIKE '8.4%'`)
{
  --let $lts_series = v84
}

if ($lts_series == unknown)
{
  --die unsupported MySQL Server version
}
if ($lts_series == v80)
{
  --let $stmt_reset_binary_logs_and_gtids = RESET MASTER
  --let $stmt_show_binary_log_status = SHOW MASTER STATUS
}
if ($lts_series == v84)
{
  --let $stmt_reset_binary_logs_and_gtids = RESET BINARY LOGS AND GTIDS
  --let $stmt_show_binary_log_status = SHOW BINARY LOG STATUS
}
