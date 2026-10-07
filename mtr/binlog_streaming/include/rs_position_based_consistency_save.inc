# ==== Purpose ====
#
# Appends a (binlog name, position) checkpoint to the '@checkpoints'
# JSON array session variable initialized by
# 'rs_position_based_consistency.test'. Setting a user variable does not
# write anything to the binary log, so saving a checkpoint does not affect
# the content of binary log files.
#
# ==== Usage ====
#
# --let $checkpoint_label = after inserting the first batch of rows
# [--let $checkpoint_binlog_name = binlog.000001]
# [--let $checkpoint_binlog_position = 4]
# --source ../include/rs_position_based_consistency_save.inc
#
# If $checkpoint_binlog_position is not specified, the current binary log
# name and position are taken from $stmt_show_binary_log_status (set by
# v80_v84_compatibility_defines.inc). If $checkpoint_binlog_position is
# specified, $checkpoint_binlog_name is used as is (an empty
# $checkpoint_binlog_name with position 4 means "from the beginning of the
# oldest binlog file").
#
# The $checkpoint_label must not contain single quotes.

if ($checkpoint_binlog_position == "")
{
  --let $checkpoint_binlog_name = query_get_value($stmt_show_binary_log_status, File, 1)
  --let $checkpoint_binlog_position = query_get_value($stmt_show_binary_log_status, Position, 1)
}

--disable_query_log
eval SET @checkpoints = JSON_ARRAY_APPEND(@checkpoints, '\$',
  JSON_OBJECT('binlog_file', '$checkpoint_binlog_name',
              'position', $checkpoint_binlog_position,
              'test_label', '$checkpoint_label'));
--enable_query_log

--let $checkpoint_binlog_name =
--let $checkpoint_binlog_position =
--let $checkpoint_label =
