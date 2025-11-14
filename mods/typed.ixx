// Created by moisrex on 10/28/25.

module;
#include <cassert>
#include <climits>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>
export module foresight.mods.typed;
import foresight.mods.context;
import foresight.lib.xkb;
import foresight.lib.mod_parser;

namespace foresight {


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
        /**
         * @brief Default constructor, initializes index and generation to 0.
         */
        constexpr aho_state() noexcept                            = default;
        constexpr aho_state(aho_state const&)                     = default;
        constexpr aho_state(aho_state&&) noexcept                 = default;
        constexpr aho_state& operator=(aho_state&&) noexcept      = default;
        constexpr aho_state& operator=(aho_state const&) noexcept = default;
        constexpr ~aho_state() noexcept                           = default;

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
        constexpr aho_state(std::uint32_t const idx, std::uint8_t const inp_gen) noexcept
          : value{idx & INDEX_MASK},
            gen{inp_gen} {
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
    export struct [[nodiscard]] basic_search_engine {
        using state_type = std::uint32_t;

        using output_link_type                    = std::uint32_t;
        static constexpr std::size_t MAX_PATTERNS = sizeof(output_link_type) * CHAR_BIT;

      private:
        struct node_type {
            char32_t         value     = 0; // the incoming code point for this node (root = 0)
            output_link_type out_link  = 0; // output bitmask (pattern IDs)
            std::uint32_t    fail_link = 0; // failure link (state index)

            // children: pair<codepoint, state_index>. kept sorted by codepoint for binary search.
            std::vector<std::pair<char32_t, std::uint32_t>> children;

            // A mask for all children keys for faster failures
            std::uint32_t children_mask = 0U;
        };

        /// UTF-32-encoded patterns (some code points are special code points)
        /// Trigger ID is the index that points to this patterns
        /// todo: use inplace_vector<std::u32string, MAX_PATTERNS>
        std::vector<std::u32string> patterns;
        std::vector<node_type>      trie;

        /// Returns the number of states that the built machine has.
        /// States are numbered 0 up to the return value - 1, inclusive.
        std::uint32_t build_machine();

        // helpers
        [[nodiscard]] state_type    find_child(state_type state, char32_t code) const noexcept;
        [[nodiscard]] std::uint32_t add_child(state_type state, char32_t code, state_type child_index);

      public:
        consteval basic_search_engine() noexcept                                 = default;
        consteval basic_search_engine(basic_search_engine const&)                = default;
        constexpr basic_search_engine(basic_search_engine&&) noexcept            = default;
        consteval basic_search_engine& operator=(basic_search_engine const&)     = default;
        constexpr basic_search_engine& operator=(basic_search_engine&&) noexcept = default;
        constexpr ~basic_search_engine()                                         = default;

        /**
         * Add a new pattern to search for
         * @param pattern It's a UTF-8-encoded string that we will try to find later on
         */
        void add_pattern(std::string_view pattern);

        /**
         * Process this new event, and return a new state
         */
        aho_state process(char32_t code_point, aho_state last_state) const noexcept;

        // todo: convert this to std::function_ref
        void matches(std::uint32_t state, std::function<void(std::u32string_view)> const& callback) const;
        [[nodiscard]] bool matches(std::uint32_t state, std::uint16_t trigger_id) const noexcept;

        /// Initialize empty
        void operator()(start_tag);

        /// Handling events
        void operator()() const noexcept {
            // do nothing
        }
    };

    export constexpr basic_search_engine search_engine;

    /**
     * This class calculates and stores the state of the keys being typed by the user in a style of a hash.
     */
    export constexpr struct [[nodiscard]] basic_typed {
        static constexpr std::uint16_t invalid_trigger_id = std::numeric_limits<std::uint16_t>::max();

      private:
        std::string_view pattern;
        std::uint16_t    trigger_id = invalid_trigger_id;
        xkb::basic_state state;
        aho_state        current_state{};

      public:
        explicit consteval basic_typed(std::string_view const inp_pattern) noexcept : pattern{inp_pattern} {}

        consteval basic_typed() noexcept                               = default;
        consteval basic_typed(basic_typed const& other)                = default;
        constexpr basic_typed(basic_typed&& other) noexcept            = default;
        consteval basic_typed& operator=(basic_typed const& other)     = default;
        constexpr basic_typed& operator=(basic_typed&& other) noexcept = default;
        constexpr ~basic_typed()                                       = default;

        /// Return a new typed class that trigger when "str" is typed by the user.
        consteval basic_typed operator()(std::string_view const inp_trigger) const noexcept {
            return basic_typed{inp_trigger};
        }

        /// Register the pattern into the search engine
        void operator()(Context auto& ctx, start_tag) {
            state.initialize(xkb::get_default_keymap());
            ctx.mod(search_engine).add_pattern(pattern);
        }

        [[nodiscard]] bool operator()(Context auto& ctx) noexcept {
            auto const code   = unicode_encoded_event(state, ctx.event());
            auto&      engine = ctx.mod(search_engine);
            current_state     = engine.process(code, current_state);
            return engine.matches(current_state.index(), trigger_id);
        }
    } typed;

} // namespace foresight
