#
# Compares a file on a local filesystem with an object from the backend storage.
#
# Usage:
#   --let $storage_backend = file
#   --let $local_file = $MYSQL_TMP_DIR/first
#   --let $storage_object = $MYSQL_TMP_DIR/second
#   --source diff_with_storage_object.inc
#
# or
#   --let $aws_cli = AWS_ACCESS_KEY_ID=... AWS_SECRET_ACCESS_KEY=... aws
#   --let $storage_backend = s3
#   --let $local_file = $MYSQL_TMP_DIR/first
#   --let $aws_s3_bucket = my-bucket
#   --let $storage_object = /vault/second
#   --source diff_with_storage_object.inc
#
# $storage_backend - determines stortage backend type (either 'file' or 'fs')
# $aws_cli         - path to AWS command line interface (cli) tools with AWS_ACCESS_KEY_ID /
#                    AWS_SECRET_ACCESS_KEY environment variables set appropriately
#                    (needed only if $storage_backend is 's3')
# $local_file      - a path to the first file on a local filesystem
# $aws_s3_bucket   - AWS S3 bucket name (needed only if $storage_backend is 's3')
# $storage_object  - if $storage_backend is 'file', a path to the second file on
#                    a local filesystem; if $storage_backend is 's3', a path to
#                    the second object on AWS S3
#

if ($storage_backend == file)
{
  --diff_files $local_file $storage_object
}
if ($storage_backend == s3)
{
  --let $downloaded_file_path = $MYSQL_TMP_DIR/diff_with_storage_object.downloaded
  --exec $aws_cli s3 cp s3://$aws_s3_bucket$storage_object $downloaded_file_path > /dev/null
  --diff_files $local_file $downloaded_file_path
  --remove_file $downloaded_file_path
}
