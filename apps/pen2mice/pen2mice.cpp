import foresight.mods;

int main() {
    using namespace foresight;
    (context                  // Init Context
     | input                  // Get the events from input
     | keys_status            // Save key presses
     | mice_quantifier        // Quantify the mouse movements
     | mods::ignore_big_jumps // Ignore big mouse jumps
     | mods::add_scroll       // Make middle button, a scroll wheel
     | output                 // print the events
     )();
    return 0;
}
