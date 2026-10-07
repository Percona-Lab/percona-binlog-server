# ==== Purpose ====
#
# Dumps binary logs starting from the specified (binlog name, position)
# checkpoint up to the last binlog file both from the Binlog Server
# replication source and directly from the MySQL Server and compares
# generated event sequences.
#
# ==== Usage ====
#
# --let $checkpoint_label = after inserting the first batch of rows
# --let $checkpoint_binlog_name = binlog.000001
# --let $checkpoint_binlog_position = 4
# [--let $saved_roundtrip_dump = $MYSQL_TMP_DIR/roundtrip.sql]
# --source ../include/rs_compare_dumps.inc
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

--echo
--echo *** Comparing event sequences from the PBS and MySQL Server starting
--echo *** from checkpoint '$checkpoint_label'.
--let $checkpoint_roundtrip_dump = $MYSQL_TMP_DIR/rs_compare_dumps_roundtrip.sql
exec $MYSQL_BINLOG
  --read-from-remote-server --host=$binsrv_connection_host --port=$binsrv_replication_source_port
  --user=$binsrv_auth_user --password=$binsrv_auth_password --default-auth=caching_sha2_password
  --start-position=$checkpoint_binlog_position --to-last-log "$checkpoint_binlog_name" > $checkpoint_roundtrip_dump 2>/dev/null;
--let $checkpoint_roundtrip_dump_filtered = $checkpoint_roundtrip_dump.filtered
--exec cat $checkpoint_roundtrip_dump | grep -E "^#[[:digit:]]+" > $checkpoint_roundtrip_dump_filtered

--let $checkpoint_direct_dump = $MYSQL_TMP_DIR/rs_compare_dumps_direct.sql
exec $MYSQL_BINLOG
  --read-from-remote-server --host=$binsrv_connection_host --user=$binsrv_connection_user
  --start-position=$checkpoint_binlog_position --to-last-log "$checkpoint_binlog_name" > $checkpoint_direct_dump 2>/dev/null;
--let $checkpoint_direct_dump_filtered = $checkpoint_direct_dump.filtered
--exec cat $checkpoint_direct_dump | grep -E "^#[[:digit:]]+" > $checkpoint_direct_dump_filtered

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
