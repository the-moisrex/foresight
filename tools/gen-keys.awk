#!/usr/bin/awk -f

# Usage:
#  ./tools/gen-keys.awk /usr/include/linux/input-event-codes.h > evdev/inputs-event-codes.ixx

BEGIN {
    # Header content
    print "module;"
    print "#include <string_view>"
    print "export module foresight.evdev.event_codes;"
    print
    print "namespace foresight {"
    print "\tstruct keynames_type {";
    print "\t\tstd::string_view name;";
    print "\t\tunsigned int value;";
    print "\t} keynames[] = {";
}

# Pattern for matching key defines
/^#define\s+(KEY_[^\s]+)\s+(0x[0-9\w]+|[0-9]+)/ {
    printf "\t\t{{\"%s\"}, %s},\n", $2, $3;
    next;
}

/^#define\s+(BTN_[^\s]+)\s+(0x[0-9\w]+|[0-9]+)/ {
    printf "\t\t{{\"%s\"}, %s},\n", $2, $3;
}

END {
    # Footer content
    print "\t\t{ nullptr, 0},";
    print "\t};";
    print "} // namespace foresight"
}