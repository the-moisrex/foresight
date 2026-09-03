// Created by moisrex on 8/22/26.

module;
#include <chrono>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <utility>
export module fs8.mods:benchmark;
import fs8.context;
import fs8.traits;

namespace fs8 {

    export struct [[nodiscard]] basic_benchmark_counter {
        using duration = std::chrono::nanoseconds;

        std::uint64_t calls = 0;
        duration      total{};
        duration      min{duration::max()};
        duration      max{};

        [[nodiscard]] constexpr duration average() const noexcept {
            return calls == 0 ? duration{} : total / static_cast<std::int64_t>(calls);
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
        }

        constexpr void clear() noexcept {
            *this = {};
        }
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
        std::string_view        name;
        mods_type               funcs{};
        basic_benchmark_counter counter{};

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
            counter.record(std::chrono::duration_cast<basic_benchmark_counter::duration>(clock::now() - started));
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

        [[nodiscard]] basic_benchmark_counter const& result() const noexcept {
            return counter;
        }

        void clear() noexcept {
            counter.clear();
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
    } // namespace benchmark_detail

    export template <typename SinkT>
    struct [[nodiscard]] basic_benchmark_result : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        [[no_unique_address]] SinkT sink;
        bool                        clear_after = false;

      public:
        constexpr basic_benchmark_result() noexcept = default;

        explicit constexpr basic_benchmark_result(SinkT inp_sink) noexcept : sink{std::move(inp_sink)} {}

        constexpr basic_benchmark_result(SinkT inp_sink, bool inp_clear) noexcept : sink{std::move(inp_sink)}, clear_after{inp_clear} {}

        /// Handle lifecycle events (toggle_on / toggle_off) transparently.
        context_action operator()(special_event const& tag) noexcept {
            using enum context_action;
            return is_lifecycle_event(tag) ? next : drop_event;
        }

        context_action operator()(Context auto& ctx) noexcept {
            using enum context_action;
            auto visit = [&](auto& mod) noexcept {
                if constexpr (requires { mod.result(); }) {
                    auto const& counter = mod.result();
                    auto const  bname   = mod.get_name();
                    sink("{}: calls={} total={}ns average={}ns min={}ns max={}ns",
                         bname.empty() ? "benchmark" : bname,
                         counter.calls,
                         counter.total.count(),
                         counter.average().count(),
                         counter.calls == 0 ? 0 : counter.min.count(),
                         counter.max.count());
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

        /// benchmark_result[sink] / benchmark_result[sink, true]: create a result reporter.
        template <typename InpSinkT>
        [[nodiscard]] consteval auto operator[](InpSinkT inp_sink, bool clear = false) const noexcept {
            return basic_benchmark_result<std::remove_cvref_t<InpSinkT>>{std::move(inp_sink), clear};
        }
    };

    export constexpr basic_benchmark<>                       benchmark;
    export constexpr benchmark_detail::benchmark_all_factory benchmark_all;
    export basic_benchmark_result<nullptr_t>                 benchmark_result;

    static_assert(Modifier<basic_benchmark<>>);

} // namespace fs8
