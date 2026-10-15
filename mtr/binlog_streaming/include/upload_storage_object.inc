#
# Copies a file from a local filesystem to the backend storage (overwriting
# the destination object if it already exists).
#
# Usage:
#   --let $storage_backend = file
#   --let $local_file = $MYSQL_TMP_DIR/first
#   --let $storage_object = $MYSQL_TMP_DIR/second
#   --source upload_storage_object.inc
#
# or
#   --let $aws_cli = AWS_ACCESS_KEY_ID=... AWS_SECRET_ACCESS_KEY=... aws
#   --let $storage_backend = s3
#   --let $local_file = $MYSQL_TMP_DIR/first
#   --let $aws_s3_bucket = my-bucket
#   --let $storage_object = /vault/second
#   --source upload_storage_object.inc
#
# $storage_backend - determines storage backend type (either 'file' or 's3')
# $aws_cli         - path to AWS command line interface (cli) tools with AWS_ACCESS_KEY_ID /
#                    AWS_SECRET_ACCESS_KEY environment variables set appropriately
#                    (needed only if $storage_backend is 's3')
# $local_file      - a path to the source file on a local filesystem
# $aws_s3_bucket   - AWS S3 bucket name (needed only if $storage_backend is 's3')
# $storage_object  - if $storage_backend is 'file', a path to the destination
#                    file on a local filesystem; if $storage_backend is 's3', a
#                    path to the destination object on AWS S3
#

if ($storage_backend == file)
{
  # '--copy_file' refuses to overwrite an existing file, hence removing the
  # destination first (ignoring the case when it does not exist yet)
  --error 0,1
  --remove_file $storage_object
  --copy_file $local_file $storage_object
}
if ($storage_backend == s3)
{
  --exec $aws_cli s3 cp $local_file s3://$aws_s3_bucket$storage_object > /dev/null
}
