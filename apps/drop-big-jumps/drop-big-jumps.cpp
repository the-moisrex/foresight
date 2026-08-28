#include <cmath>
#include <linux/input.h>
#include <unistd.h>

using code_type  = decltype(input_event::code);
using ev_type    = decltype(input_event::type);
using value_type = decltype(input_event::value);


static constexpr value_type threshold = 50; // pixels to resistence to move

int main() {
    input_event event{};

    for (;;) {
        // we ignore errors
        if (read(STDIN_FILENO, &event, sizeof(event)) == 0) { // EOF
            break;
        }

        if (event.type
            == EV_REL
            && (event.code == REL_X || event.code == REL_Y)
            && std::abs(event.value)
            > threshold)
        {
            continue;
        }

        std::ignore = write(STDOUT_FILENO, &event, sizeof(event));
    }

    return 0;
}
