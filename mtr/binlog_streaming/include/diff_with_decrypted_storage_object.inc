#
# Compares a file on a local filesystem with a binlog data object from the
# backend storage, decrypting the latter first if its binlog metadata object
# ('<storage object>.json') has an encryption record.
#
# Usage:
#   --let $storage_backend = file
#   --let $local_file = $MYSQL_TMP_DIR/first
#   --let $storage_object = $MYSQL_TMP_DIR/second
#   --source diff_with_decrypted_storage_object.inc
#
# or
#   --let $aws_cli = AWS_ACCESS_KEY_ID=... AWS_SECRET_ACCESS_KEY=... aws
#   --let $storage_backend = s3
#   --let $aws_s3_bucket = my-bucket
#   --let $local_file = $MYSQL_TMP_DIR/first
#   --let $storage_object = /vault/second
#   --source diff_with_decrypted_storage_object.inc
#
# $storage_backend - determines storage backend type (either 'file' or 's3')
# $aws_cli         - path to AWS command line interface (cli) tools with AWS_ACCESS_KEY_ID /
#                    AWS_SECRET_ACCESS_KEY environment variables set appropriately
#                    (needed only if $storage_backend is 's3')
# $aws_s3_bucket   - AWS S3 bucket name (needed only if $storage_backend is 's3')
# $local_file      - a path to the file on a local filesystem
# $storage_object  - if $storage_backend is 'file', a path to the binlog data
#                    file on a local filesystem; if $storage_backend is 's3', a
#                    path to the binlog data object on AWS S3
#
# Encrypted binlog data objects can be decrypted only if the keyring content
# is available in the '@keyring_keys_array' user variable (set by
# 'generate_keyring_data_file.inc').
#

--let $diff_with_decrypted_storage_object_saved_local_file = $local_file
--let $diff_with_decrypted_storage_object_saved_storage_object = $storage_object

--let $diff_with_decrypted_storage_object_metadata_file = $MYSQL_TMP_DIR/diff_with_decrypted_storage_object.json
--let $local_file = $diff_with_decrypted_storage_object_metadata_file
--let $storage_object = $diff_with_decrypted_storage_object_saved_storage_object.json
--source download_storage_object.inc
--let $read_from_file = $diff_with_decrypted_storage_object_metadata_file
--source include/read_file_to_var.inc
--remove_file $diff_with_decrypted_storage_object_metadata_file

--disable_query_log
eval SET @diff_with_decrypted_storage_object_metadata = CAST('$result' AS JSON);
--enable_query_log
--let $diff_with_decrypted_storage_object_kek_id = `SELECT IFNULL(JSON_UNQUOTE(JSON_EXTRACT(@diff_with_decrypted_storage_object_metadata, '$.encryption.file_key_envelope.kek_id')), '')`

--let $local_file = $diff_with_decrypted_storage_object_saved_local_file
--let $storage_object = $diff_with_decrypted_storage_object_saved_storage_object

if ($diff_with_decrypted_storage_object_kek_id == "")
{
  --source diff_with_storage_object.inc
}
if ($diff_with_decrypted_storage_object_kek_id != "")
{
  --disable_query_log
  --let $diff_with_decrypted_storage_object_kek_cipher = `SELECT cipher FROM JSON_TABLE(@keyring_keys_array, '$[*]' COLUMNS(id VARCHAR(32) PATH '$.id', cipher VARCHAR(32) PATH '$.cipher')) AS tt WHERE id = '$diff_with_decrypted_storage_object_kek_id'`
  --let $diff_with_decrypted_storage_object_kek_data_hex = `SELECT data_hex FROM JSON_TABLE(@keyring_keys_array, '$[*]' COLUMNS(id VARCHAR(32) PATH '$.id', data_hex VARCHAR(128) PATH '$.data_hex')) AS tt WHERE id = '$diff_with_decrypted_storage_object_kek_id'`
  --let $diff_with_decrypted_storage_object_file_key_encrypted_hex = `SELECT JSON_UNQUOTE(JSON_EXTRACT(@diff_with_decrypted_storage_object_metadata, '$.encryption.file_key_envelope.data_hex'))`
  --let $diff_with_decrypted_storage_object_file_key_iv_hex = `SELECT IFNULL(JSON_UNQUOTE(JSON_EXTRACT(@diff_with_decrypted_storage_object_metadata, '$.encryption.file_key_envelope.iv_hex')), '')`
  --let $diff_with_decrypted_storage_object_data_cipher = `SELECT JSON_UNQUOTE(JSON_EXTRACT(@diff_with_decrypted_storage_object_metadata, '$.encryption.file_data_envelope.cipher'))`
  --let $diff_with_decrypted_storage_object_data_iv_hex = `SELECT JSON_UNQUOTE(JSON_EXTRACT(@diff_with_decrypted_storage_object_metadata, '$.encryption.file_data_envelope.iv_hex'))`
  --enable_query_log

  # IV used for encrypting the file key can be empty for ECB ciphers
  --let $diff_with_decrypted_storage_object_file_key_iv_param =
  if ($diff_with_decrypted_storage_object_file_key_iv_hex != "")
  {
    --let $diff_with_decrypted_storage_object_file_key_iv_param = -iv $diff_with_decrypted_storage_object_file_key_iv_hex
  }

  # decrypting the file key with OpenSSL command line utility
  --let $diff_with_decrypted_storage_object_file_key_file = $MYSQL_TMP_DIR/diff_with_decrypted_storage_object.key
  exec echo $diff_with_decrypted_storage_object_file_key_encrypted_hex |
       xxd -r -p |
       openssl enc -$diff_with_decrypted_storage_object_kek_cipher -d -K $diff_with_decrypted_storage_object_kek_data_hex $diff_with_decrypted_storage_object_file_key_iv_param -nopad |
       xxd -p -c 255 -u >
       $diff_with_decrypted_storage_object_file_key_file
  ;
  # keeping the output the same for encrypted and unencrypted binlog files
  --let $read_from_file = $diff_with_decrypted_storage_object_file_key_file
  --let $include_silent = 1
  --source include/read_file_to_var.inc
  --let $include_silent =
  --let $diff_with_decrypted_storage_object_file_key_hex = $result
  --remove_file $diff_with_decrypted_storage_object_file_key_file

  # decrypting the binlog data with the file key
  --let $diff_with_decrypted_storage_object_encrypted_file = $MYSQL_TMP_DIR/diff_with_decrypted_storage_object.encrypted
  --let $local_file = $diff_with_decrypted_storage_object_encrypted_file
  --source download_storage_object.inc
  --let $diff_with_decrypted_storage_object_decrypted_file = $MYSQL_TMP_DIR/diff_with_decrypted_storage_object.decrypted
  exec openssl enc -$diff_with_decrypted_storage_object_data_cipher -d -K $diff_with_decrypted_storage_object_file_key_hex -iv $diff_with_decrypted_storage_object_data_iv_hex -in $diff_with_decrypted_storage_object_encrypted_file -out $diff_with_decrypted_storage_object_decrypted_file;
  --remove_file $diff_with_decrypted_storage_object_encrypted_file

  --diff_files $diff_with_decrypted_storage_object_saved_local_file $diff_with_decrypted_storage_object_decrypted_file
  --remove_file $diff_with_decrypted_storage_object_decrypted_file

  --let $local_file = $diff_with_decrypted_storage_object_saved_local_file
}

SET @diff_with_decrypted_storage_object_metadata = NULL;
