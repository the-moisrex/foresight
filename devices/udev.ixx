// Created by moisrex on 12/16/25.

module;
#include <cstdint>
#include <libudev.h>
#include <string_view>
#include <utility>
export module foresight.devices.udev;

namespace fs8 {

    /**
     * This struct manages udev context.
     */
    export struct [[nodiscard]] udev {
        udev() noexcept;
        udev(udev const&) noexcept;
        udev& operator=(udev const&) noexcept;
        udev(udev&&) noexcept;
        udev& operator=(udev&&) noexcept;
        ~udev();

        [[nodiscard]] bool is_valid() const noexcept;

        /// is_valid
        [[nodiscard]] explicit operator bool() const noexcept;

        [[nodiscard]] ::udev* native() const noexcept;

        static udev instance();

      private:
        ::udev* handle;
    };

    export struct [[nodiscard]] udev_list_entry {
      private:
        ::udev_list_entry* entry;

      public:
        explicit udev_list_entry(::udev_list_entry* e) noexcept : entry(e) {}

        [[nodiscard]] std::string_view name() const noexcept;
        [[nodiscard]] std::string_view value() const noexcept;

        struct [[nodiscard]] iterator {
          private:
            ::udev_list_entry* current;

          public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = udev_list_entry;
            using difference_type   = std::ptrdiff_t;
            using pointer           = udev_list_entry*;
            using reference         = udev_list_entry;

            explicit iterator(::udev_list_entry* e) noexcept : current(e) {}

            [[nodiscard]] reference operator*() const noexcept {
                return udev_list_entry{current};
            }

            iterator& operator++() noexcept {
                current = ::udev_list_entry_get_next(current);
                return *this;
            }

            iterator operator++(int) noexcept {
                iterator const tmp = *this;
                ++(*this);
                return tmp;
            }

            [[nodiscard]] bool operator==(iterator const& other) const {
                return current == other.current;
            }
        };

        iterator begin() const noexcept {
            return iterator{entry};
        }

        iterator end() const noexcept {
            return iterator{nullptr};
        }
    };

    /**
     * Represents a `udev` device.
     */
    export struct [[nodiscard]] udev_device {
        udev_device() noexcept = default;
        explicit udev_device(::udev* ctx);

        explicit udev_device(::udev_device* device) noexcept : dev{device} {}

        udev_device(::udev* ctx, char const* subsystem, char const* sysname);
        udev_device(::udev* ctx, char type, dev_t devnum);

        udev_device(::udev*, char const*) noexcept;
        udev_device(udev_device const&) noexcept            = delete;
        udev_device& operator=(udev_device const&) noexcept = delete;
        udev_device(udev_device&&) noexcept                 = default;
        udev_device& operator=(udev_device&&) noexcept      = default;
        ~udev_device() noexcept;

        [[nodiscard]] bool is_valid() const noexcept;

        /// is_valid
        [[nodiscard]] explicit operator bool() const noexcept;

        udev_device parent() const noexcept;
        udev_device parent(char const* subsystem, char const* devtype = nullptr) const noexcept;

        [[nodiscard]] std::string_view subsystem() const noexcept;
        [[nodiscard]] std::string_view devtype() const noexcept;
        [[nodiscard]] std::string_view syspath() const noexcept;
        [[nodiscard]] std::string_view sysname() const noexcept;
        [[nodiscard]] std::string_view sysnum() const noexcept;
        [[nodiscard]] std::string_view devnode() const noexcept;
        [[nodiscard]] std::string_view property(char const*) const noexcept;
        [[nodiscard]] std::string_view driver() const noexcept;
        [[nodiscard]] std::string_view action() const noexcept;
        [[nodiscard]] std::string_view sysattr(char const*) const noexcept;
        [[nodiscard]] bool             has_tag(char const*) const noexcept;

        [[nodiscard]] ::udev_device* native() const noexcept;

        // List iterators
        udev_list_entry properties() const noexcept;
        udev_list_entry tags() const noexcept;
        udev_list_entry sysattrs() const noexcept;
        udev_list_entry devlinks() const noexcept;

        // State and Sequence
        [[nodiscard]] bool          is_initialized() const noexcept;
        [[nodiscard]] std::uint64_t usec_since_initialized() const noexcept;
        [[nodiscard]] std::uint64_t seqnum() const noexcept;

      private:
        ::udev_device* dev = nullptr;
    };

    /**
     * List the devices
     */
    export struct [[nodiscard]] udev_enumerate {
        explicit udev_enumerate(udev const&) noexcept;
        udev_enumerate() noexcept;
        udev_enumerate(udev_enumerate const&) = delete;
        udev_enumerate(udev_enumerate&&) noexcept;
        udev_enumerate& operator=(udev_enumerate const&) = delete;
        udev_enumerate& operator=(udev_enumerate&&) noexcept;
        ~udev_enumerate() noexcept;

        [[nodiscard]] ::udev_enumerate* native() const noexcept;

        [[nodiscard]] bool is_valid() const noexcept;

        [[nodiscard]] explicit operator bool() const noexcept;

        // we use char const* because std::string_view may not be null terminated.
        udev_enumerate& match_subsystem(char const*) noexcept;
        udev_enumerate& nomatch_subsystem(char const*) noexcept;

        udev_enumerate& match_sysattr(char const* name, char const* value = nullptr) noexcept;
        udev_enumerate& nomatch_sysattr(char const* name, char const* value = nullptr) noexcept;

        udev_enumerate& match_property(char const* name, char const* value = nullptr) noexcept;

        udev_enumerate& match_sysname(char const*) noexcept;
        udev_enumerate& match_tag(char const*) noexcept;

        udev_enumerate& match_parent(udev_device const&) noexcept;

        udev_list_entry list_entries() const noexcept;

        void scan_devices() noexcept;
        void scan_subsystems() noexcept;

      private:
        ::udev_enumerate* handle = nullptr;
        int               code   = 0;
    };

    /**
     * Monitor udev devices for events
     */
    export struct [[nodiscard]] udev_monitor {
        explicit udev_monitor(udev const&) noexcept;
        udev_monitor() noexcept;
        udev_monitor(udev_monitor const&)            = delete;
        udev_monitor& operator=(udev_monitor const&) = delete;
        udev_monitor(udev_monitor&&) noexcept;
        udev_monitor& operator=(udev_monitor&&) noexcept;
        ~udev_monitor() noexcept;

        void match_device(char const* subsystem, char const* type = nullptr) noexcept;
        void match_tag(char const*) noexcept;

        [[nodiscard]] bool is_valid() const noexcept;

        /// Get the file descriptor so you can manually watch for it.
        [[nodiscard]] int file_descriptor() const noexcept;

        /// Enable watching
        void enable() noexcept;

        /// Get the device that just we have received a new event for
        udev_device next_device() const noexcept;

        void set_receive_buffer_size(int size) noexcept;
        void filter_update() noexcept;
        void filter_remove() noexcept;

        // todo: add manually watching in this class for completeness as well
      private:
        ::udev_monitor* mon;
        int             fd   = 0;
        int             code = 0;
    };

    export struct [[nodiscard]] udev_hwdb {
      private:
        ::udev_hwdb* handle;

      public:
        explicit udev_hwdb(::udev* ctx);

        ~udev_hwdb();

        udev_hwdb(udev_hwdb const&)            = delete;
        udev_hwdb& operator=(udev_hwdb const&) = delete;

        udev_hwdb(udev_hwdb&& other) noexcept;

        udev_list_entry get_properties(char const* modalias, unsigned int flags = 0) const noexcept;
    };

    export struct [[nodiscard]] udev_queue {
      private:
        ::udev_queue* handle;

      public:
        explicit udev_queue(::udev* ctx);

        ~udev_queue();

        udev_queue(udev_queue const&)            = delete;
        udev_queue& operator=(udev_queue const&) = delete;

        udev_queue(udev_queue&& other) noexcept;

        [[nodiscard]] bool is_active() const noexcept;
        [[nodiscard]] bool is_empty() const noexcept;
    };


} // namespace fs8
