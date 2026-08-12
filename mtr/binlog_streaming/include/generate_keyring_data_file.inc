#
# Creates a keyring data file in JSON format for the Binlog Server Utility
#
# Usage:
#   --let $binsrv_keyring_data_file_path = $MYSQL_TMP_DIR/keyring_data.json
#   --let $binsrv_encryption_keyring_content = { "id": "alpha", "cipher": "AES-128-ECB", "data_hex": "00112233445566778899AABBCCDDEEFF"} (optional)
#         if $binsrv_encryption_keyring_content is not set, this file will generate several predefined keys
#   --source generate_keyring_data_file.inc

--echo
--echo *** Generating keyring data file in JSON format.

if ($binsrv_encryption_keyring_content != "")
{
  SET @keyring_keys_array = CAST('$binsrv_encryption_keyring_content' AS JSON);
}
if (!$binsrv_encryption_keyring_content)
{
  --disable_query_log
  SET @keyring_keys_array = JSON_ARRAY(
    JSON_OBJECT('id', 'alpha16', 'cipher', 'AES-128-ECB', 'data_hex', '000102030405060708090A0B0C0D0E0F'),
    JSON_OBJECT('id', 'alpha24', 'cipher', 'AES-192-ECB', 'data_hex', '000102030405060708090A0B0C0D0E0F0001020304050607'),
    JSON_OBJECT('id', 'alpha32', 'cipher', 'AES-256-ECB', 'data_hex', '000102030405060708090A0B0C0D0E0F000102030405060708090A0B0C0D0E0F'),

    JSON_OBJECT('id',  'beta16', 'cipher', 'AES-128-CBC', 'data_hex', '101112131415161718191A1B1C1D1E1F'),
    JSON_OBJECT('id',  'beta24', 'cipher', 'AES-192-CBC', 'data_hex', '101112131415161718191A1B1C1D1E1F1011121314151617'),
    JSON_OBJECT('id',  'beta32', 'cipher', 'AES-256-CBC', 'data_hex', '101112131415161718191A1B1C1D1E1F101112131415161718191A1B1C1D1E1F'),

    JSON_OBJECT('id', 'gamma16', 'cipher', 'AES-128-CTR', 'data_hex', '202122232425262728292A2B2C2D2E2F'),
    JSON_OBJECT('id', 'gamma24', 'cipher', 'AES-192-CTR', 'data_hex', '202122232425262728292A2B2C2D2E2F2021222324252627'),
    JSON_OBJECT('id', 'gamma32', 'cipher', 'AES-256-CTR', 'data_hex', '202122232425262728292A2B2C2D2E2F202122232425262728292A2B2C2D2E2F'),

    JSON_OBJECT('id', 'delta16', 'cipher', 'AES-128-GCM', 'data_hex', '303132333435363738393A3B3C3D3E3F'),
    JSON_OBJECT('id', 'delta24', 'cipher', 'AES-192-GCM', 'data_hex', '303132333435363738393A3B3C3D3E3F3031323334353637'),
    JSON_OBJECT('id', 'delta32', 'cipher', 'AES-256-GCM', 'data_hex', '303132333435363738393A3B3C3D3E3F303132333435363738393A3B3C3D3E3F')
  );
  --enable_query_log
  --let $binsrv_encryption_keyring_content = `SELECT @keyring_keys_array`
}

--let $write_var = `SELECT JSON_OBJECT('version', 1, 'keys', CAST(@keyring_keys_array AS JSON))`
--let $write_to_file = $binsrv_keyring_data_file_path
--source include/write_var_to_file.inc
