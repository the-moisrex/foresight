import foresight.mods;

int main() {
    using namespace foresight;
    (context | mods::inp | keys_status | mice_quantifier | mods::ignore_big_jumps | mods::out)();
    return 0;
}
