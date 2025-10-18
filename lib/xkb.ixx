// Created by moisrex on 10/12/25.

module;
#include <memory>
#include <stdexcept>
#include <xkbcommon/xkbcommon.h>
export module foresight.lib.xkb;

export namespace foresight::xkb {

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
    struct [[nodiscard]] context final : std::enable_shared_from_this<context> {
        using pointer = std::shared_ptr<context>;

      private:
        struct private_tag {};

      public:
        explicit context(private_tag, xkb_context_flags flags = XKB_CONTEXT_NO_FLAGS);
        static pointer create(xkb_context_flags flags = XKB_CONTEXT_NO_FLAGS);

        // Non-copyable, movable
        context(context const &)                = delete;
        context &operator=(context const &)     = delete;
        context(context &&) noexcept            = default;
        context &operator=(context &&) noexcept = default;
        ~context() noexcept;

        [[nodiscard]] pointer      getptr();
        [[nodiscard]] xkb_context *get() const noexcept;

        // Set and get log level
        void                        set_log_level(xkb_log_level level) const noexcept;
        [[nodiscard]] xkb_log_level log_level() const noexcept;

      private:
        xkb_context *ctx;
    };

    /**
     * Keymap wrapper
     */
    struct [[nodiscard]] keymap final : std::enable_shared_from_this<keymap> {
        using pointer = std::shared_ptr<keymap>;

      private:
        struct private_tag {};

      public:
        /// Create keymap from rules+model+layout+variant+options
        explicit keymap(
          private_tag,
          context::pointer inp_ctx,
          char const      *rules   = nullptr,
          char const      *model   = nullptr,
          char const      *layout  = nullptr,
          char const      *variant = nullptr,
          char const      *options = nullptr);

        /// Create from raw pointer (used internally)
        keymap(context::pointer inp_ctx, xkb_keymap *km);

        static pointer create(
          context::pointer const &inp_ctx,
          char const             *rules   = nullptr,
          char const             *model   = nullptr,
          char const             *layout  = nullptr,
          char const             *variant = nullptr,
          char const             *options = nullptr);

        static pointer create(
          char const *rules   = nullptr,
          char const *model   = nullptr,
          char const *layout  = nullptr,
          char const *variant = nullptr,
          char const *options = nullptr);

        void load(xkb_rule_names const *names, xkb_keymap_format keymap_format = XKB_KEYMAP_FORMAT_TEXT_V2);

        /// Create keymap from compiled keymap string (file contents or XKB keymap text)
        static pointer from_string(context::pointer const &ctx, std::string_view xml);

        keymap(keymap const &)                = delete;
        keymap &operator=(keymap const &)     = delete;
        keymap(keymap &&) noexcept            = default;
        keymap &operator=(keymap &&) noexcept = default;
        ~keymap() noexcept;

        [[nodiscard]] pointer       getptr();
        [[nodiscard]] xkb_keymap   *get() const noexcept;
        [[nodiscard]] xkb_keycode_t min_keycode() const noexcept;
        [[nodiscard]] xkb_keycode_t max_keycode() const noexcept;

        [[nodiscard]] context::pointer get_context() const;

        /// Return a UTF-8 label for the keymap
        [[nodiscard]] std::string as_string() const;

      private:
        context::pointer ctx;
        xkb_keymap      *handle{};
    };

    [[nodiscard]] std::string name(xkb_keysym_t keysym);

} // namespace foresight::xkb
