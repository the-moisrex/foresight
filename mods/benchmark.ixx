// Created by moisrex on 8/22/26.

module;
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <utility>
export module fs8.mods:benchmark;
import fs8.context;
import fs8.traits;

namespace fs8 {

    export template <Modifier... Funcs>
    struct [[nodiscard]] basic_benchmark : consteval_copyable {
        using consteval_copyable::consteval_copyable;
        using mods_type = std::tuple<std::remove_cvref_t<Funcs>...>;
        using clock     = std::chrono::steady_clock;

        struct [[nodiscard]] counter {
            using duration = std::chrono::nanoseconds;

            std::uint64_t calls = 0;
            duration      total{};
            duration      min{duration::max()};
            duration      max{};

            // Running variance (Welford's online algorithm)
            double mean = 0.0;
            double m2   = 0.0;

            // Last N samples for percentile estimation (circular buffer, sorted on read)
            static constexpr std::size_t      max_samples = 100;
            std::array<duration, max_samples> samples{};
            std::size_t                       sample_count = 0;
            std::size_t                       sample_write = 0;

            [[nodiscard]] constexpr duration average() const noexcept {
                return calls == 0 ? duration{} : total / static_cast<std::int64_t>(calls);
            }

            [[nodiscard]] constexpr double std_deviation() const noexcept {
                return calls < 2 ? 0.0 : std::sqrt(m2 / static_cast<double>(calls - 1));
            }

            [[nodiscard]] constexpr duration percentile(double p) const noexcept {
                if (sample_count == 0) {
                    return {};
                }
                // Copy samples into a sorted array
                std::array<duration, max_samples> sorted{};
                auto const                        n = std::min(sample_count, max_samples);
                for (std::size_t i = 0; i < n; ++i) {
                    sorted[i] = samples[i];
                }
                std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(n));
                auto const idx = static_cast<std::size_t>(std::clamp(p * static_cast<double>(n - 1), 0.0, static_cast<double>(n - 1)));
                return sorted[idx];
            }

            constexpr void record(duration elapsed) noexcept {
                ++calls;
                total += elapsed;
                if (elapsed < min) {
                    min = elapsed;
                }
                if (elapsed > max) {
                    max = elapsed;
                }

                // Welford's online variance
                auto const delta   = static_cast<double>(elapsed.count()) - mean;
                mean              += delta / static_cast<double>(calls);
                auto const delta2  = static_cast<double>(elapsed.count()) - mean;
                m2                += delta * delta2;

                // Circular buffer for percentile samples
                samples[sample_write] = elapsed;
                sample_write          = (sample_write + 1) % max_samples;
                if (sample_count < max_samples) {
                    ++sample_count;
                }
            }

            constexpr void clear() noexcept {
                *this = {};
            }
        };

      private:
        std::string_view name;
        mods_type        funcs{};
        counter          counter_{};

      public:
        explicit constexpr basic_benchmark(std::remove_cvref_t<Funcs>... inp_funcs) noexcept : funcs{inp_funcs...} {}

        constexpr basic_benchmark(std::string_view inp_name, std::remove_cvref_t<Funcs>... inp_funcs) noexcept
          : name{inp_name},
            funcs{inp_funcs...} {}

        /// benchmark[context | mod1 | mod2]: wrap all mods into a single benchmark.
        template <Context CtxT>
        [[nodiscard]] consteval auto operator[](CtxT const& ctx) const noexcept {
            return std::apply(
              []<typename... ModT>(ModT const&... mods) constexpr noexcept {
                  return basic_benchmark<std::remove_cvref_t<ModT>...>{mods...};
              },
              ctx.get_mods());
        }

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            auto const started = clock::now();
            auto const action  = invoke_mods(ctx, funcs);
            counter_.record(std::chrono::duration_cast<typename counter::duration>(clock::now() - started));
            return action;
        }

        template <Context CtxT, typename... TagTs>
            requires(sizeof...(TagTs) == 1 && (std::same_as<std::remove_cvref_t<TagTs>, special_event> && ...))
        context_action operator()(CtxT& ctx, TagTs... tags) noexcept {
            using enum context_action;
            // Lifecycle events (toggle_on/toggle_off) are for the `on` block, not the
            // benchmarked mods.  Forwarding them would cause inner mods (which don't
            // handle special events) to return drop_event, making the on block abort.
            return [&]<typename Tag>(Tag const& tag) noexcept -> context_action {
                if (is_lifecycle_event(tag)) {
                    return next;
                }
                return invoke_mods(ctx, funcs, tag);
            }(tags...);
        }

        template <Context CtxT, typename... Args>
            requires(sizeof...(Args) >= 2)
        context_action operator()(CtxT& ctx, Args const&... args) noexcept {
            return invoke_mods(ctx, funcs, args...);
        }

        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) sub_mods(this Self&& self) noexcept {
            return std::forward_like<Self>(self.funcs);
        }

        [[nodiscard]] std::string_view get_name() const noexcept {
            return name;
        }

        [[nodiscard]] counter const& result() const noexcept {
            return counter_;
        }

        void clear() noexcept {
            counter_.clear();
        }
    };

    namespace benchmark_detail {
        template <typename ModT>
        [[nodiscard]] consteval auto benchmark_all_create(ModT const& mod) noexcept {
            return basic_benchmark<ModT>{pretty_type_name<ModT>(), mod};
        }

        struct [[nodiscard]] benchmark_all_factory {
            template <Context CtxT>
            [[nodiscard]] consteval auto operator[](CtxT const& ctx) const noexcept {
                return std::apply(
                  []<typename... ModT>(ModT const&... mods) constexpr noexcept {
                      return (context | ... | benchmark_all_create(mods));
                  },
                  ctx.get_mods());
            }
        };

        // Named benchmark proxy: benchmark["name"] | context | mod
        struct [[nodiscard]] named_benchmark_proxy {
            std::string_view name;

            template <Context CtxT>
            [[nodiscard]] consteval auto operator[](CtxT const& ctx) const noexcept {
                return std::apply(
                  [&]<typename... ModT>(ModT const&... mods) constexpr noexcept {
                      return basic_benchmark<std::remove_cvref_t<ModT>...>{name, mods...};
                  },
                  ctx.get_mods());
            }

            template <Modifier... Ms>
            [[nodiscard]] consteval auto operator|(basic_benchmark<Ms...> const& b) const noexcept {
                return std::apply(
                  [&]<typename... ModT>(ModT const&... mods) constexpr noexcept {
                      return basic_benchmark<std::remove_cvref_t<ModT>...>{name, mods...};
                  },
                  b.sub_mods());
            }
        };
    } // namespace benchmark_detail

    export template <typename SinkT>
    struct [[nodiscard]] basic_benchmark_result : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        [[no_unique_address]] SinkT sink;
        bool                        clear_after = false;
        std::string_view            name_filter = ""; // empty = report all

      public:
        constexpr basic_benchmark_result() noexcept = default;

        explicit constexpr basic_benchmark_result(SinkT inp_sink) noexcept : sink{std::move(inp_sink)} {}

        constexpr basic_benchmark_result(SinkT inp_sink, bool inp_clear) noexcept : sink{std::move(inp_sink)}, clear_after{inp_clear} {}

        constexpr basic_benchmark_result(SinkT inp_sink, bool inp_clear, std::string_view inp_filter) noexcept
          : sink{std::move(inp_sink)},
            clear_after{inp_clear},
            name_filter{inp_filter} {}

        /// Handle lifecycle events (toggle_on / toggle_off) transparently.
        context_action operator()(special_event const& tag) noexcept {
            using enum context_action;
            return is_lifecycle_event(tag) ? next : drop_event;
        }

        context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;
            auto visit = [&](auto& mod) noexcept {
                if constexpr (requires { mod.result(); }) {
                    auto const bname   = mod.get_name();
                    auto const display = bname.empty() ? std::string_view{"benchmark"} : bname;
                    // Apply name filter
                    if (!name_filter.empty() && display != name_filter) {
                        return;
                    }
                    auto const& c = mod.result();
                    sink("{}: calls={} total={}ns average={}ns min={}ns max={}ns stddev={:.2f}ns p50={}ns p95={}ns p99={}ns",
                         display,
                         c.calls,
                         c.total.count(),
                         c.average().count(),
                         c.calls == 0 ? 0 : c.min.count(),
                         c.max.count(),
                         c.std_deviation(),
                         c.percentile(0.50).count(),
                         c.percentile(0.95).count(),
                         c.percentile(0.99).count());
                }
                if constexpr (requires { mod.clear(); }) {
                    if (clear_after) {
                        mod.clear();
                    }
                }
            };
            std::apply(
              [&](auto&... mod) noexcept {
                  (walk_mod_tree(mod, visit), ...);
              },
              ctx.get_mods());
            return drop_event;
        }

        /// benchmark_result[sink] / benchmark_result[sink, true] / benchmark_result[sink, true, "name"]
        template <typename InpSinkT>
        [[nodiscard]] consteval auto operator[](InpSinkT inp_sink, bool clear = false, std::string_view filter = "") const noexcept {
            return basic_benchmark_result<std::remove_cvref_t<InpSinkT>>{std::move(inp_sink), clear, filter};
        }
    };

    export constexpr basic_benchmark<>                       benchmark;
    export constexpr benchmark_detail::benchmark_all_factory benchmark_all;
    export basic_benchmark_result<std::nullptr_t>            benchmark_result;

    static_assert(Modifier<basic_benchmark<>>);

} // namespace fs8
