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

--let $at_startup_fixer =
--let $checkpoint_remote_source_mode =
if ($checkpoint_replication_mode == position)
{
  --let $checkpoint_remote_source_mode = BINLOG-DUMP-NON-GTIDS
}
if ($checkpoint_replication_mode == gtid)
{
  --let $checkpoint_remote_source_mode = BINLOG-DUMP-GTIDS
  # currently PBS does not follow the behavior of the MySQL Server regarding
  # the "at startup" annotation in the binlog FORMAT_DESCRIPTION event
  # so, here we remove the phrase "at startup" at the end of the line and
  # replace checksum with '0x00000000' for every line matching 'Start:'
  # (identifying FDE)
  --let $at_startup_fixer = | sed -E '/Start:/ { s/0x[[:xdigit:]]{8}/0x00000000/; s/[[:space:]]+at startup\$//; }'
}
if (!$checkpoint_remote_source_mode)
{
  --die ERROR IN TEST: 'checkpoint_replication_mode' parameter must be 'position' or 'gtid'
}

--let $checkpoint_gtid_options =
if ($checkpoint_gtids)
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
  --get-server-public-key
  --start-position=$checkpoint_binlog_position --to-last-log "$checkpoint_binlog_name" > $checkpoint_roundtrip_dump 2>/dev/null;
--let $checkpoint_roundtrip_dump_filtered = $checkpoint_roundtrip_dump.filtered
--exec cat $checkpoint_roundtrip_dump $at_startup_fixer | grep -E "^#[[:digit:]]+" > $checkpoint_roundtrip_dump_filtered

--let $checkpoint_direct_dump = $MYSQL_TMP_DIR/rs_compare_dumps_direct.sql
exec $MYSQL_BINLOG
  --read-from-remote-source=$checkpoint_remote_source_mode $checkpoint_gtid_options
  --host=$binsrv_connection_host --user=$binsrv_connection_user
  --start-position=$checkpoint_binlog_position --to-last-log "$checkpoint_binlog_name" > $checkpoint_direct_dump 2>/dev/null;
--let $checkpoint_direct_dump_filtered = $checkpoint_direct_dump.filtered
--exec cat $checkpoint_direct_dump $at_startup_fixer | grep -E "^#[[:digit:]]+" > $checkpoint_direct_dump_filtered

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
--let $checkpoint_gtid_options =
--let $checkpoint_remote_source_mode =
