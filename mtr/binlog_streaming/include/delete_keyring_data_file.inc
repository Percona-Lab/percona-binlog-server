#
# Deletes the keyring data file in JSON format for the Binlog Server Utility
#
# Usage:
#   --let $binsrv_keyring_data_file_path = $MYSQL_TMP_DIR/keyring_data.json
#   --source generate_keyring_data_file.inc

--echo
--echo *** Deleting keyring data file in JSON format.
--remove_file $binsrv_keyring_data_file_path
