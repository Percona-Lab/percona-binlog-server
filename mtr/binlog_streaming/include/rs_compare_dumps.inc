# ==== Purpose ====
#
# Dumps binary logs in position or GTID mode starting from the specified
# (binlog name, position) checkpoint up to the last binlog file both from
# the Binlog Server replication source and directly from the MySQL Server
# and compares generated event sequences.
#
# ==== Usage ====
#
# --let $checkpoint_label = after inserting the first batch of rows
# --let $checkpoint_replication_mode = position
# --let $checkpoint_binlog_name = binlog.000001
# --let $checkpoint_binlog_position = 4
# [--let $checkpoint_gtids = uuid:1-10]
# [--let $saved_roundtrip_dump = $MYSQL_TMP_DIR/roundtrip.sql]
# [--let $checkpoint_roundtrip_binlog_name = bnlg.000001]
# --source ../include/rs_compare_dumps.inc
#
# $checkpoint_replication_mode is required and must be "position" or "gtid".
# It selects BINLOG-DUMP-NON-GTIDS or BINLOG-DUMP-GTIDS, respectively.
#
# If $checkpoint_gtids is specified, it is passed via --exclude-gtids to
# both mysqlbinlog commands. $checkpoint_gtids is reset at the end.
#
# An empty $checkpoint_binlog_name means "start from the oldest binlog file
# available" (in this case $checkpoint_binlog_position must be 4).
#
# If $checkpoint_roundtrip_binlog_name is specified, it is used instead of
# $checkpoint_binlog_name as the binlog file name to start from when dumping
# from the Binlog Server replication source (needed in 'gtid' replication
# mode, in which the utility always operates in 'rewrite' mode and its
# storage binlog file names differ from the ones on the MySQL Server).
# $checkpoint_roundtrip_binlog_name is reset at the end.
#
# When the utility operates in 'gtid' replication mode ('rewrite' mode),
# its storage binlog files are split differently and have their own
# ROTATE, FORMAT_DESCRIPTION and PREVIOUS_GTIDS_LOG events, and GTID events
# have renumbered logical timestamps. So, in this mode only transaction
# events are compared, with 'end_log_pos' values, checksums (which cover
# 'end_log_pos') and logical timestamps of GTID events masked out.
#
# If $saved_roundtrip_dump is specified, the unfiltered dump received from
# the Binlog Server replication source is copied to this location before
# the cleanup (e.g. to be restored later). It is the responsibility of the
# caller to remove this file. $saved_roundtrip_dump is reset at the end.
#
# Expected inputs set by set_up_binsrv_environment.inc:
#   $binsrv_connection_host
#   $binsrv_connection_user
#   $binsrv_replication_source_port
#   $binsrv_auth_user
#   $binsrv_auth_password
#   $binsrv_replication_mode

--let $checkpoint_remote_source_mode =
if ($checkpoint_replication_mode == position)
{
  --let $checkpoint_remote_source_mode = BINLOG-DUMP-NON-GTIDS
}
if ($checkpoint_replication_mode == gtid)
{
  --let $checkpoint_remote_source_mode = BINLOG-DUMP-GTIDS
}
if ($checkpoint_remote_source_mode == "")
{
  --die ERROR IN TEST: 'checkpoint_replication_mode' parameter must be 'position' or 'gtid'
}

--let $rewrite_mode_fixer =
if ($binsrv_replication_mode == gtid)
{
  --let $rewrite_mode_fixer = | grep -v -E '[[:space:]](Rotate to|Start: binlog|Previous-GTIDs|Stop)' | sed -E 's/end_log_pos [0-9]+ CRC32 0x[[:xdigit:]]{8}/end_log_pos <pos> CRC32 <crc>/; s/last_committed=[0-9]+[[:space:]]+sequence_number=[0-9]+/last_committed=<lc> sequence_number=<sn>/'
}

--let $resolved_roundtrip_binlog_name = $checkpoint_binlog_name
if ($checkpoint_roundtrip_binlog_name != "")
{
  --let $resolved_roundtrip_binlog_name = $checkpoint_roundtrip_binlog_name
}

--let $checkpoint_gtid_options =
if ($checkpoint_gtids != "")
{
  --let $checkpoint_gtid_options = --exclude-gtids="$checkpoint_gtids"
}

--echo
--echo *** Comparing event sequences from the PBS and MySQL Server starting
--echo *** from checkpoint '$checkpoint_label'.
--let $checkpoint_roundtrip_dump = $MYSQL_TMP_DIR/rs_compare_dumps_roundtrip.sql
exec $MYSQL_BINLOG
  --read-from-remote-source=$checkpoint_remote_source_mode $checkpoint_gtid_options
  --host=$binsrv_connection_host --port=$binsrv_replication_source_port
  --user=$binsrv_auth_user --password=$binsrv_auth_password --default-auth=caching_sha2_password
  --ssl-mode=DISABLED
  --get-server-public-key
  --start-position=$checkpoint_binlog_position --to-last-log "$resolved_roundtrip_binlog_name" > $checkpoint_roundtrip_dump 2>/dev/null;
--let $checkpoint_roundtrip_dump_filtered = $checkpoint_roundtrip_dump.filtered
--exec cat $checkpoint_roundtrip_dump | grep -E "^#[[:digit:]]+" $rewrite_mode_fixer > $checkpoint_roundtrip_dump_filtered

--let $checkpoint_direct_dump = $MYSQL_TMP_DIR/rs_compare_dumps_direct.sql
exec $MYSQL_BINLOG
  --read-from-remote-source=$checkpoint_remote_source_mode $checkpoint_gtid_options
  --host=$binsrv_connection_host --user=$binsrv_connection_user
  --start-position=$checkpoint_binlog_position --to-last-log "$checkpoint_binlog_name" > $checkpoint_direct_dump 2>/dev/null;
--let $checkpoint_direct_dump_filtered = $checkpoint_direct_dump.filtered
--exec cat $checkpoint_direct_dump | grep -E "^#[[:digit:]]+" $rewrite_mode_fixer > $checkpoint_direct_dump_filtered

--diff_files $checkpoint_roundtrip_dump_filtered $checkpoint_direct_dump_filtered

if ($saved_roundtrip_dump)
{
  --copy_file $checkpoint_roundtrip_dump $saved_roundtrip_dump
}

--remove_file $checkpoint_roundtrip_dump
--remove_file $checkpoint_roundtrip_dump_filtered
--remove_file $checkpoint_direct_dump
--remove_file $checkpoint_direct_dump_filtered

--let $saved_roundtrip_dump =
--let $checkpoint_roundtrip_binlog_name =
--let $resolved_roundtrip_binlog_name =
--let $rewrite_mode_fixer =
--let $checkpoint_gtid_options =
--let $checkpoint_remote_source_mode =
