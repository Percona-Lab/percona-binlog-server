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

# Remove the per-instance local settings directory created in
# set_up_binsrv_environment.inc (together with the instance_id file inside it).
# No --echo here on purpose, to keep recorded results stable.
--force-rmdir $binsrv_local_settings_path
