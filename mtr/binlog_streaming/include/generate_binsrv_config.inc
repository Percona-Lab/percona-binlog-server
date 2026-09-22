#
# Creates a JSON configuration file for the Binlog Server Utility
#
# Sets (as an output for the caller):
#   $binsrv_replication_source_port - TCP port on which the utility
#     listens for downstream replicas (@@global.port + 1000, unique per
#     MTR worker).
#
# Usage:
#   --let $binsrv_log_level = trace | debug | info | warning | error | fatal (optional, default: trace)
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
#   --let $binsrv_auth_user = rpl (optional, default: rpl)
#   --let $binsrv_auth_password = password (optional, default: password)
#   --source set_up_binsrv_environment.inc

--echo
--echo *** Generating a configuration file in JSON format for the Binlog
--echo *** Server utility.

# temporarily disabling MySQL general query log so that AWS credentials
# will not appear in plain in recorded SQL queries
--disable_query_log
SET @old_sql_log_off = @@sql_log_off;
SET sql_log_off = ON;

--let $binsrv_encryption_enabled = 0
if ($binsrv_encryption_cipher != "")
{
  if ($binsrv_encryption_kek_id != "")
  {
    --let $binsrv_encryption_enabled = 1
  }
}

if ($storage_backend == file)
{
  --let $binsrv_storage_path = $MYSQL_TMP_DIR/storage
  eval SET @storage_uri = CONCAT('file://', '$binsrv_storage_path');
}
if ($storage_backend == s3)
{
  --let $binsrv_buffer_path = $MYSQL_TMP_DIR/buffer
  if ($binsrv_storage_uuid == "")
  {
    --let $binsrv_storage_uuid = `SELECT UUID()`
  }
  --let $binsrv_storage_path = `SELECT CONCAT('/mtr-', '$binsrv_storage_uuid')`
  --let $aws_s3_bucket = $MTR_BINSRV_AWS_S3_BUCKET
  if ($MTR_BINSRV_AWS_S3_ENDPOINT != '')
  {
    eval SET @storage_uri = CONCAT('http://', '$MTR_BINSRV_AWS_ACCESS_KEY_ID', ':', REPLACE('$MTR_BINSRV_AWS_SECRET_ACCESS_KEY', '/', '%2F'), '@', '$MTR_BINSRV_AWS_S3_ENDPOINT', '/', '$MTR_BINSRV_AWS_S3_BUCKET', '$binsrv_storage_path');
  }
  if ($MTR_BINSRV_AWS_S3_ENDPOINT == '')
  {
    --let $qualified_bucket = $MTR_BINSRV_AWS_S3_BUCKET
    if ($MTR_BINSRV_AWS_S3_REGION)
    {
      --let $qualified_bucket = $qualified_bucket.$MTR_BINSRV_AWS_S3_REGION
    }
    eval SET @storage_uri = CONCAT('s3://', '$MTR_BINSRV_AWS_ACCESS_KEY_ID', ':', REPLACE('$MTR_BINSRV_AWS_SECRET_ACCESS_KEY', '/', '%2F'), '@', '$qualified_bucket', '$binsrv_storage_path');
  }
}

if ($binsrv_log_level == "")
{
  --let $binsrv_log_level = trace
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

# The Binlog Server exposes its own MySQL-compatible listener in 'pull'
# mode. To let multiple MTR workers run pull-mode tests in parallel, each
# worker gets its own listening port derived from the worker's MySQL port
# (worker port + 1000). The +1000 offset intentionally jumps well beyond
# MTR's per-worker port allocation (empirically MTR reserves ports within
# ~200 of each worker's @@port on GitHub Actions runners), so the derived
# listener port never collides with a neighbour worker's mysqld/mysqlx or
# other MTR-reserved ports. An earlier +100 offset produced bind failures
# ("Address already in use") on CI when the derived port fell inside a
# sibling worker's range. We expose the computed value via
# $binsrv_replication_source_port so the test can reuse it (e.g. when
# connecting a probe client to the listener).
--let $binsrv_replication_source_port = `SELECT @@global.port + 1000`

if ($binsrv_auth_user == "")
{
  --let $binsrv_auth_user = rpl
}
if ($binsrv_auth_password == "")
{
  --let $binsrv_auth_password = password
}

eval SET @binsrv_config_json = JSON_OBJECT(
  'logger', JSON_OBJECT(
    'level', '$binsrv_log_level',
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
     'mode', '$binsrv_replication_mode'
  ),
  'replication_source', JSON_OBJECT(
     'port', $binsrv_replication_source_port,
     'read_timeout', 60,
     'write_timeout', 60,
     'authentication', JSON_OBJECT(
        'user', '$binsrv_auth_user',
        'password', '$binsrv_auth_password',
        'plugin', 'caching_sha2_password'
     )
  ),
  'storage', JSON_OBJECT(
     'backend', '$storage_backend',
     'uri', @storage_uri
  )
);

if ($binsrv_rewrite_file_size != "")
{
  eval SET @binsrv_config_json = JSON_INSERT(@binsrv_config_json, '$.replication.rewrite', JSON_OBJECT('base_file_name', 'bnlg', 'file_size', '$binsrv_rewrite_file_size'));
}

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


if ($binsrv_keyring_data_file_path != "")
{
  eval SET @binsrv_config_json = JSON_INSERT(
    @binsrv_config_json, '$.keyring',
    JSON_OBJECT('uri', CONCAT('file://', '$binsrv_keyring_data_file_path'))
  );
}
if ($binsrv_encryption_enabled)
{
  eval SET @binsrv_config_json = JSON_INSERT(
    @binsrv_config_json, '$.storage.encryption',
    JSON_OBJECT(
      'format', 'generic',
      'kek_id', '$binsrv_encryption_kek_id',
      'cipher', '$binsrv_encryption_cipher'
    )
  );
}

--let $binsrv_config_file_path = $MYSQL_TMP_DIR/binsrv_config.json
--let $write_var = `SELECT @binsrv_config_json`
--let $write_to_file = $binsrv_config_file_path
--source include/write_var_to_file.inc

SET sql_log_off = @old_sql_log_off;
--enable_query_log
