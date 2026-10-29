# ==== Purpose ====
#
# Sets paths to the TLS certificate, key, and CA certificate used by the
# Binlog Server replication source TLS tests.
#
# ==== Usage ====
#
# --source ../include/set_up_rs_certs_and_keys.inc
# --source ../include/set_up_binsrv_environment.inc
#
# Sets (as an output for the caller):
#   $binsrv_source_ssl_cert - path to the replication source certificate PEM
#     file (picked up by generate_binsrv_config.inc)
#   $binsrv_source_ssl_private_key - path to the replication source private key PEM
#     file (picked up by generate_binsrv_config.inc)
#   $binsrv_source_ssl_ca_cert - path to the CA certificate PEM file, for
#     clients to verify the replication source certificate

--let $binsrv_source_ssl_cert_dir = $MYSQL_TEST_DIR/suite/binlog_streaming/std_data
--let $binsrv_source_ssl_cert = $binsrv_source_ssl_cert_dir/rs_cert.pem
--let $binsrv_source_ssl_private_key = $binsrv_source_ssl_cert_dir/rs_priv.pem
--let $binsrv_source_ssl_ca_cert = $binsrv_source_ssl_cert_dir/rs_ca_cert.pem
