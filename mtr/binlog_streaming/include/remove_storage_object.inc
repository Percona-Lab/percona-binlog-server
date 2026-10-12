#
# Removes an object from the backend storage.
#
# Usage:
#   --let $storage_backend = file
#   --let $storage_object = $MYSQL_TMP_DIR/first
#   --source remove_storage_object.inc
#
# or
#   --let $aws_cli = AWS_ACCESS_KEY_ID=... AWS_SECRET_ACCESS_KEY=... aws
#   --let $storage_backend = s3
#   --let $aws_s3_bucket = my-bucket
#   --let $storage_object = /vault/first
#   --source remove_storage_object.inc
#
# $storage_backend - determines storage backend type (either 'file' or 's3')
# $aws_cli         - path to AWS command line interface (cli) tools with AWS_ACCESS_KEY_ID /
#                    AWS_SECRET_ACCESS_KEY environment variables set appropriately
#                    (needed only if $storage_backend is 's3')
# $aws_s3_bucket   - AWS S3 bucket name (needed only if $storage_backend is 's3')
# $storage_object  - if $storage_backend is 'file', a path to the file on a
#                    local filesystem; if $storage_backend is 's3', a path to
#                    the object on AWS S3
#

if ($storage_backend == file)
{
  --remove_file $storage_object
}
if ($storage_backend == s3)
{
  --exec $aws_cli s3 rm s3://$aws_s3_bucket$storage_object > /dev/null
}
