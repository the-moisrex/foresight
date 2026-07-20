// Created by moisrex on 12/16/25.

module;
#include <cassert>
#include <cstdint>
#include <libudev.h>
#include <string_view>
#include <utility>
export module fs8.devices.udev;

namespace fs8 {

    /**
     * @brief Manages the library context for libudev.
     *
     * This struct encapsulates the `::udev` context used as the base for all other udev operations.
     */
    export struct [[nodiscard]] udev {
        /**
         * @brief Constructs a new udev context.
         */
        udev() noexcept;

        /**
         * @brief Copy constructor creating a new reference to the context.
         * @param other The udev instance to copy.
         */
        udev(udev const& other) noexcept;

        /**
         * @brief Copy assignment operator.
         * @param other The udev instance to assign from.
         * @return Reference to this instance.
         */
        udev& operator=(udev const& other) noexcept;

        /**
         * @brief Move constructor.
         * @param other The udev instance to move from.
         */
        udev(udev&& other) noexcept;

        /**
         * @brief Move assignment operator.
         * @param other The udev instance to move from.
         * @return Reference to this instance.
         */
        udev& operator=(udev&& other) noexcept;

        /**
         * @brief Destroys the udev context, dropping the reference count.
         */
        ~udev() noexcept;

        /**
         * @brief Checks if the underlying udev context is valid (not null).
         * @return `true` if valid, `false` otherwise.
         */
        [[nodiscard]] bool is_valid() const noexcept;

        /**
         * @brief Boolean conversion operator, equivalent to is_valid().
         * @return `true` if valid, `false` otherwise.
         */
        [[nodiscard]] explicit operator bool() const noexcept;

        /**
         * @brief Retrieves the raw libudev context pointer.
         * @return Pointer of type `::udev*`.
         */
        [[nodiscard]] ::udev* native() const noexcept;

        /**
         * @brief Retrieves a singleton or shared instance of the udev context.
         * @return A valid udev instance.
         */
        static udev instance() noexcept;

      private:
        ::udev* handle;
    };

    /**
     * @brief Represents an entry in a udev linked list.
     */
    export struct [[nodiscard]] udev_list_entry {
      private:
        ::udev_list_entry* entry;

      public:
        /**
         * @brief Constructs a list entry from a raw libudev list entry pointer.
         * @param e Raw pointer to a `::udev_list_entry`.
         */
        explicit udev_list_entry(::udev_list_entry* e) noexcept : entry(e) {}

        /**
         * @brief Retrieves the name of the list entry.
         * @return A `std::string_view` representing the name.
         */
        [[nodiscard]] std::string_view name() const noexcept;

        /**
         * @brief Retrieves the value of the list entry.
         * @return A `std::string_view` representing the value.
         */
        [[nodiscard]] std::string_view value() const noexcept;

        /**
         * @brief Forward iterator for traversing udev list entries.
         */
        struct [[nodiscard]] iterator {
          private:
            ::udev_list_entry* current;

          public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = udev_list_entry;
            using difference_type   = std::ptrdiff_t;
            using pointer           = udev_list_entry*;
            using reference         = udev_list_entry;

            constexpr iterator() noexcept : current(nullptr) {}

            /**
             * @brief Constructs an iterator starting at the given list entry.
             * @param e Raw pointer to a `::udev_list_entry`.
             */
            explicit iterator(::udev_list_entry* e) noexcept : current(e) {}

            /**
             * @brief Dereferences the iterator to get the current list entry.
             * @return A `udev_list_entry` object.
             */
            [[nodiscard]] reference operator*() const noexcept {
                assert(current != nullptr);
                return udev_list_entry{current};
            }

            /**
             * @brief Pre-increments the iterator to the next entry.
             * @return Reference to the updated iterator.
             */
            iterator& operator++() noexcept {
                assert(current != nullptr);
                current = ::udev_list_entry_get_next(current);
                return *this;
            }

            /**
             * @brief Post-increments the iterator to the next entry.
             * @return A copy of the iterator before incrementing.
             */
            iterator operator++(int) noexcept {
                assert(current != nullptr);
                iterator const tmp = *this;
                ++(*this);
                return tmp;
            }

            /**
             * @brief Compares two iterators for equality.
             * @param other The iterator to compare with.
             * @return `true` if they point to the same entry, `false` otherwise.
             */
            [[nodiscard]] bool operator==(iterator const& other) const {
                return current == other.current;
            }
        };

        /**
         * @brief Returns an iterator to the beginning of the list.
         * @return An `iterator` object.
         */
        iterator begin() const noexcept {
            return iterator{entry};
        }

        /**
         * @brief Returns an iterator to the end of the list.
         * @return An `iterator` object pointing to `nullptr`.
         */
        iterator end() const noexcept {
            return iterator{nullptr};
        }
    };

    /**
     * @brief Represents a `udev` device.
     *
     * Provides an interface to access device properties, attributes, and hierarchy.
     */
    export struct [[nodiscard]] udev_device {
        /**
         * @brief Default constructs an invalid device.
         */
        udev_device() noexcept = default;

        /**
         * @brief Constructs a device from a given udev context.
         * @param ctx Raw pointer to the `::udev` context.
         */
        explicit udev_device(::udev* ctx) noexcept;

        /**
         * @brief Constructs a device taking ownership of a raw libudev device pointer.
         * @param device Raw pointer to a `::udev_device`.
         */
        explicit udev_device(::udev_device* device) noexcept : dev{device} {}

        /**
         * @brief Constructs a device from subsystem and sysname.
         * @param ctx Raw pointer to the `::udev` context.
         * @param subsystem Null-terminated string of the subsystem name.
         * @param sysname Null-terminated string of the sysname.
         */
        udev_device(::udev* ctx, char const* subsystem, char const* sysname) noexcept;

        udev_device(udev_list_entry const&) noexcept;

        /**
         * @brief Constructs a device from device type and number.
         * @param ctx Raw pointer to the `::udev` context.
         * @param type The device type character (e.g., 'c' or 'b').
         * @param devnum The `dev_t` device number.
         */
        udev_device(::udev* ctx, char type, dev_t devnum) noexcept;

        /**
         * @brief Constructs a device from a syspath.
         * @param ctx Raw pointer to the `::udev` context.
         * @param syspath Null-terminated string of the syspath.
         */
        udev_device(::udev* ctx, char const* syspath) noexcept;

        udev_device(udev_device const&) noexcept            = delete;
        udev_device& operator=(udev_device const&) noexcept = delete;
        udev_device(udev_device&&) noexcept;
        udev_device& operator=(udev_device&&) noexcept;

        /**
         * @brief Destroys the device, dropping its reference count.
         */
        ~udev_device() noexcept;

        /**
         * @brief Checks if the device instance holds a valid udev device pointer.
         * @return `true` if valid, `false` otherwise.
         */
        [[nodiscard]] bool is_valid() const noexcept;

        /**
         * @brief Boolean conversion operator, equivalent to is_valid().
         * @return `true` if valid, `false` otherwise.
         */
        [[nodiscard]] explicit operator bool() const noexcept;

        /**
         * @brief Retrieves the parent device.
         * @return A `udev_device` representing the parent.
         */
        udev_device parent() const noexcept;

        /**
         * @brief Retrieves the parent device matching a specific subsystem and type.
         * @param subsystem The subsystem to match (null-terminated).
         * @param devtype The device type to match (null-terminated), defaults to `nullptr`.
         * @return A `udev_device` representing the matching parent.
         */
        udev_device parent(char const* subsystem, char const* devtype = nullptr) const noexcept;

        /**
         * @brief Gets the subsystem of the device.
         * @return `std::string_view` of the subsystem.
         */
        [[nodiscard]] std::string_view subsystem() const noexcept;

        /**
         * @brief Gets the device type.
         * @return `std::string_view` of the device type.
         */
        [[nodiscard]] std::string_view devtype() const noexcept;

        /**
         * @brief Gets the syspath of the device.
         * @return `std::string_view` of the syspath.
         */
        [[nodiscard]] std::string_view syspath() const noexcept;

        /**
         * @brief Gets the sysname of the device.
         * @return `std::string_view` of the sysname.
         */
        [[nodiscard]] std::string_view sysname() const noexcept;

        /**
         * @brief Gets the instance number of the device.
         * @return `std::string_view` of the sysnum.
         */
        [[nodiscard]] std::string_view sysnum() const noexcept;

        /**
         * @brief Gets the device node path.
         * @return `std::string_view` of the devnode.
         */
        [[nodiscard]] std::string_view devnode() const noexcept;

        /**
         * @brief Gets the value of a specific property.
         * @param key The property name (null-terminated).
         * @return `std::string_view` of the property value.
         */
        [[nodiscard]] std::string_view property(char const* key) const noexcept;

        /**
         * @brief Gets the driver associated with the device.
         * @return `std::string_view` of the driver name.
         */
        [[nodiscard]] std::string_view driver() const noexcept;

        /**
         * @brief Gets the action (for udev events).
         * @return `std::string_view` of the action.
         */
        [[nodiscard]] std::string_view action() const noexcept;

        /**
         * @brief Gets a sysfs attribute value.
         * @param attr The attribute name (null-terminated).
         * @return `std::string_view` of the attribute value.
         */
        [[nodiscard]] std::string_view sysattr(char const* attr) const noexcept;

        /**
         * @brief Checks if the device has a specific tag.
         * @param tag The tag name (null-terminated).
         * @return `true` if the tag exists, `false` otherwise.
         */
        [[nodiscard]] bool has_tag(char const* tag) const noexcept;

        /**
         * @brief Retrieves the raw libudev device pointer.
         * @return Pointer of type `::udev_device*`.
         */
        [[nodiscard]] ::udev_device* native() const noexcept;

        /**
         * @brief Gets an iterable list of the device's properties.
         * @return A `udev_list_entry` for properties.
         */
        udev_list_entry properties() const noexcept;

        /**
         * @brief Gets an iterable list of the device's tags.
         * @return A `udev_list_entry` for tags.
         */
        udev_list_entry tags() const noexcept;

        /**
         * @brief Gets an iterable list of the device's sysfs attributes.
         * @return A `udev_list_entry` for sysattrs.
         */
        udev_list_entry sysattrs() const noexcept;

        /**
         * @brief Gets an iterable list of the device's devlinks.
         * @return A `udev_list_entry` for devlinks.
         */
        udev_list_entry devlinks() const noexcept;

        /**
         * @brief Checks if the device has been initialized by udev.
         * @return `true` if initialized, `false` otherwise.
         */
        [[nodiscard]] bool is_initialized() const noexcept;

        /**
         * @brief Gets the time in microseconds since the device was initialized.
         * @return `std::uint64_t` representing microseconds.
         */
        [[nodiscard]] std::uint64_t usec_since_initialized() const noexcept;

        /**
         * @brief Gets the udev sequence number for the device event.
         * @return `std::uint64_t` representing the sequence number.
         */
        [[nodiscard]] std::uint64_t seqnum() const noexcept;

      private:
        ::udev_device* dev = nullptr;
    };

    /**
     * @brief Used to query and list devices currently known to udev.
     */
    export struct [[nodiscard]] udev_enumerate {
        /**
         * @brief Constructs a udev enumerator bound to a udev context.
         * @param ctx The `udev` context.
         */
        explicit udev_enumerate(udev const& ctx) noexcept;

        /**
         * @brief Default constructs an unassociated udev_enumerate.
         */
        udev_enumerate() noexcept;

        udev_enumerate(udev_enumerate const&) = delete;

        /**
         * @brief Move constructor.
         * @param other The object to move from.
         */
        udev_enumerate(udev_enumerate&& other) noexcept;
        udev_enumerate& operator=(udev_enumerate const&) = delete;

        /**
         * @brief Move assignment operator.
         * @param other The object to move from.
         * @return Reference to this instance.
         */
        udev_enumerate& operator=(udev_enumerate&& other) noexcept;

        /**
         * @brief Destroys the enumerator.
         */
        ~udev_enumerate() noexcept;

        /**
         * @brief Retrieves the raw libudev enumerate pointer.
         * @return Pointer of type `::udev_enumerate*`.
         */
        [[nodiscard]] ::udev_enumerate* native() const noexcept;

        /**
         * @brief Checks if the enumerator is valid.
         * @return `true` if valid, `false` otherwise.
         */
        [[nodiscard]] bool is_valid() const noexcept;

        /**
         * @brief Boolean conversion operator, equivalent to is_valid().
         * @return `true` if valid, `false` otherwise.
         */
        [[nodiscard]] explicit operator bool() const noexcept;

        /**
         * @brief Adds a subsystem match to the enumerator filter.
         * @param subsystem The subsystem to match (null-terminated).
         * @return Reference to this instance for chaining.
         */
        udev_enumerate& match_subsystem(char const* subsystem) noexcept;

        /**
         * @brief Adds a negative subsystem match to the enumerator filter.
         * @param subsystem The subsystem to exclude (null-terminated).
         * @return Reference to this instance for chaining.
         */
        udev_enumerate& nomatch_subsystem(char const* subsystem) noexcept;

        /**
         * @brief Adds a sysfs attribute match to the enumerator filter.
         * @param name The attribute name (null-terminated).
         * @param value The attribute value to match (null-terminated), or `nullptr` to just test existence.
         * @return Reference to this instance for chaining.
         */
        udev_enumerate& match_sysattr(char const* name, char const* value = nullptr) noexcept;

        /**
         * @brief Adds a negative sysfs attribute match to the enumerator filter.
         * @param name The attribute name (null-terminated).
         * @param value The attribute value to exclude (null-terminated).
         * @return Reference to this instance for chaining.
         */
        udev_enumerate& nomatch_sysattr(char const* name, char const* value = nullptr) noexcept;

        /**
         * @brief Adds a property match to the enumerator filter.
         * @param name The property name (null-terminated).
         * @param value The property value to match (null-terminated).
         * @return Reference to this instance for chaining.
         */
        udev_enumerate& match_property(char const* name, char const* value = nullptr) noexcept;

        /**
         * @brief Adds a sysname match to the enumerator filter.
         * @param sysname The sysname to match (null-terminated).
         * @return Reference to this instance for chaining.
         */
        udev_enumerate& match_sysname(char const* sysname) noexcept;

        /**
         * @brief Adds a tag match to the enumerator filter.
         * @param tag The tag to match (null-terminated).
         * @return Reference to this instance for chaining.
         */
        udev_enumerate& match_tag(char const* tag) noexcept;

        /**
         * @brief Adds a parent device match to the enumerator filter.
         * @param parent The `udev_device` to match as parent.
         * @return Reference to this instance for chaining.
         */
        udev_enumerate& match_parent(udev_device const& parent) noexcept;

        /**
         * @brief Retrieves the list entries containing the scan results.
         * @return A `udev_list_entry` representing the head of the result list.
         */
        udev_list_entry list_entries() const noexcept;

        /**
         * @brief Scans sysfs for devices matching the applied filters.
         */
        void scan_devices() noexcept;

        /**
         * @brief Scans sysfs for subsystems matching the applied filters.
         */
        void scan_subsystems() noexcept;

      private:
        ::udev_enumerate* handle = nullptr;
        int               code   = 0;
    };

    /**
     * @brief Monitors udev for device state changes and events.
     */
    export struct [[nodiscard]] udev_monitor {
        /**
         * @brief Constructs a udev monitor from a udev context.
         * @param ctx The `udev` context.
         */
        explicit udev_monitor(udev const& ctx) noexcept;

        /**
         * @brief Default constructs with a global udev instance.
         */
        udev_monitor() noexcept;

        udev_monitor(udev_monitor const&)            = delete;
        udev_monitor& operator=(udev_monitor const&) = delete;

        /**
         * @brief Move constructor.
         * @param other The object to move from.
         */
        udev_monitor(udev_monitor&& other) noexcept;

        /**
         * @brief Move assignment operator.
         * @param other The object to move from.
         * @return Reference to this instance.
         */
        udev_monitor& operator=(udev_monitor&& other) noexcept;

        /**
         * @brief Destroys the monitor.
         */
        ~udev_monitor() noexcept;

        /**
         * @brief Adds a filter to match specific subsystems and device types.
         * @param subsystem The subsystem to monitor (null-terminated).
         * @param type The device type to monitor (null-terminated), defaults to `nullptr`.
         */
        void match_device(char const* subsystem, char const* type = nullptr) noexcept;

        /**
         * @brief Adds a filter to match specific tags.
         * @param tag The tag to monitor (null-terminated).
         */
        void match_tag(char const* tag) noexcept;

        /**
         * @brief Checks if the monitor is valid.
         * @return `true` if valid, `false` otherwise.
         */
        [[nodiscard]] bool is_valid() const noexcept;

        /**
         * @brief Retrieves the file descriptor of the monitor for manual event polling (e.g., using `epoll` or `select`).
         * @return An `int` representing the file descriptor.
         */
        [[nodiscard]] int file_descriptor() const noexcept;

        /**
         * @brief Binds the monitor socket and enables receiving events.
         */
        void enable() noexcept;

        /**
         * @brief Receives the next device event from the monitor.
         * @return A `udev_device` instance associated with the received event.
         */
        udev_device next_device() const noexcept;

        /**
         * @brief Sets the size of the receive buffer for the monitor socket.
         * @param size The buffer size in bytes.
         */
        void set_receive_buffer_size(int size) noexcept;

        /**
         * @brief Updates the active monitor filters to match the currently configured rules.
         */
        void filter_update() noexcept;

        /**
         * @brief Removes all active filters from the monitor.
         */
        void filter_remove() noexcept;

        // todo: add manually watching in this class for completeness as well
      private:
        ::udev_monitor* mon  = nullptr;
        int             code = 0;
    };

    /**
     * @brief Interacts with the udev hardware database (hwdb).
     */
    export struct [[nodiscard]] udev_hwdb {
      private:
        ::udev_hwdb* handle;

      public:
        /**
         * @brief Constructs a hwdb interface using a udev context.
         * @param ctx Raw pointer to the `::udev` context.
         */
        explicit udev_hwdb(::udev* ctx);

        /**
         * @brief Destroys the hwdb interface.
         */
        ~udev_hwdb();

        udev_hwdb(udev_hwdb const&)            = delete;
        udev_hwdb& operator=(udev_hwdb const&) = delete;

        /**
         * @brief Move constructor.
         * @param other The object to move from.
         */
        udev_hwdb(udev_hwdb&& other) noexcept;

        /**
         * @brief Retrieves properties from the hwdb for a given modalias.
         * @param modalias The modalias string (null-terminated).
         * @param flags Query flags, defaults to 0.
         * @return A `udev_list_entry` containing matched properties.
         */
        udev_list_entry get_properties(char const* modalias, unsigned int flags = 0) const noexcept;
    };

    /**
     * @brief Interfaces with the udev event queue.
     */
    export struct [[nodiscard]] udev_queue {
      private:
        ::udev_queue* handle;

      public:
        /**
         * @brief Constructs a udev queue utilizing the provided context.
         * @param ctx Raw pointer to the `::udev` context.
         */
        explicit udev_queue(::udev* ctx);

        /**
         * @brief Destroys the udev queue interface.
         */
        ~udev_queue();

        udev_queue(udev_queue const&)            = delete;
        udev_queue& operator=(udev_queue const&) = delete;

        /**
         * @brief Move constructor.
         * @param other The object to move from.
         */
        udev_queue(udev_queue&& other) noexcept;

        /**
         * @brief Checks if the udev daemon is active (processing events).
         * @return `true` if active, `false` otherwise.
         */
        [[nodiscard]] bool is_active() const noexcept;

        /**
         * @brief Checks if the udev event queue is currently empty.
         * @return `true` if empty, `false` otherwise.
         */
        [[nodiscard]] bool is_empty() const noexcept;
    };

} // namespace fs8
