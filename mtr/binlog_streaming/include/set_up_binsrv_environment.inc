#
# Creates a data directory and a JSON configuration file for the Binlog Server Utility
#
# Usage:
#   --let $binsrv_connection_user = repl_user (optional)
#   --let $binsrv_connection_host = 127.0.0.1 (optional)
#   --let $binsrv_connection_password = password (optional)
#   --let $binsrv_connect_timeout = 20
#   --let $binsrv_read_timeout = 60
#   --let $binsrv_ssl_mode = disabled | preferred | required | validate_ca | validate_identity (optional)
#   --let $binsrv_ssl_ca = /path/to/ca.pem (optional)
#   --let $binsrv_ssl_cert = /path/to/client-cert.pem (optional)
#   --let $binsrv_ssl_key = /path/to/client-key.pem (optional)
#   --let $binsrv_tls_version = TLSv1.2 (optional)
#   --let $binsrv_idle_time = 10
#   --let $binsrv_verify_checksum = TRUE | FALSE
#   --let $binsrv_replication_mode = position | gtid
#   --let $binsrv_checkpoint_size = 2M (optional)
#   --let $binsrv_checkpoint_interval = 30s (optional)
#   --let $binsrv_rewrite_file_size = 1K (optional)
#   --let $binsrv_encryption_cipher = AES-256-CTR (optional)
#   --let $binsrv_encryption_kek_id = alpha (optional)
#   --let $binsrv_keyring_data_file_path = $MYSQL_TMP_DIR/keyring_data.json (optional)
#   --source set_up_binsrv_environment.inc

--source ../include/generate_binsrv_config.inc

--echo
--echo *** Determining binlog file directory from the server.
--disable_query_log
SET @path_separator = '/';
--source include/check_windows.inc
if ($have_windows) {
  SET @path_separator = '\\';
}
--let $binlog_base_dir = `SELECT LEFT(@@global.log_bin_basename, CHAR_LENGTH(@@global.log_bin_basename) - CHAR_LENGTH(SUBSTRING_INDEX(@@global.log_bin_basename, @path_separator, -1)))`
--enable_query_log


--echo
--echo *** Creating a temporary directory <BINSRV_STORAGE_PATH> for storing
--echo *** binlog files downloaded via the Binlog Server utility.
if ($storage_backend == file)
{
  --mkdir $binsrv_storage_path
}
if ($storage_backend == s3)
{
  --mkdir $binsrv_buffer_path
}
