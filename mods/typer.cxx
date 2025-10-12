// Created by moisrex on 10/11/25.

module foresight.mods.typer;

using foresight::user_event;
using foresight::typer_iterator;

namespace {
    user_event parse_modifier_keys(typer_iterator& pos, typer_iterator endp) noexcept{

    }
}

user_event foresight::parse_next_event (typer_iterator& pos, typer_iterator const endp) noexcept {
        auto const cch = *pos++;
        switch (cch) {
            case '<':
                return parse_modifier_keys(pos, endp);
            case '>':
                return invalid_user_event;
        }
}

