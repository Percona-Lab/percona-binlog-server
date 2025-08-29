# This script will set the following variables based on the value
# returned by the USER() function:
# - $binsrv_connection_user
# - $binsrv_connection_host

SET @delimiter_pos = INSTR(USER(), '@');
SET @connection_user = SUBSTRING(USER(), 1, @delimiter_pos - 1);
SET @connection_host = SUBSTRING(USER(), @delimiter_pos + 1);
SET @connection_host = IF(@connection_host = 'localhost', '127.0.0.1', @connection_host);

--let $binsrv_connection_user = `SELECT @connection_user`
--let $binsrv_connection_host = `SELECT @connection_host`
