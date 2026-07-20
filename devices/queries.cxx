
module;
#include <string>
module fs8.devices.queries;

using fs8::device_query_snapshot;
using fs8::classify::Classification;

std::string fs8::to_string(device_query_snapshot const& query) {
    std::string str;
    if (!query.name.empty()) {
        str += query.name;
    } else {
        str += "no-name";
    }
    str += ' ';
    str += to_string(query.classification);
    if (query.fail_on_no_match) {
        str += " [Fail on no match]";
    }
    if (query.grab) {
        str += " [grab]";
    }
    if (query.matches_limit) {
        str += " [";
        str += std::to_string(query.matches_limit);
        str += " match limit]";
    }
    return str;
}
