// Created by moisrex on 8/22/26.

module;
#include <chrono>
#include <cstdint>
#include <string_view>
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

    namespace benchmark_detail {
        namespace pretty_type_name_impl {

            constexpr std::string_view trim(std::string_view s) noexcept {
                constexpr std::string_view ws{" \t"};
                auto const                 b = s.find_first_not_of(ws);
                if (b == std::string_view::npos) {
                    return {};
                }
                auto const e = s.find_last_not_of(ws);
                return s.substr(b, e - b + 1);
            }

            /// Slice the type token, keeping nested <...> and (...).
            constexpr std::string_view type_token(std::string_view s) noexcept {
                int depth = 0;
                for (std::size_t i = 0; i < s.size(); ++i) {
                    char const c = s[i];
                    if (c == '<' || c == '(') {
                        ++depth;
                    } else if (c == '>' || c == ')') {
                        if (depth == 0) {
                            return trim(s.substr(0, i));
                        }
                        --depth;
                    } else if (depth == 0 && (c == ']' || c == ',' || c == ';')) {
                        return trim(s.substr(0, i));
                    }
                }
                return trim(s);
            }

            constexpr std::string_view extract_type(std::string_view pretty) noexcept {
                // GCC / Clang: "... [T = Type]" or "... [with T = Type; ...]"
                if (auto const p = pretty.find("T = "); p != std::string_view::npos) {
                    return type_token(pretty.substr(p + 4));
                }
                return {};
            }

            constexpr std::string_view unqualified(std::string_view name) noexcept {
                auto const args = name.find('<');
                auto const head = name.substr(0, args == std::string_view::npos ? name.size() : args);
                auto const ns   = head.rfind("::");
                return ns == std::string_view::npos ? name : name.substr(ns + 2);
            }

            constexpr std::string_view short_name(std::string_view pretty) noexcept {
                auto const type = extract_type(pretty);
                return type.empty() ? pretty : unqualified(type);
            }

        } // namespace pretty_type_name_impl

        /// Extract a short readable name from __PRETTY_FUNCTION__.
        /// Turns "...pretty_type_name() [T = fs8::basic_abs2rel]" into "basic_abs2rel".
        template <typename T>
        consteval std::string_view pretty_type_name() noexcept {
            constexpr std::string_view raw = __PRETTY_FUNCTION__;
            return pretty_type_name_impl::short_name(raw);
        }

    } // namespace benchmark_detail

    export template <Modifier... Funcs>
    struct [[nodiscard]] basic_benchmark : consteval_copyable {
        using consteval_copyable::consteval_copyable;
        using mods_type = std::tuple<std::remove_cvref_t<Funcs>...>;
        using clock     = std::chrono::steady_clock;

      private:
        std::string_view        name{};
        mods_type               funcs{};
        basic_benchmark_counter counter{};

      public:
        explicit constexpr basic_benchmark(std::remove_cvref_t<Funcs>... inp_funcs) noexcept : funcs{inp_funcs...} {}

        constexpr basic_benchmark(std::string_view inp_name, std::remove_cvref_t<Funcs>... inp_funcs) noexcept
          : name{inp_name},
            funcs{inp_funcs...} {}

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
            using enum context_action;
            // Lifecycle events (toggle_on/toggle_off) are for the `on` block, not the
            // benchmarked mods.  Forwarding them would cause inner mods (which don't
            // handle special events) to return drop_event, making the on block abort.
            return [&]<typename Tag>(Tag const& tag) noexcept -> context_action {
                if (tag.code == toggle_on.code || tag.code == toggle_off.code) {
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

    /// Wraps each mod in its own benchmark with an auto-generated name.
    /// Usage: benchmark_all[context | mod1 | mod2 | ...]
    export struct [[nodiscard]] basic_benchmark_all_factory {
        template <Context CtxT>
        [[nodiscard]] consteval auto operator[](CtxT const& ctx) const noexcept {
            return std::apply(
              []<typename... ModT>(ModT const&... mods) constexpr noexcept {
                  return (context | ... | make_benchmark<ModT>(mods));
              },
              ctx.get_mods());
        }

      private:
        template <typename ModT>
        [[nodiscard]] static consteval auto make_benchmark(ModT const& mod) noexcept {
            return basic_benchmark<ModT>{benchmark_detail::pretty_type_name<ModT>(), mod};
        }
    };

    namespace benchmark_detail {
        template <typename ModT, typename FnT>
        void for_each_benchmark(ModT& mod, FnT& fn) noexcept {
            if constexpr (requires { mod.result(); }) {
                fn(mod.get_name(), mod.result());
            }
            if constexpr (requires { mod.sub_mods(); }) {
                std::apply(
                  [&](auto&... sub) noexcept {
                      (for_each_benchmark(sub, fn), ...);
                  },
                  mod.sub_mods());
            }
        }

        template <typename ModT>
        void for_each_benchmark_clear(ModT& mod) noexcept {
            if constexpr (requires { mod.clear(); }) {
                mod.clear();
            }
            if constexpr (requires { mod.sub_mods(); }) {
                std::apply(
                  [&](auto&... sub) noexcept {
                      (for_each_benchmark_clear(sub), ...);
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
        bool                        clear_after = false;

      public:
        explicit constexpr basic_benchmark_result(SinkT inp_sink) noexcept : sink{std::move(inp_sink)} {}

        constexpr basic_benchmark_result(SinkT inp_sink, bool inp_clear) noexcept : sink{std::move(inp_sink)}, clear_after{inp_clear} {}

        /// Handle lifecycle events (toggle_on / toggle_off) transparently.
        /// The `on` block sends toggle_on when its condition first becomes true
        /// and toggle_off when it becomes false.  We must return `next` so the
        /// on block's lifecycle proceeds normally; returning drop_event would
        /// cause the on block to abort before running our actual operator().
        context_action operator()(special_event const& tag) noexcept {
            using enum context_action;
            if (tag.code == toggle_on.code || tag.code == toggle_off.code) {
                return next;
            }
            return drop_event;
        }

        context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;
            auto emit = [&](std::string_view const bname, benchmark_stats const& stats) noexcept {
                sink("{}: calls={} total={}ns average={}ns min={}ns max={}ns",
                     bname.empty() ? "benchmark" : bname,
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
            if (clear_after) {
                std::apply(
                  [&](auto&... mod) noexcept {
                      (benchmark_detail::for_each_benchmark_clear(mod), ...);
                  },
                  ctx.get_mods());
            }
            return drop_event;
        }
    };

    export struct [[nodiscard]] basic_benchmark_result_factory {
        template <typename SinkT>
        [[nodiscard]] consteval auto operator[](SinkT sink) const noexcept {
            return basic_benchmark_result<std::remove_cvref_t<SinkT>>{std::move(sink)};
        }

        template <typename SinkT>
        [[nodiscard]] consteval auto operator[](SinkT sink, bool clear) const noexcept {
            return basic_benchmark_result<std::remove_cvref_t<SinkT>>{std::move(sink), clear};
        }
    };

    export constexpr basic_benchmark_factory        benchmark;
    export constexpr basic_benchmark_all_factory    benchmark_all;
    export constexpr basic_benchmark_result_factory benchmark_result;

    static_assert(Modifier<basic_benchmark<>>);

} // namespace fs8
