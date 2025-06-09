// Created by moisrex on 6/8/25.

module;
#include <concepts>
#include <cstdint>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
export module foresight.mods.context;
import foresight.mods.event;

export namespace foresight {
    template <typename... Funcs>
    struct basic_context;

    template <typename T>
    concept Context = requires(T ctx) { ctx.event(); };

    template <typename T>
    concept modifier = std::copyable<std::remove_cvref_t<T>>;

    enum struct context_action : std::uint8_t {
        next,
        ignore_event,
        exit,
    };

    template <typename ModT, typename CtxT>
    [[nodiscard]] constexpr context_action invoke_mod(ModT &mod, CtxT &ctx) {
        using enum context_action;
        using result = std::invoke_result_t<ModT, CtxT &>;
        if constexpr (std::same_as<result, bool>) {
            return mod(ctx) ? next : ignore_event;
        } else if constexpr (std::same_as<result, context_action>) {
            return mod(ctx);
        } else {
            mod(ctx);
            return next;
        }
    }

    template <typename... Funcs>
    struct [[nodiscard]] basic_context {
        constexpr basic_context() noexcept = default;

        template <typename... InpFuncs>
        constexpr explicit basic_context(event_type inp_ev, InpFuncs &&...inp_funcs) noexcept
          : ev{std::move(inp_ev)},
            funcs{std::forward<InpFuncs>(inp_funcs)...} {}

        constexpr basic_context(basic_context const &inp_ctx)                = default;
        constexpr basic_context(basic_context &&inp_ctx) noexcept            = default;
        constexpr basic_context &operator=(basic_context &&inp_ctx) noexcept = default;
        constexpr basic_context &operator=(basic_context const &inp_ctx)     = default;
        constexpr ~basic_context() noexcept                                  = default;

        [[nodiscard]] constexpr event_type const &event() const noexcept {
            return ev;
        }

        [[nodiscard]] constexpr event_type &event() noexcept {
            return ev;
        }

        template <modifier Mod>
        [[nodiscard]] consteval basic_context<Funcs..., Mod> operator|(Mod &&mod) const noexcept {
            return std::apply(
              [this, &mod]<typename... Fs>(Fs &&...the_funcs) {
                  return basic_context<Funcs..., Mod>{ev, std::forward<Fs>(the_funcs)..., mod};
              },
              funcs);
        }

        constexpr decltype(auto) operator()() {
            using enum context_action;
            return std::apply(
              [this](auto &...mods) {
                  for (;;) {
                      auto action = next;
                      (((action = invoke_mod(mods, *this)), action == next) && ...);
                      if (action == exit) {
                          break;
                      }
                  }
              },
              funcs);
        }

      private:
        event_type           ev;
        std::tuple<Funcs...> funcs;
    };

    constexpr basic_context<> context;

    [[nodiscard]] constexpr event_type::type_type type(Context auto const &ctx) noexcept {
        return ctx.event().type();
    }

    [[nodiscard]] constexpr event_type::code_type code(Context auto const &ctx) noexcept {
        return ctx.event().code();
    }

    [[nodiscard]] constexpr event_type::value_type value(Context auto const &ctx) noexcept {
        return ctx.event().value();
    }


} // namespace foresight
