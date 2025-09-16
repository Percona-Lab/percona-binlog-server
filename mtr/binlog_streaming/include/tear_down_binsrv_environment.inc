--echo
--echo *** Removing the Binlog Server utility storage directory.
if ($storage_backend == file)
{
  --force-rmdir $binsrv_storage_path
}
if ($storage_backend == s3)
{
  --exec $aws_cli s3 rm s3://$aws_s3_bucket$binsrv_storage_path/ --recursive > /dev/null
  --force-rmdir $binsrv_buffer_path
}

--echo
--echo *** Removing the Binlog Server utility log file.
--remove_file $binsrv_log_path

--echo
--echo *** Removing the Binlog Server utility configuration file.
--remove_file $binsrv_config_file_path
