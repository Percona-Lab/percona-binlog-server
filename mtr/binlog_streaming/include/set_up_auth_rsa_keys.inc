# ==== Purpose ====
#
# Sets paths to the sample RSA key pair used by the caching_sha2_password
# full authentication with the Binlog Server replication source.
#
# ==== Usage ====
#
# --source ../include/set_up_auth_rsa_keys.inc
# --source ../include/set_up_binsrv_environment.inc
#
# Sets (as an output for the caller):
#   $binsrv_auth_rsa_private_key - path to the RSA private key PEM file
#     (picked up by generate_binsrv_config.inc and written into the
#     <replication_source.auth_rsa_private_key> configuration parameter)
#   $binsrv_auth_rsa_public_key - path to the corresponding RSA public key
#     PEM file (to be passed to mysql / mysqlbinlog clients via the
#     --server-public-key-path option)

--let $binsrv_auth_rsa_key_dir = $MYSQL_TEST_DIR/suite/binlog_streaming/std_data
--let $binsrv_auth_rsa_private_key = $binsrv_auth_rsa_key_dir/caching_sha2_rsa_priv.pem
--let $binsrv_auth_rsa_public_key = $binsrv_auth_rsa_key_dir/caching_sha2_rsa_pub.pem
