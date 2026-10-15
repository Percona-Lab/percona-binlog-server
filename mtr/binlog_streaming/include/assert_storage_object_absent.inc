#
# Asserts that an object does not exist in the backend storage.
#
# Usage:
#   --let $storage_backend = file
#   --let $storage_object = $MYSQL_TMP_DIR/first
#   --source assert_storage_object_absent.inc
#
# or
#   --let $aws_cli = AWS_ACCESS_KEY_ID=... AWS_SECRET_ACCESS_KEY=... aws
#   --let $storage_backend = s3
#   --let $aws_s3_bucket = my-bucket
#   --let $storage_object = /vault/first
#   --source assert_storage_object_absent.inc
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
  --error 1
  --file_exists $storage_object
}
if ($storage_backend == s3)
{
  # 'aws s3 ls' cannot be used here as it matches objects by prefix;
  # 'aws s3api head-object' fails with exit code 254 (AWS CLI v2) or
  # 255 (AWS CLI v1) when the object does not exist
  --let $assert_storage_object_absent_key = `SELECT TRIM(LEADING '/' FROM '$storage_object')`
  --error 254,255
  --exec $aws_cli s3api head-object --bucket $aws_s3_bucket --key $assert_storage_object_absent_key > /dev/null 2>&1
}
