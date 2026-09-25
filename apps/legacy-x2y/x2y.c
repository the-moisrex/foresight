// Created by moisrex on 11/2/24.

#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    setbuf(stdin, NULL);  // disable stdin buffer
    setbuf(stdout, NULL); // disable stdout buffer

    struct input_event event;

    while (fread(&event, sizeof(event), 1, stdin) == 1) {
        if (event.type != EV_KEY) {
            continue;
        }

        // modify the input however you like
        // here, we change "x" to "y"
        if (event.code == KEY_X) {
            event.code = KEY_Y;
        }

        // write it to stdout
        fwrite(&event, sizeof(event), 1, stdout);

        // You can log it as you wish:
        // fwrite(&event, sizeof(event), 1, stderr);
    }
}
