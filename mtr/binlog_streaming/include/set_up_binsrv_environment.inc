#
# Creates a data directory and a JSON configuration file for the Binlog Server Utility
#
# Usage:
#   --let $binsrv_connect_timeout = 20
#   --let $binsrv_read_timeout = 60
#   --let $binsrv_idle_time = 10
#   --let $binsrv_verify_checksum = TRUE | FALSE
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
  --let $qualified_bucket = $MTR_BINSRV_AWS_S3_BUCKET
  if ($MTR_BINSRV_AWS_S3_REGION)
  {
    --let $qualified_bucket = $qualified_bucket.$MTR_BINSRV_AWS_S3_REGION
  }
  --let $binsrv_storage_path = `SELECT CONCAT('/mtr-', UUID())`
  eval SET @storage_uri = CONCAT('s3://', '$MTR_BINSRV_AWS_ACCESS_KEY_ID', ':', '$MTR_BINSRV_AWS_SECRET_ACCESS_KEY', '@', '$qualified_bucket',  '$binsrv_storage_path');
  --let $aws_s3_bucket = $MTR_BINSRV_AWS_S3_BUCKET
}

--let $binsrv_log_path = $MYSQL_TMP_DIR/binsrv_utility.log
eval SET @log_path = '$binsrv_log_path';

SET @delimiter_pos = INSTR(USER(), '@');
SET @connection_user = SUBSTRING(USER(), 1, @delimiter_pos - 1);
SET @connection_host = SUBSTRING(USER(), @delimiter_pos + 1);
SET @connection_host = IF(@connection_host = 'localhost', '127.0.0.1', @connection_host);

eval SET @binsrv_config_json = JSON_OBJECT(
  'logger', JSON_OBJECT(
    'level', 'trace',
    'file', @log_path
  ),
  'connection', JSON_OBJECT(
     'host', @connection_host,
     'port', @@global.port,
     'user', @connection_user,
     'password', '',
     'connect_timeout', $binsrv_connect_timeout,
     'read_timeout', $binsrv_read_timeout,
     'write_timeout', 60
  ),
  'replication', JSON_OBJECT(
     'server_id', @@server_id + 1,
     'idle_time', $binsrv_idle_time,
     'verify_checksum', $binsrv_verify_checksum
  ),
  'storage', JSON_OBJECT(
     'uri', @storage_uri
  )
);

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
