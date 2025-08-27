# make sure that $BINSRV environment variable is set to the absolute path
# of the Binlog Server utility before running this test
if (!$BINSRV) {
  --skip \$BINSRV environment variable must be set
}
