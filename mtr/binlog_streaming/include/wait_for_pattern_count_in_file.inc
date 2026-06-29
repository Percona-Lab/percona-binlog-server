# Wait until $grep_pattern appears at least $grep_match_count times in
# $grep_file.
#
# Usage:
#   --let $grep_pattern = <fixed string to search for>
#   --let $grep_file = FILENAME
#   --let $grep_match_count = N
#   [--let $wait_timeout = 120]
#   [--let $skip_log_pattern = 1]
#   --source ../include/wait_for_pattern_count_in_file.inc

if ($grep_match_count == "")
{
  --die grep_match_count must be set for wait_for_pattern_count_in_file.inc
}
if ($wait_timeout == "")
{
  --let $wait_timeout = 120
}

if ($skip_log_pattern != 1)
{
  --echo include/wait_for_pattern_count_in_file.inc [$grep_pattern] count=$grep_match_count
}

--let EXPORTED_WFPCIF_PATTERN = $grep_pattern
--let EXPORTED_WFPCIF_FILE = $grep_file
--let EXPORTED_WFPCIF_COUNT = $grep_match_count
--let EXPORTED_WFPCIF_TIMEOUT = $wait_timeout
--perl
  use strict;
  use warnings;
  my $pattern = $ENV{'EXPORTED_WFPCIF_PATTERN'}
      or die 'grep_pattern is not set';
  my $file = $ENV{'EXPORTED_WFPCIF_FILE'}
      or die 'grep_file is not set';
  my $required = $ENV{'EXPORTED_WFPCIF_COUNT'} + 0;
  my $timeout = ($ENV{'EXPORTED_WFPCIF_TIMEOUT'} // '') + 0;
  $timeout = 120 if $timeout <= 0;
  for (my $attempt = 0; $attempt < $timeout; $attempt++) {
    my $count = 0;
    if (-f $file) {
      open my $fh, '<', $file or die "cannot open $file: $!";
      while (my $line = <$fh>) {
        $count++ if index($line, $pattern) >= 0;
      }
      close $fh;
    }
    exit 0 if $count >= $required;
    sleep 1 if $attempt + 1 < $timeout;
  }
  die "Timed out waiting for $required occurrence(s) of '$pattern' in $file";
EOF
