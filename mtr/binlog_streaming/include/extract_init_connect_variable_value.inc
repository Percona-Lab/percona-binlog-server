#
# Extracts the value of the variable set via --init-connect command line
# option.
#
# Usage:
#   Put
#     init-connect = SET @foo = bar
#   into the .opt or .cnf file
#   ...
#   --let $extracted_init_connect_variable_name = foo
#   --source extract_init_connect_variable_value.inc
#   --echo $extracted_init_connect_variable_value
#
# $extracted_init_connect_variable_name  - the name of the variable to
#                                          extract the value from
# $extracted_init_connect_variable_value - the MTR variable which will hold
#                                          the result value

# Because, "--init-connect" statemenmt is executed only for non-privileged
# users, we need to create one here and read the value of the variable from a new
# connection established with this user's credentials.
--disable_query_log
CREATE USER nonpriv@localhost;
--enable_query_log

--source include/count_sessions.inc

--connect(con0,localhost,nonpriv)
--let $extracted_init_connect_variable_value = `SELECT @$extracted_init_connect_variable_name`

--disconnect con0
--connection default

--source include/wait_until_count_sessions.inc

--disable_query_log
DROP USER nonpriv@localhost;
--enable_query_log
