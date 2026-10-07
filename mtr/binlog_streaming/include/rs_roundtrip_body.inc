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
--echo *** Creating source_db.tbl and inserting the first batch of rows.
CREATE DATABASE source_db;
CREATE TABLE source_db.tbl(
  id INT UNSIGNED NOT NULL,
  value VARCHAR(32) NOT NULL,
  PRIMARY KEY(id)
) ENGINE=InnoDB;
INSERT INTO source_db.tbl VALUES (1, 'one'), (2, 'two'), (3, 'three');

--echo
--echo *** Flushing the source binary log for the first time.
FLUSH BINARY LOGS;

--echo
--echo *** Inserting the second batch of rows.
INSERT INTO source_db.tbl VALUES (4, 'four'), (5, 'five'), (6, 'six');

--echo
--echo *** Determining binary log name and position for the second part.
--let $second_part_binlog_name = query_get_value($stmt_show_binary_log_status, File, 1)
--let $second_part_binlog_position = query_get_value($stmt_show_binary_log_status, Position, 1)

--echo
--echo *** Fetching the source binary logs in position mode for the first time.
--exec $BINSRV fetch $binsrv_config_file_path > /dev/null

--echo
--echo *** Starting Binlog Server Utility in background in pull mode for the first time.
--let $proc_command_line = $BINSRV pull $binsrv_config_file_path > /dev/null
--source ../include/start_proc_in_background.inc

--echo
--echo *** Waiting for the replication source listener port ready to accept
--echo *** connections for the first time.
--let $listening_port = $binsrv_replication_source_port
--source ../include/wait_for_listening_port.inc

--let $first_roundtrip_dump = $MYSQL_TMP_DIR/first_rs_roundtrip.sql
--let $checkpoint_label = the beginning of the oldest binlog file
--let $checkpoint_binlog_name =
--let $checkpoint_binlog_position = 4
--let $saved_roundtrip_dump = $first_roundtrip_dump
--source ../include/rs_compare_dumps.inc

--echo
--echo *** Sending SIGTERM signal to the Binlog Server Utility and waiting for
--echo *** the process to terminate for the first time.
--source ../include/terminate_proc.inc

--echo
--echo *** Inserting the third batch of rows.
INSERT INTO source_db.tbl VALUES (7, 'seven'), (8, 'eight'), (9, 'nine');

--echo
--echo *** Flushing the source binary log for the second time.
FLUSH BINARY LOGS;

--echo
--echo *** Inserting the fourth batch of rows.
INSERT INTO source_db.tbl VALUES (10, 'ten'), (11, 'eleven'), (12, 'twelve');

--echo
--echo *** Fetching the source binary logs in position mode for the second time.
--exec $BINSRV fetch $binsrv_config_file_path > /dev/null

--echo *** Starting Binlog Server Utility in background in pull mode for the second time.
--let $proc_command_line = $BINSRV pull $binsrv_config_file_path > /dev/null
--source ../include/start_proc_in_background.inc

--echo
--echo *** Waiting for the replication source listener port ready to accept
--echo *** connections for the second time.
--let $listening_port = $binsrv_replication_source_port
--source ../include/wait_for_listening_port.inc

--let $second_roundtrip_dump = $MYSQL_TMP_DIR/second_rs_roundtrip.sql
--let $checkpoint_label = after inserting the second batch of rows
--let $checkpoint_binlog_name = $second_part_binlog_name
--let $checkpoint_binlog_position = $second_part_binlog_position
--let $saved_roundtrip_dump = $second_roundtrip_dump
--source ../include/rs_compare_dumps.inc

--echo
--echo *** Sending SIGTERM signal to the Binlog Server Utility and waiting for
--echo *** the process to terminate for the second time.
--source ../include/terminate_proc.inc

--echo
--echo *** Moving source_db.tbl to backup_db.tbl.
CREATE DATABASE backup_db;
RENAME TABLE source_db.tbl TO backup_db.tbl;
DROP DATABASE source_db;

--echo
--echo *** Restoring source_db from two replication source dumps.
--exec $MYSQL < $first_roundtrip_dump
--exec $MYSQL < $second_roundtrip_dump

--echo
--echo *** Comparing the restored and backup tables.
--let $diff_tables = source_db.tbl, backup_db.tbl
--source include/diff_tables.inc

--echo
--echo *** Dropping the round-trip databases.
DROP DATABASE source_db;
DROP DATABASE backup_db;

--echo
--echo *** Cleaning up.
--remove_file $first_roundtrip_dump
--remove_file $second_roundtrip_dump
