// Created by moisrex on 8/8/26.

module;
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <sys/poll.h>
#include <type_traits>
#include <utility>
export module fs8.mods:io_manager;
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
        bool     unwatch = false;          // set by callback to request removal
    };

    template <typename T>
    concept io_handler = !Context<T> && std::is_nothrow_invocable_r_v<context_action, T&, io_fd&>;

    /**
     * Register file descriptors, wait for events on all of them at once, and dispatch
     * each ready file descriptor to its registered handler.
     * The handlers are bound by reference, so their lifetime must be as long as this
     * manager's (e.g. mods living in the same pipeline).
     */
    constexpr struct [[nodiscard]] basic_io_manager : pimpl_idiom<basic_io_manager> {
        using io_callback   = std::function_ref<context_action(io_fd&)>;
        using idle_callback = std::function<context_action(std::chrono::microseconds)>;

        template <io_handler HandlerT>
        [[nodiscard]] bool watch(io_fd const& fd, HandlerT& handler) noexcept {
            return watch(fd, io_callback{handler});
        }

        void                      unwatch(int fd) noexcept;
        void                      clear() noexcept;
        [[nodiscard]] bool        is_watched(int fd) const noexcept;
        [[nodiscard]] bool        empty() const noexcept;
        [[nodiscard]] std::size_t size() const noexcept;

        /// Configure an idle timeout.  When no fd is ready for `timeout` microseconds,
        /// the registered idle callback is invoked.  Pass `0` to disable.
        void set_idle_timeout(std::chrono::microseconds timeout) noexcept;

        /// Disable the idle timeout.
        void clear_idle_timeout() noexcept;

        /// Register a callback invoked when the idle timeout fires.
        /// The callback receives the idle timeout duration.
        void set_idle_callback(idle_callback cb) noexcept;

        /// Unregister the idle callback.
        void clear_idle_callback() noexcept;

        context_action operator()(control_event const& event) noexcept;

      private:
        [[nodiscard]] bool watch(io_fd const& fd, io_callback const& cb) noexcept;
    } io_manager;

    /// Result of an `io_watch`, written back into the request by the
    /// `io_manager` while it handles the broadcast.
    enum struct [[nodiscard]] io_watch_status : std::uint8_t {
        no_poller,  ///< no `io_manager` in this pipeline: nobody handled the broadcast
        failed,     ///< `io_manager` is present but refused the fd (fd < 0 / allocation)
        registered, ///< fd watched (or replaced in place)
    };

    /// Payload for `io_watch`: the fd to watch plus the handler to dispatch to.
    /// `status` is the out-parameter.  The handler is bound *by reference* and
    /// must outlive the registration — same rule as `io_manager` handlers and
    /// `query_provider_handle`.  Trivially copyable and allocation-free: the
    /// `std::function_ref` stores a pointer, not a closure.
    struct [[nodiscard]] io_watch_request {
        io_fd                         fd{};
        basic_io_manager::io_callback callback;
        io_watch_status               status = io_watch_status::no_poller;
    };

    static_assert(std::is_trivially_copyable_v<io_watch_request>);

    /// Type-erase `handler` into an `io_watch` request (mirrors
    /// `provider_handle`).  The request is a plain value: hand it to
    /// `broadcast(io_watch + &req)` and read back `req.status`.
    template <io_handler HandlerT>
    [[nodiscard]] io_watch_request watch_of(io_fd const& fd, HandlerT& handler) noexcept {
        return io_watch_request{
          .fd       = fd,
          .callback = basic_io_manager::io_callback{handler},
          .status   = io_watch_status::no_poller,
        };
    }

    template <control_event CEvent>
        requires(io_watch == CEvent)
    [[nodiscard]] constexpr io_watch_request& payload(control_event const& event) noexcept {
        if (event.payload == nullptr) [[unlikely]] {
            std::terminate();
        }
        return *static_cast<io_watch_request*>(event.payload);
    }

    template <control_event CEvent>
        requires(io_unwatch == CEvent)
    [[nodiscard]] constexpr int payload(control_event const& event) noexcept {
        if (event.payload == nullptr) [[unlikely]] {
            std::terminate();
        }
        return *static_cast<int*>(event.payload);
    }

    template <control_event CEvent>
        requires(io_idle_timeout == CEvent)
    [[nodiscard]] constexpr std::chrono::microseconds payload(control_event const& event) noexcept {
        if (event.payload == nullptr) [[unlikely]] {
            std::terminate();
        }
        return *static_cast<std::chrono::microseconds*>(event.payload);
    }

    /// Payload for `io_idle_callback`.  `io_manager` *moves* the callable out
    /// of it, so the caller must not reuse it after the broadcast.
    template <control_event CEvent>
        requires(io_idle_callback == CEvent)
    [[nodiscard]] constexpr basic_io_manager::idle_callback& payload(control_event const& event) noexcept {
        if (event.payload == nullptr) [[unlikely]] {
            std::terminate();
        }
        return *static_cast<basic_io_manager::idle_callback*>(event.payload);
    }

} // namespace fs8
