import foresight.mods;

int main() {
    using namespace foresight;
    (context | mods::inp | mods::ignore_big_jumps | mods::out)();
    return 0;
}
