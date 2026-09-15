#
# Creates a JSON configuration file for the Binlog Server Utility
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
#   --let $binsrv_pbs_listener_rsa_public_key_path  = /path/to/pub.pem  (optional)
#   --let $binsrv_pbs_listener_rsa_private_key_path = /path/to/priv.pem (optional)
#   --let $binsrv_pbs_listener_ssl_cert             = /path/to/server-cert.pem (optional)
#   --let $binsrv_pbs_listener_ssl_key              = /path/to/server-key.pem  (optional)
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
     'mode', '$binsrv_replication_mode'
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

# Optional 'pbs_listener' block (see binsrv/pbs_listener_config.hpp).
# The block carries two independent field pairs (RSA key pair for
# caching_sha2_password full auth, and TLS cert/key pair for the SSL
# listener); the block is emitted whenever at least one of the pairs is
# fully set, but ALL FOUR fields are always written - the pair that the
# caller did not set gets emitted as empty strings so nv_tuple_from_json
# always sees every declared field and pbs_listener_config::validate()
# sees the empty pair as "not configured" (which is fine per each pair's
# own "both-set-or-both-empty" invariant). Tests that don't set either
# pair get a plain plaintext-only, RSA-less listener config with no
# 'pbs_listener' block at all.
--let $binsrv_pbs_listener_needs_block = 0
if ($binsrv_pbs_listener_rsa_public_key_path != "")
{
  if ($binsrv_pbs_listener_rsa_private_key_path != "")
  {
    --let $binsrv_pbs_listener_needs_block = 1
  }
}
if ($binsrv_pbs_listener_ssl_cert != "")
{
  if ($binsrv_pbs_listener_ssl_key != "")
  {
    --let $binsrv_pbs_listener_needs_block = 1
  }
}
if ($binsrv_pbs_listener_needs_block)
{
  eval SET @binsrv_config_json = JSON_INSERT(
    @binsrv_config_json, '$.pbs_listener',
    JSON_OBJECT(
      'rsa_public_key_path',  '$binsrv_pbs_listener_rsa_public_key_path',
      'rsa_private_key_path', '$binsrv_pbs_listener_rsa_private_key_path',
      'ssl_cert_path',        '$binsrv_pbs_listener_ssl_cert',
      'ssl_key_path',         '$binsrv_pbs_listener_ssl_key'
    )
  );
}

--let $binsrv_config_file_path = $MYSQL_TMP_DIR/binsrv_config.json
--let $write_var = `SELECT @binsrv_config_json`
--let $write_to_file = $binsrv_config_file_path
--source include/write_var_to_file.inc

SET sql_log_off = @old_sql_log_off;
--enable_query_log
