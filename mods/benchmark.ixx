// Created by moisrex on 8/22/26.

module;
#include <chrono>
#include <cstdint>
#include <limits>
#include <tuple>
#include <utility>
export module fs8.mods:benchmark;
import fs8.context;
import fs8.pimpl;
import fs8.traits;

namespace fs8 {

    export struct [[nodiscard]] benchmark_stats {
        using duration = std::chrono::nanoseconds;

        std::uint64_t calls = 0;
        duration      total{};
        duration      min{duration::max()};
        duration      max{};

        [[nodiscard]] constexpr duration average() const noexcept {
            return calls == 0 ? duration{} : total / static_cast<std::int64_t>(calls);
        }
    };

    export struct [[nodiscard]] basic_benchmark_counter : pimpl_idiom<basic_benchmark_counter> {
        using pimpl_idiom::pimpl_idiom;

        void                          record(benchmark_stats::duration elapsed) noexcept;
        [[nodiscard]] benchmark_stats result() const noexcept;
        void                          clear() noexcept;
    };

    export template <Modifier... Funcs>
    struct [[nodiscard]] basic_benchmark : consteval_copyable {
        using consteval_copyable::consteval_copyable;
        using mods_type = std::tuple<std::remove_cvref_t<Funcs>...>;
        using clock     = std::chrono::steady_clock;

      private:
        mods_type               funcs{};
        basic_benchmark_counter counter{};

      public:
        explicit constexpr basic_benchmark(std::remove_cvref_t<Funcs>... inp_funcs) noexcept : funcs{inp_funcs...} {}

        template <Context CtxT>
        context_action operator()(CtxT& ctx) noexcept {
            auto const started = clock::now();
            auto const action  = invoke_mods(ctx, funcs);
            counter.record(std::chrono::duration_cast<benchmark_stats::duration>(clock::now() - started));
            return action;
        }

        template <Context CtxT, typename... TagTs>
            requires(sizeof...(TagTs) == 1 && (std::same_as<std::remove_cvref_t<TagTs>, special_event> && ...))
        context_action operator()(CtxT& ctx, TagTs... tags) noexcept {
            // Only forward non-lifecycle special_events (start, load_event, next_event are handled above)
            return invoke_mods(ctx, funcs, tags...);
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

        [[nodiscard]] benchmark_stats result() const noexcept {
            return counter.result();
        }

        void clear() noexcept {
            counter.clear();
        }
    };

    export struct [[nodiscard]] basic_benchmark_factory {
        template <Context CtxT>
        [[nodiscard]] consteval auto operator[](CtxT const& ctx) const noexcept {
            return std::apply(
              []<typename... ModT>(ModT const&... mods) constexpr noexcept {
                  return basic_benchmark<std::remove_cvref_t<ModT>...>{mods...};
              },
              ctx.get_mods());
        }
    };

    namespace benchmark_detail {
        template <typename ModT, typename FnT>
        void for_each_benchmark(ModT& mod, FnT& fn) noexcept {
            if constexpr (requires { mod.result(); }) {
                fn(mod.result());
            }
            if constexpr (requires { mod.sub_mods(); }) {
                std::apply(
                  [&](auto&... sub) noexcept {
                      (for_each_benchmark(sub, fn), ...);
                  },
                  mod.sub_mods());
            }
        }
    } // namespace benchmark_detail

    export template <typename SinkT>
    struct [[nodiscard]] basic_benchmark_result : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        [[no_unique_address]] SinkT sink;

      public:
        explicit constexpr basic_benchmark_result(SinkT inp_sink) noexcept : sink{std::move(inp_sink)} {}

        void operator()(Context auto& ctx) noexcept {
            auto emit = [&](benchmark_stats const& stats) noexcept {
                sink("benchmark: calls={} total={}ns average={}ns min={}ns max={}ns",
                     stats.calls,
                     stats.total.count(),
                     stats.average().count(),
                     stats.calls == 0 ? 0 : stats.min.count(),
                     stats.max.count());
            };
            std::apply(
              [&](auto&... mod) noexcept {
                  (benchmark_detail::for_each_benchmark(mod, emit), ...);
              },
              ctx.get_mods());
        }
    };

    export struct [[nodiscard]] basic_benchmark_result_factory {
        template <typename SinkT>
        [[nodiscard]] consteval auto operator[](SinkT sink) const noexcept {
            return basic_benchmark_result<std::remove_cvref_t<SinkT>>{std::move(sink)};
        }
    };

    export constexpr basic_benchmark_factory        benchmark;
    export constexpr basic_benchmark_result_factory benchmark_result;

    static_assert(Modifier<basic_benchmark<>>);

} // namespace fs8
