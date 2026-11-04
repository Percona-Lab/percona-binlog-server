#
# Deletes the keyring data file in JSON format for the Binlog Server Utility
#
# Usage:
#   --let $binsrv_keyring_data_file_path = $MYSQL_TMP_DIR/keyring_data.json
#   --let $binsrv_keyring_data_file_silent_generation = 1 (optional)
#   --source generate_keyring_data_file.inc

if (!$binsrv_keyring_data_file_silent_generation)
{
  --echo
  --echo *** Deleting keyring data file in JSON format.
}
--remove_file $binsrv_keyring_data_file_path
