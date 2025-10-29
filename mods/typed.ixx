// Created by moisrex on 10/28/25.

module;
#include <cstdint>
#include <string_view>
export module foresight.mod.typed;
import foresight.mods.context;

namespace foresight {

    /**
     * This class calculates and stores the state of the keys being typed by the user in a style of a hash.
     */
    export constexpr struct [[nodiscard]] basic_typed {
      private:
        std::uint64_t    hash = 0;
        std::string_view trigger;

      public:
        explicit constexpr basic_typed(std::string_view const inp_trigger) : trigger{inp_trigger} {}

        consteval basic_typed() noexcept                               = default;
        constexpr basic_typed(basic_typed const& other)                = default;
        constexpr basic_typed(basic_typed&& other) noexcept            = default;
        constexpr basic_typed& operator=(basic_typed const& other)     = default;
        constexpr basic_typed& operator=(basic_typed&& other) noexcept = default;
        constexpr ~basic_typed()                                       = default;

        /// Return a new typed class that trigger when "str" is typed by the user.
        consteval basic_typed operator()(std::string_view const inp_trigger) const noexcept {
            return basic_typed{inp_trigger};
        }

        /// Ingest the specified event and calculate the hash
        [[nodiscard]] bool operator()(event_type const& event) noexcept;
    } typed;

} // namespace foresight
