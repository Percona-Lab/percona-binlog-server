# ==== Purpose ====
#
# Executes the 'mysql' command line client against the Binlog Server Utility
# replication source listener once for every authentication combination
# from the 'auth_combinations' table matching the specified condition
# (in the order of their 'id') and checks that the client's exit code
# corresponds to the 'expected_result' of the combination.
#
# ==== Usage ====
#
# --let $combinations_condition = SQL condition
# --source ../include/rs_authentication_run_combinations.inc
#
# Parameters:
#   $combinations_condition
#     SQL condition (WHERE clause) selecting combinations to execute.
#   $mysql_client_base
#     Common part of the 'mysql' client command line (host, port).
#   $probe_query
#     A query to execute after successful authentication.
#   $binsrv_auth_user
#     Valid user name.
#   $binsrv_auth_password
#     Valid password.
#   $binsrv_auth_rsa_public_key
#     Path to the server's RSA public key file.
#   $combinations_restart_required
#     Whether the PBS should be restarted for each combinations or not.
#

if (!$combinations_condition)
{
  --die ERROR IN TEST: 'combinations_condition' parameter not specified
}

--let $combination_count = `SELECT COUNT(*) FROM auth_combinations WHERE $combinations_condition`
--let $combination_index = 0
while ($combination_index < $combination_count)
{
  if ($combinations_restart_required)
  {
    --source ../include/start_proc_in_background.inc
    --source ../include/wait_for_listening_port.inc
  }
  --let $combination_id = `SELECT id FROM auth_combinations WHERE $combinations_condition ORDER BY id LIMIT 1 OFFSET $combination_index`

  --disable_query_log
  eval SELECT
      CONCAT(
        '#', id, ' ', state,
        ': user=', user,
        ', password=', password,
        ', key_option=', key_option,
        ', plugin=', plugin,
        ' -> ', IF(expected_result, 'success', 'failure')
      ) INTO @combination_description
    FROM auth_combinations
    WHERE id = $combination_id;
  --enable_query_log
  --let $combination_description = `SELECT @combination_description`
  --echo *** $combination_description

  --disable_query_log
  eval SELECT
      CONCAT_WS(' ',
        CASE user WHEN 'valid' THEN '--user=$binsrv_auth_user' ELSE '--user=wrong_$binsrv_auth_user' END,
        CASE password WHEN 'valid' THEN '--password=$binsrv_auth_password' WHEN 'invalid' THEN '--password=wrong_$binsrv_auth_password' ELSE '--password=' END,
        CASE key_option WHEN 'path' THEN '--server-public-key-path=$binsrv_auth_rsa_public_key' WHEN 'get' THEN '--get-server-public-key' ELSE '' END,
        CASE plugin WHEN 'matching' THEN '--default-auth=caching_sha2_password' ELSE '--default-auth=mysql_native_password' END
      ) INTO @combination_options
    FROM auth_combinations
    WHERE id = $combination_id;
  --enable_query_log
  --let $combination_options = `SELECT @combination_options`
  --let $combination_expected_result = `SELECT expected_result FROM auth_combinations WHERE id = $combination_id`

  if ($combination_expected_result)
  {
    --exec $mysql_client_base $combination_options -e "$probe_query" >/dev/null 2>&1
  }
  if (!$combination_expected_result)
  {
    --error 1
    --exec $mysql_client_base $combination_options -e "$probe_query" >/dev/null 2>&1
  }

  if ($combinations_restart_required)
  {
    --source ../include/terminate_proc.inc
    --source ../include/kill_binlog_dump_connection.inc
  }
  --inc $combination_index
}
