// Created by moisrex on 9/9/22.

#ifndef SMART_KEYBOARD_PROVIDER_H
#define SMART_KEYBOARD_PROVIDER_H

#include <map>
#include <string>

/**
 * The eventio class will request this class for what to do with the events and this class will provide a
 * response to it.
 */
class provider {

    std::map<std::string, std::string> keys;

void check() {
    to_string();
    auto const pos = std::ranges::find_if(keys, [this](auto const &cur_kry_value_pair) {
        auto const &[key, value] = cur_kry_value_pair;
        return str.ends_with(key.data());
    });
    if (pos != keys.end()) {
        replace(*pos);
    }
}

};


#endif // SMART_KEYBOARD_PROVIDER_H
