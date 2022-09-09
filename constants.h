// Created by moisrex on 9/9/22.

#ifndef SMART_KEYBOARD_CONSTANTS_H
#define SMART_KEYBOARD_CONSTANTS_H

#define KEY_RELEASE 0
#define KEY_PRESS 1
#define KEY_KEEPING_PRESSED 2

#define null_key '\0'
static constexpr char visual_keys[]{
        null_key, // 0
        null_key, // 1
        '1',      // 2
        '2',      // 3
        '3',      // 4
        '4',      // 5
        '5',      // 6
        '6',      // 7
        '7',      // 8
        '8',      // 9
        '9',      // 10
        '0',      // 11
        '-',      // 12
        '=',      // 13
        null_key, // 14
        null_key, // 15
        'q',      // 16
        'w',      // 17
        'e',      // 18
        'r',      // 19
        't',      // 20
        'y',      // 21
        'u',      // 22
        'i',      // 23
        'o',      // 24
        'p',      // 25
        null_key, // 26
        null_key, // 27
        null_key, // 28
        null_key, // 29
        'a',      // 30
        's',      // 31
        'd',      // 32
        'f',      // 33
        'g',      // 34
        'h',      // 35
        'j',      // 36
        'k',      // 37
        'l',      // 38
        ';',      // 39
        '\'',     // 40
        null_key, // 41
        null_key, // 42
        '\\',     // 43
        'z',      // 44
        'x',      // 45
        'c',      // 46
        'v',      // 47
        'b',      // 48
        'n',      // 49
        'm',      // 50
        ',',      // 51
        '.',      // 52
        '/'       // 53
};

#endif //SMART_KEYBOARD_CONSTANTS_H
