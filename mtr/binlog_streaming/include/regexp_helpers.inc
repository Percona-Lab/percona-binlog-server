# Regular expressions that can be used in both Perl (for instance, in
# 'include/wait_for_pattern_in_file.inc') and POSIX ERE (for instance, in
# 'grep -E') patterns.

# UUID in its canonical textual form (8-4-4-4-12 hexadecimal digits)
--let $uuid_regexp = [[:xdigit:]]{8}(-[[:xdigit:]]{4}){3}-[[:xdigit:]]{12}
