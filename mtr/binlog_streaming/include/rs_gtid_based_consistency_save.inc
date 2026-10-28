# ==== Purpose ====
#
# Appends a GTID checkpoint to the '@checkpoints' JSON array session
# variable initialized by 'rs_gtid_based_consistency.test'. Setting a user
# variable does not write anything to the binary log, so saving a
# checkpoint does not affect the content of binary log files.
#
# ==== Usage ====
#
# --let $checkpoint_label = after inserting the first batch of rows
# --let $checkpoint_gtid_executed = uuid:1-10
# --source ../include/rs_gtid_based_consistency_save.inc
#
# If $checkpoint_gtid_executed is set to 'auto', the current GTID set is
# taken from @@global.gtid_executed. If it is empty, then it means an
# empty GTID set.
# Each checkpoint is a JSON object with
# 'test_label' and 'gtid_executed' keys. Both input variables are reset
# after saving the checkpoint.
#
# The $checkpoint_label must not contain single quotes.

--let $resolved_checkpoint_gtid_executed = $checkpoint_gtid_executed
if ($checkpoint_gtid_executed == "auto")
{
  --let $resolved_checkpoint_gtid_executed = `SELECT @@global.gtid_executed`
}

--disable_query_log
eval SET @checkpoints = JSON_ARRAY_APPEND(@checkpoints, '\$',
  JSON_OBJECT('test_label', '$checkpoint_label',
              'gtid_executed', '$resolved_checkpoint_gtid_executed'));
--enable_query_log
