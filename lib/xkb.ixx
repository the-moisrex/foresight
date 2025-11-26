// Created by moisrex on 10/12/25.

module;
#include <memory>
#include <stdexcept>
#include <xkbcommon/xkbcommon.h>
export module foresight.lib.xkb;

export namespace fs8::xkb {

    // Forward declare Keymap and State
    struct context;
    struct keymap;
    struct state;

    /// Exception type
    struct xkb_error final : std::runtime_error {
        explicit xkb_error(std::string const &msg) : std::runtime_error(msg) {}
    };

    /**
     * context: wraps xkb_context_t
     */
    struct [[nodiscard]] context final {
        explicit context(xkb_context_flags flags = XKB_CONTEXT_NO_FLAGS);

        // Non-copyable, movable
        context(context const &)                = delete;
        context &operator=(context const &)     = delete;
        context(context &&) noexcept            = default;
        context &operator=(context &&) noexcept = default;
        ~context() noexcept;

        [[nodiscard]] xkb_context *get() const noexcept;

        // Set and get log level
        void                        set_log_level(xkb_log_level level) const noexcept;
        [[nodiscard]] xkb_log_level log_level() const noexcept;

      private:
        xkb_context *ctx = nullptr;
    };

    [[nodiscard]] context &get_default_context();

    /**
     * Keymap wrapper
     */
    struct [[nodiscard]] keymap final {
        /// Create keymap from rules+model+layout+variant+options
        explicit keymap(
          context const &inp_ctx,
          char const    *rules   = nullptr,
          char const    *model   = nullptr,
          char const    *layout  = nullptr,
          char const    *variant = nullptr,
          char const    *options = nullptr);

        /// Create from raw pointer (used internally)
        explicit keymap(xkb_keymap *km);

        void load(context const        &ctx,
                  xkb_rule_names const *names,
                  xkb_keymap_format     keymap_format = XKB_KEYMAP_FORMAT_TEXT_V2);

        /// Create keymap from compiled keymap string (file contents or XKB keymap text)
        static keymap from_string(context const &ctx, std::string_view xml);

        keymap(keymap const &)                = delete;
        keymap &operator=(keymap const &)     = delete;
        keymap(keymap &&) noexcept            = default;
        keymap &operator=(keymap &&) noexcept = default;
        ~keymap() noexcept;

        [[nodiscard]] xkb_keymap   *get() const noexcept;
        [[nodiscard]] xkb_keycode_t min_keycode() const noexcept;
        [[nodiscard]] xkb_keycode_t max_keycode() const noexcept;

        /// Return a UTF-8 label for the keymap
        [[nodiscard]] std::string as_string() const;

      private:
        xkb_keymap *handle = nullptr;
    };

    [[nodiscard]] keymap     &get_default_keymap();
    [[nodiscard]] std::string name(xkb_keysym_t keysym);

    /**
     * Wraps xkb_state
     */
    struct [[nodiscard]] basic_state final {
        /// Initialize as well
        explicit basic_state(keymap const &inp_map);

        // Non-copyable, movable
        consteval basic_state() noexcept                               = default;
        consteval basic_state(basic_state const &)                     = default;
        consteval basic_state &operator=(basic_state const &) noexcept = default;
        basic_state(basic_state &&) noexcept                           = default;
        basic_state &operator=(basic_state &&) noexcept                = default;

        constexpr ~basic_state() noexcept {
            if !consteval {
                destroy();
            }
        }

        void initialize(keymap const &inp_map);

        /// Make sure you have initialized it first
        [[nodiscard]] xkb_state *get() const noexcept;

      private:
        void       destroy() noexcept;
        xkb_state *handle = nullptr;
    };

} // namespace fs8::xkb
