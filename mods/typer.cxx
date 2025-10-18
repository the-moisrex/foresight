// Created by moisrex on 10/11/25.

module foresight.mods.typer;

using foresight::typer_iterator;
using foresight::user_event;

namespace {
    user_event parse_modifier_keys(typer_iterator& pos, typer_iterator endp) noexcept {}
} // namespace

// user_event foresight::parse_next_event(typer_iterator& pos, typer_iterator const endp) noexcept {
//     auto const cch = *pos++;
//     switch (cch) {
//         case '<': return parse_modifier_keys(pos, endp);
//         case '>': return invalid_user_event;
//     }
// }
