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
#   --let $binsrv_checkpoint_size = 2M (optional)
#   --let $binsrv_checkpoint_interval = 30s (optional)
#   --source set_up_binsrv_environment.inc

--echo
--echo *** Generating a configuration file in JSON format for the Binlog
--echo *** Server utility.

# temporarily disabling MySQL general query log so that AWS credentials
# will not appear in plain in recorded SQL queries
--disable_query_log
SET @old_sql_log_off = @@sql_log_off;
SET sql_log_off = ON;

if ($storage_backend == file)
{
  --let $binsrv_storage_path = $MYSQL_TMP_DIR/storage
  eval SET @storage_uri = CONCAT('file://', '$binsrv_storage_path');
}
if ($storage_backend == s3)
{
  --let $binsrv_buffer_path = $MYSQL_TMP_DIR/buffer
  --let $binsrv_storage_path = `SELECT CONCAT('/mtr-', UUID())`
  --let $aws_s3_bucket = $MTR_BINSRV_AWS_S3_BUCKET
  if ($MTR_BINSRV_AWS_S3_ENDPOINT != '')
  {
    eval SET @storage_uri = CONCAT('http://', '$MTR_BINSRV_AWS_ACCESS_KEY_ID', ':', '$MTR_BINSRV_AWS_SECRET_ACCESS_KEY', '@', '$MTR_BINSRV_AWS_S3_ENDPOINT', '/$MTR_BINSRV_AWS_S3_BUCKET', '$binsrv_storage_path');
  }
  if ($MTR_BINSRV_AWS_S3_ENDPOINT == '')
  {
    --let $qualified_bucket = $MTR_BINSRV_AWS_S3_BUCKET
    if ($MTR_BINSRV_AWS_S3_REGION)
    {
      --let $qualified_bucket = $qualified_bucket.$MTR_BINSRV_AWS_S3_REGION
    }
    eval SET @storage_uri = CONCAT('s3://', '$MTR_BINSRV_AWS_ACCESS_KEY_ID', ':', '$MTR_BINSRV_AWS_SECRET_ACCESS_KEY', '@', '$qualified_bucket',  '$binsrv_storage_path');
  }
}

--let $binsrv_log_path = $MYSQL_TMP_DIR/binsrv_utility.log
eval SET @log_path = '$binsrv_log_path';

if ($binsrv_connection_host == "")
{
  if ($binsrv_connection_user == "")
  {
    --source detect_connection_user_and_host.inc
  }
}

eval SET @binsrv_config_json = JSON_OBJECT(
  'logger', JSON_OBJECT(
    'level', 'trace',
    'file', @log_path
  ),
  'connection', JSON_OBJECT(
     'host', '$binsrv_connection_host',
     'port', @@global.port,
     'user', '$binsrv_connection_user',
     'password', '$binsrv_connection_password',
     'connect_timeout', $binsrv_connect_timeout,
     'read_timeout', $binsrv_read_timeout,
     'write_timeout', 60
  ),
  'replication', JSON_OBJECT(
     'server_id', @@server_id + 1,
     'idle_time', $binsrv_idle_time,
     'verify_checksum', $binsrv_verify_checksum,
     'mode', 'position'
  ),
  'storage', JSON_OBJECT(
     'backend', '$storage_backend',
     'uri', @storage_uri
  )
);
if ($storage_backend == s3)
{
  eval SET @binsrv_config_json = JSON_INSERT(@binsrv_config_json, '$.storage.fs_buffer_directory', '$binsrv_buffer_path');
}

if ($binsrv_checkpoint_size != "")
{
  eval SET @binsrv_config_json = JSON_INSERT(@binsrv_config_json, '$.storage.checkpoint_size', '$binsrv_checkpoint_size');
}

if ($binsrv_checkpoint_interval != "")
{
  eval SET @binsrv_config_json = JSON_INSERT(@binsrv_config_json, '$.storage.checkpoint_interval', '$binsrv_checkpoint_interval');
}

if ($binsrv_ssl_mode != "")
{
  eval SET @binsrv_config_json = JSON_INSERT(@binsrv_config_json, '$.connection.ssl', JSON_OBJECT('mode', '$binsrv_ssl_mode'));
  if ($binsrv_ssl_ca != "")
  {
    eval SET @binsrv_config_json = JSON_INSERT(@binsrv_config_json, '$.connection.ssl.ca', '$binsrv_ssl_ca');
  }
  if ($binsrv_ssl_cert != "")
  {
    eval SET @binsrv_config_json = JSON_INSERT(@binsrv_config_json, '$.connection.ssl.cert', '$binsrv_ssl_cert');
  }
  if ($binsrv_ssl_key != "")
  {
    eval SET @binsrv_config_json = JSON_INSERT(@binsrv_config_json, '$.connection.ssl.key', '$binsrv_ssl_key');
  }
}

if ($binsrv_tls_version != "")
{
  eval SET @binsrv_config_json = JSON_INSERT(@binsrv_config_json, '$.connection.tls', JSON_OBJECT('version', '$binsrv_tls_version'));
}

--let $binsrv_config_file_path = $MYSQL_TMP_DIR/binsrv_config.json
--let $write_var = `SELECT @binsrv_config_json`
--let $write_to_file = $binsrv_config_file_path
--source include/write_var_to_file.inc

SET sql_log_off = @old_sql_log_off;
--enable_query_log

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
