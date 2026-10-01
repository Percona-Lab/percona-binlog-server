# ==== Purpose ====
#
# Round-trip MySQL -> Binlog Server storage -> COM_BINLOG_DUMP body shared
# between 'rs_roundtrip' and 'rs_roundtrip_encryption'. Each caller is
# responsible for setting up (and tearing down) the Binlog Server environment
# with or without encryption before sourcing this file.
#
# ==== Usage ====
#
# --source ../include/rs_roundtrip_body.inc
#
# Expected inputs set by set_up_binsrv_environment.inc:
#   $binsrv_config_file_path
#   $binsrv_replication_source_port
#   $stmt_show_binary_log_status

--echo
--echo *** Determining the first binary log name.
--let $first_binlog = query_get_value($stmt_show_binary_log_status, File, 1)

--echo
--echo *** Creating source_db.tbl and inserting the first batch of rows.
CREATE DATABASE source_db;
CREATE TABLE source_db.tbl(
  id INT UNSIGNED NOT NULL,
  value VARCHAR(32) NOT NULL,
  PRIMARY KEY(id)
) ENGINE=InnoDB;
INSERT INTO source_db.tbl VALUES (1, 'one'), (2, 'two'), (3, 'three');

--echo
--echo *** Flushing the source binary log.
FLUSH BINARY LOGS;

--echo
--echo *** Inserting the second batch of rows.
INSERT INTO source_db.tbl VALUES (4, 'four'), (5, 'five'), (6, 'six');

--echo
--echo *** Fetching the source binary logs in position mode.
--exec $BINSRV fetch $binsrv_config_file_path > /dev/null

--echo
--echo *** Starting Binlog Server Utility in background in pull mode.
--let $proc_command_line = $BINSRV pull $binsrv_config_file_path > /dev/null
--source ../include/start_proc_in_background.inc

--echo
--echo *** Waiting for the replication source listener port ready to accept
--echo *** connections.
--let $listening_port = $binsrv_replication_source_port
--source ../include/wait_for_listening_port.inc

--echo
--echo *** Dumping the binary logs from the Binlog Server replication source.
--let $roundtrip_dump = $MYSQL_TMP_DIR/rs_roundtrip.sql
--exec $MYSQL_BINLOG --read-from-remote-server --host=127.0.0.1 --port=$binsrv_replication_source_port --user=rpl --password=password --default-auth=caching_sha2_password $first_binlog > $roundtrip_dump 2>/dev/null

--echo
--echo *** Sending SIGTERM signal to the Binlog Server Utility and waiting for
--echo *** the process to terminate
--source ../include/terminate_proc.inc

--echo
--echo *** Moving source_db.tbl to backup_db.tbl.
CREATE DATABASE backup_db;
RENAME TABLE source_db.tbl TO backup_db.tbl;
DROP DATABASE source_db;

--echo
--echo *** Restoring source_db from the replication source dump.
--exec $MYSQL < $roundtrip_dump

--echo
--echo *** Comparing the restored and backup tables.
--let $diff_tables = source_db.tbl, backup_db.tbl
--source include/diff_tables.inc

--echo
--echo *** Dropping the round-trip databases.
DROP DATABASE source_db;
DROP DATABASE backup_db;

--remove_file $roundtrip_dump
