// Created by moisrex on 10/28/25.

module;
#include <cassert>
#include <climits>
#include <cstdint>
#include <functional>
#include <string_view>
export module fs8.mods.typed;
import fs8.context;
import fs8.lib.xkb;
import fs8.lib.mod_parser;
import fs8.log;
import fs8.pimpl;
import fs8.traits;

namespace fs8 {


    /**
     * @brief A struct that wraps an unsigned 32-bit integer representing a state using bit-fields.
     *
     * The state combines an index (24 bits) and a generation counter (8 bits).
     * This allows checking if a state is still valid by comparing generations, useful for
     * detecting invalidations in structures like vectors where elements may be moved or removed.
     */
    export struct [[nodiscard]] aho_state {
        /// Number of bits for the index.
        static constexpr std::uint32_t INDEX_BITS       = 24U;
        /// Number of bits for the generation counter.
        static constexpr std::uint32_t GENERATION_BITS  = 8U;
        /// Mask for extracting the index.
        static constexpr std::uint32_t INDEX_MASK       = (1U << INDEX_BITS) - 1U;
        /// Mask for extracting the generation.
        static constexpr std::uint32_t GENERATION_MASK  = (1U << GENERATION_BITS) - 1U;
        /// Shift amount for the generation bits.
        static constexpr std::uint32_t GENERATION_SHIFT = INDEX_BITS;

      private:
        std::uint32_t value : 24U; // The index bit-field (24 bits).
        std::uint8_t  gen   : 8U;  // The generation bit-field (8 bits).
      public:
        aho_state() noexcept                       = default;
        aho_state(aho_state&&) noexcept            = default;
        aho_state(aho_state const&)                = default;
        aho_state& operator=(aho_state const&)     = default;
        aho_state& operator=(aho_state&&) noexcept = default;
        ~aho_state() noexcept                      = default;

        /**
         * @brief Constructor from a raw 32-bit unsigned integer value.
         * @param raw_value The raw value to wrap.
         */
        explicit constexpr aho_state(std::uint32_t const raw_value) noexcept
          : value{raw_value & INDEX_MASK},
            gen{static_cast<std::uint8_t>((raw_value >> GENERATION_SHIFT) & GENERATION_MASK)} {}

        /**
         * @brief Constructor from separate index and generation values.
         * @param idx The index part (truncated to lower 24 bits).
         * @param inp_gen The generation part (truncated to lower 8 bits).
         */
        constexpr aho_state(std::uint32_t const idx, std::uint8_t const inp_gen) noexcept : value{idx & INDEX_MASK}, gen{inp_gen} {
            assert(idx <= INDEX_MASK);
        }

        [[nodiscard]] constexpr std::uint32_t index() const noexcept {
            return value;
        }

        [[nodiscard]] constexpr std::uint8_t generation() const noexcept {
            return gen;
        }

        /// Get the next generation with the newly specified value
        [[nodiscard]] constexpr aho_state next_generation(std::uint32_t const raw_value) const noexcept {
            return aho_state{raw_value, static_cast<std::uint8_t>(gen + 1U)};
        }

        /// Increment the generation and set the new value
        [[nodiscard]] constexpr aho_state& operator=(std::uint32_t const new_value) noexcept {
            value = new_value & INDEX_MASK;
            ++gen;
            return *this;
        }

        /**
         * @brief Checks if this state is valid compared to another (same generation).
         * @param current_state The current state to compare against.
         * @returns True if generations match, false otherwise.
         */
        [[nodiscard]] constexpr bool is_valid(aho_state const& current_state) const noexcept {
            return gen == current_state.generation();
        }

        // NOLINTNEXTLINE(*explicit*)
        [[nodiscard]] explicit(false) constexpr operator std::uint32_t() const noexcept {
            return value;
        }

        [[nodiscard]] constexpr bool operator==(aho_state const& other) const noexcept {
            return value == other.value && gen == other.gen;
        }

        [[nodiscard]] constexpr bool operator!=(aho_state const& other) const noexcept {
            return !(*this == other);
        }
    };

    /**
     * Aho-Corasick status
     */
    export struct [[nodiscard]] basic_search_engine : pimpl_idiom<basic_search_engine> {
        using pimpl_idiom::pimpl_idiom;

        using state_type = std::uint32_t;

        using output_link_type                    = std::uint32_t;
        static constexpr std::size_t MAX_PATTERNS = sizeof(output_link_type) * CHAR_BIT;

      private:
        /// Returns the number of states that the built machine has.
        /// States are numbered 0 up to the return value - 1, inclusive.
        std::uint32_t build_machine();

        // helpers
        [[nodiscard]] state_type    find_child(state_type state, char32_t code) const noexcept;
        [[nodiscard]] state_type    quick_find_child(state_type state, char32_t code) const noexcept;
        [[nodiscard]] std::uint32_t add_child(state_type state, char32_t code, state_type child_index);

      public:
        /**
         * Add a new pattern to search for
         * @param pattern It's a UTF-8-encoded string that we will try to find later on
         */
        [[nodiscard("Don't lose your trigger id")]] std::uint16_t emplace_pattern(std::string_view pattern);

        /**
         * Process this new event, and return a new state
         */
        aho_state process(char32_t code_point, aho_state last_state) const noexcept;

        void               matches(std::uint32_t state, std::function_ref<void(std::u32string_view)> callback) const;
        [[nodiscard]] bool matches(std::uint32_t state, std::uint16_t trigger_id) const noexcept;

        /// Initialize empty
        context_action operator()(start_tag) noexcept;

        /// Handling events
        void operator()() const noexcept {
            // do nothing
        }

        /// Process and match
        [[nodiscard]] bool
        search(event_type const& event, std::uint16_t trigger_id, xkb::basic_state const& keyboard_state, aho_state& state) const noexcept;
    };

    export constexpr basic_search_engine search_engine;

    /**
     * This class calculates and stores the state of the keys being typed by the user in style of a hash.
     */
    export constexpr struct [[nodiscard]] basic_typed : pimpl_idiom<basic_typed> {
        using pimpl_idiom::pimpl_idiom;

        static constexpr std::uint16_t invalid_trigger_id = std::numeric_limits<std::uint16_t>::max();

      private:
        std::string_view pattern;        // pattern string
        xkb::basic_state keyboard_state; // the state of the modifier keys and what not

        /// Register the pattern into the search engine
        context_action on_start(basic_search_engine& engine) noexcept;

        /// Process and search
        [[nodiscard]] bool on_search(event_type const& event, basic_search_engine const& engine) noexcept;

      public:
        explicit consteval basic_typed(std::string_view const inp_pattern) noexcept : pattern{inp_pattern} {}

        /// Return a new typed class that trigger when "str" is typed by the user.
        consteval basic_typed operator[](std::string_view const inp_trigger) const noexcept {
            return basic_typed{inp_trigger};
        }

        /// Register the pattern into the search engine
        context_action operator()(Context auto& ctx, start_tag) noexcept {
            keyboard_state.initialize(xkb::get_default_keymap());
            return on_start(ctx.mod(search_engine));
        }

        template <Context CtxT>
        [[nodiscard]] bool operator()(CtxT& ctx) noexcept {
            static_assert(has_mod<basic_search_engine, CtxT>, "You need to have 'search_engine' in your pipeline.");
            return on_search(ctx.event(), ctx.mod(search_engine));
        }
    } typed;

} // namespace fs8
