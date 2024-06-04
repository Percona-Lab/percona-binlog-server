# The following environment variables must be defined to use AWS S3 as a
# storage backend:
# - $MTR_BINSRV_AWS_ACCESS_KEY_ID
# - $MTR_BINSRV_AWS_SECRET_ACCESS_KEY
# - $MTR_BINSRV_AWS_S3_BUCKET
# - $MTR_BINSRV_AWS_S3_REGION (optional)

--let $storage_backend = file
if ($MTR_BINSRV_AWS_ACCESS_KEY_ID != '')
{
  if ($MTR_BINSRV_AWS_SECRET_ACCESS_KEY != '')
  {
    if ($MTR_BINSRV_AWS_S3_BUCKET != '')
    {
      --let $storage_backend = s3
      --let $aws_cli = AWS_ACCESS_KEY_ID=$MTR_BINSRV_AWS_ACCESS_KEY_ID AWS_SECRET_ACCESS_KEY=$MTR_BINSRV_AWS_SECRET_ACCESS_KEY aws
      if ($MTR_BINSRV_AWS_S3_REGION != '')
      {
        --let $aws_cli = $aws_cli --region $MTR_BINSRV_AWS_S3_REGION
      }
    }
  }
}
