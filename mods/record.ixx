// Created by moisrex on 8/17/26.

module;
#include <algorithm>
#include <concepts>
#include <functional>
#include <ranges>
#include <span>
#include <vector>
export module fs8.mods.record;
import fs8.event;
import fs8.context;
import fs8.pimpl;

namespace fs8 {

    /**
     * Record whatever events pass through it, then examine them.
     *
     * By default events are recorded into an internal buffer and examined
     * through `pipeline.mod<basic_record>()`. It can also be bound to an
     * external buffer (e.g. a global used by a test) via `record[sink]`, which
     * is handy when the record sits inside `on[...]` where it cannot be reached
     * after the pipeline runs.
     */
    export constexpr struct [[nodiscard]] basic_record : pimpl_idiom<basic_record> {
        using pimpl_idiom::pimpl_idiom;

        /// Bind to record into an external buffer. The buffer must live at a
        /// constant-expression address (e.g. a global/static) because this is a
        /// consteval factory used while building pipelines.
        consteval basic_record operator[](std::vector<event_type>& inp_sink) const noexcept {
            return basic_record{&inp_sink};
        }

        /// Pass-through pipeline mod: record the current event and continue.
        context_action operator()(Context auto& ctx) noexcept {
            return record_event(ctx.event());
        }

        // --- Examination --------------------------------------------------

        /// Read-only view over the recorded events (external sink or internal buffer).
        [[nodiscard]] std::span<event_type const> events() const noexcept;

        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] bool        empty() const noexcept;
        void                      clear() noexcept;

        [[nodiscard]] event_type const& operator[](std::size_t index) const noexcept;
        [[nodiscard]] event_type const& at(std::size_t index) const;
        [[nodiscard]] event_type const& front() const noexcept;
        [[nodiscard]] event_type const& back() const noexcept;

        [[nodiscard]] std::span<event_type const>::const_iterator begin() const noexcept;
        [[nodiscard]] std::span<event_type const>::const_iterator end() const noexcept;

        /// The recorded events as plain user_events (e.g. for comparison helpers).
        [[nodiscard]] std::vector<user_event> as_user_events() const noexcept;

        // --- Queries ------------------------------------------------------

        /// Number of recorded events for which `pred` is true.
        template <typename Pred>
            requires(!std::convertible_to<Pred, event_type::type_type>)
        [[nodiscard]] std::size_t count(Pred&& pred) const noexcept {
            return static_cast<std::size_t>(std::ranges::count_if(events(), std::forward<Pred>(pred)));
        }

        [[nodiscard]] std::size_t count(event_type::type_type type) const noexcept;
        [[nodiscard]] std::size_t count(event_type::type_type type, event_type::code_type code) const noexcept;

        /// Whether any recorded event satisfies `pred`.
        template <typename Pred>
        [[nodiscard]] bool any(Pred&& pred) const noexcept {
            return std::ranges::any_of(events(), std::forward<Pred>(pred));
        }

        /// Whether every recorded event satisfies `pred`.
        template <typename Pred>
        [[nodiscard]] bool all(Pred&& pred) const noexcept {
            return std::ranges::all_of(events(), std::forward<Pred>(pred));
        }

        /// A copy of the recorded events for which `pred` is true.
        template <typename Pred>
        [[nodiscard]] std::vector<event_type> filter(Pred&& pred) const noexcept {
            std::vector<event_type> out;
            for (auto const& event : events()) {
                if (std::invoke(std::forward<Pred>(pred), event)) {
                    out.push_back(event);
                }
            }
            return out;
        }

        /// Only the EV_KEY events, as a copy.
        [[nodiscard]] std::vector<event_type> keys() const noexcept;

        /// All recorded events except SYN_REPORTs, as a copy.
        [[nodiscard]] std::vector<event_type> without_syn() const noexcept;

      private:
        explicit consteval basic_record(std::vector<event_type>* inp_sink) noexcept : sink{inp_sink} {}

        context_action record_event(event_type const& event) noexcept;

        std::vector<event_type>* sink = nullptr; // null -> internal (pimpl) storage
    } record;

    static_assert(Modifier<basic_record>);

} // namespace fs8
