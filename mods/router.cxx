// Created by moisrex on 8/12/26.

module;
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
module fs8.mods;
import fs8.nullable_indirect;
import fs8.devices.queries;
import fs8.devices.capabilities;

namespace fs8::detail {

    // Mirrors `basic_router::hash`: the type occupies the high bits and the
    // code sits in the low `shift` bits (KEY_MAX = 767 needs 10 bits).
    [[nodiscard]] constexpr std::uint16_t router_hash(std::uint16_t const type, std::uint16_t const code) noexcept {
        static constexpr std::uint16_t shift = std::bit_width<std::uint16_t>(KEY_MAX) - 1U;
        return static_cast<std::uint16_t>((type << shift) | code);
    }

    struct router_state {
        // One entry per (type, code) pair; max valid hash is router_hash(EV_MAX, KEY_MAX).
        static constexpr std::size_t table_size = static_cast<std::size_t>(router_hash(EV_MAX, KEY_MAX)) + 1U;

        std::array<std::int8_t, table_size> hashes{};
        std::int8_t                         last_index = 0;
    };

    void router_set_caps(nullable_indirect<router_state>& state, device_query const* const queries_begin, std::size_t const queries_count) noexcept {
        if (state.get() == nullptr) [[unlikely]] {
            state = nullable_indirect<router_state>::make();
        }

        auto& hashes = state->hashes;
        hashes.fill(-1);

        // Declaring which hash belongs to which uinput device
        std::int8_t input_pick = 0;
        for (std::size_t i = 0; i < queries_count; ++i) {
            for (auto const [type, codes, action] : queries_begin[i].caps) {
                for (auto const code : codes) {
                    auto const index = router_hash(static_cast<std::uint16_t>(type), static_cast<std::uint16_t>(code));
                    if (action == caps_action::append /* && hashes.at(index) == -1 */) {
                        hashes[index] = input_pick;
                    }
                }
            }
            ++input_pick;
        }
    }

    std::int32_t router_lookup(router_state& state, std::uint32_t const hashed_value, bool const is_syn_event) noexcept {
        if (!is_syn_event) {
            state.last_index = state.hashes[static_cast<std::size_t>(hashed_value)];
        }
        return state.last_index;
    }

} // namespace fs8::detail