// Created by moisrex on 10/28/25.

module;
#include <cassert>
#include <cstdint>
#include <string_view>
#include <vector>
export module foresight.mod.typed;
import foresight.mods.context;

namespace foresight {


    /**
     * @brief A struct that wraps an unsigned 32-bit integer representing a state using bit-fields.
     *
     * The state combines an index (24 bits) and a generation counter (8 bits).
     * This allows checking if a state is still valid by comparing generations, useful for
     * detecting invalidations in structures like vectors where elements may be moved or removed.
     */
    struct aho_state {
        /// Number of bits for the index.
        static constexpr std::uint32_t INDEX_BITS       = 24U;
        /// Number of bits for the generation counter.
        static constexpr std::uint32_t GENERATION_BITS  = 8U;
        /// Mask for extracting the index.
        static constexpr std::uint32_t INDEX_MASK       = (1u << INDEX_BITS) - 1;
        /// Mask for extracting the generation.
        static constexpr std::uint32_t GENERATION_MASK  = (1u << GENERATION_BITS) - 1;
        /// Shift amount for the generation bits.
        static constexpr std::uint32_t GENERATION_SHIFT = INDEX_BITS;

      private:
        std::uint32_t value : 24; // The index bit-field (24 bits).
        std::uint8_t  gen   : 8;  // The generation bit-field (8 bits).
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
    struct [[nodiscard]] aho_typed_status {
      private:
        static constexpr size_t ALPHABET_SIZE = 26;

        std::vector<std::string>                    patterns;
        std::vector<int>                            output_links;
        std::vector<int>                            failure_links;
        std::vector<std::array<int, ALPHABET_SIZE>> trie;

        /// Returns the number of states that the built machine has.
        /// States are numbered 0 up to the return value - 1, inclusive.
        int build_machine();

        [[nodiscard]] int find_next_state(int current_state, char input) const noexcept;

      public:
        aho_typed_status();

        /**
         * Add a new pattern to search for
         */
        void add_pattern(std::string_view pattern);

        /**
         * Process this new event
         */
        [[nodiscard("Store the state")]] int process(char code_point, int state) noexcept;

        std::vector<std::string> matches(int state) const;
    };

    /**
     * This class calculates and stores the state of the keys being typed by the user in a style of a hash.
     */
    export constexpr struct [[nodiscard]] basic_typed {
      private:
        // if collisions happen, make these 64bit:
        std::uint32_t target_hash  = 0;
        std::uint32_t current_hash = 0;

        std::size_t target_length  = 0;
        std::size_t current_length = 0;

        std::string_view trigger;

      public:
        explicit constexpr basic_typed(std::string_view const inp_trigger) noexcept : trigger{inp_trigger} {}

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

        void operator()(start_tag);

        /// Ingest the specified event and calculate the hash
        [[nodiscard]] bool operator()(event_type const& event) noexcept;
    } typed;

} // namespace foresight
