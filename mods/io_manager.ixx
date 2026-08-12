// Created by moisrex on 8/8/26.

module;
#include <functional>
#include <sys/poll.h>
#include <type_traits>
#include <utility>
export module fs8.mods.io_manager;
import fs8.context;
import fs8.pimpl;

export namespace fs8 {

    /// Events that can be watched and reported for a file descriptor.
    /// The values are the native poll masks, so no translation is needed.
    /// `pri` and `nval` are not watchable but are reported in `revents`, so
    /// every bit `poll` can produce is representable.
    enum struct [[nodiscard]] io_event : short {
        none = 0,
        in   = POLLIN,
        out  = POLLOUT,
        pri  = POLLPRI,
        err  = POLLERR,
        hup  = POLLHUP,
        nval = POLLNVAL,
    };

    [[nodiscard]] constexpr io_event operator|(io_event const lhs, io_event const rhs) noexcept {
        return static_cast<io_event>(std::to_underlying(lhs) | std::to_underlying(rhs));
    }

    [[nodiscard]] constexpr io_event operator&(io_event const lhs, io_event const rhs) noexcept {
        return static_cast<io_event>(std::to_underlying(lhs) & std::to_underlying(rhs));
    }

    [[nodiscard]] constexpr io_event operator~(io_event const rhs) noexcept {
        return static_cast<io_event>(~std::to_underlying(rhs));
    }

    [[nodiscard]] constexpr bool has(io_event const set, io_event const flag) noexcept {
        return (std::to_underlying(set) & std::to_underlying(flag)) != 0;
    }

    /// A watched file descriptor and its state.
    struct [[nodiscard]] io_fd {
        int      fd      = -1;
        io_event events  = io_event::in;
        io_event revents = io_event::none; // filled by the manager before dispatching
    };

    template <typename T>
    concept io_handler = !Context<T> && std::is_nothrow_invocable_r_v<context_action, T&, io_fd const&>;

    /**
     * Register file descriptors, wait for events on all of them at once, and dispatch
     * each ready file descriptor to its registered handler.
     * The handlers are bound by reference, so their lifetime must be as long as this
     * manager's (e.g. mods living in the same pipeline).
     */
    constexpr struct [[nodiscard]] basic_io_manager : pimpl_idiom<basic_io_manager> {
        using io_callback = std::function_ref<context_action(io_fd const&)>;

        template <io_handler HandlerT>
        [[nodiscard]] bool watch(io_fd const& fd, HandlerT& handler) noexcept {
            return watch(fd, io_callback{handler});
        }

        void                      unwatch(int fd) noexcept;
        void                      clear() noexcept;
        [[nodiscard]] bool        is_watched(int fd) const noexcept;
        [[nodiscard]] bool        empty() const noexcept;
        [[nodiscard]] std::size_t size() const noexcept;

        context_action operator()(start_tag) noexcept;
        context_action operator()(load_event_tag) noexcept;

      private:
        [[nodiscard]] bool watch(io_fd const& fd, io_callback const& cb) noexcept;
    } io_manager;

} // namespace fs8
