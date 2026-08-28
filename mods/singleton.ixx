// Created by moisrex on 8/20/26.

module;
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <tuple>
#include <typeinfo>
export module fs8.mods:singleton;
import fs8.context;
import fs8.traits;
import fs8.hash;
import :intercept;

namespace fs8 {

    // FNV-1a 64-bit constants (re-declared here because fs8.hash does not
    // export its internal constants).
    constexpr std::uint64_t SINGLETON_HASH_INIT  = 0xCBF2'9CE4'8422'2325ULL;
    constexpr std::uint64_t SINGLETON_HASH_PRIME = 0x100'0000'01B3ULL;

    // ---------------------------------------------------------------------------
    // Hash strategies
    // ---------------------------------------------------------------------------

    /// Hash a user-provided string name (must outlive the pipeline, e.g. a
    /// string literal).
    export struct [[nodiscard]] named_solution {
        std::string_view name;

        [[nodiscard]] constexpr std::uint64_t operator()(auto &) const noexcept {
            std::uint64_t hash = SINGLETON_HASH_INIT;
            for (auto const cur_ch : name) {
                hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(cur_ch));
                hash *= SINGLETON_HASH_PRIME;
            }
            return hash;
        }
    };

    /// Hash the executable name read from /proc/self/exe (default).
    export struct [[nodiscard]] exe_hash_solution {
        [[nodiscard]] std::uint64_t operator()(auto &) const noexcept {
            std::uint64_t hash = SINGLETON_HASH_INIT;
            try {
                auto const path = std::filesystem::canonical("/proc/self/exe");
                auto const name = path.filename().string();
                for (auto const cur_ch : name) {
                    hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(cur_ch));
                    hash *= SINGLETON_HASH_PRIME;
                }
            } catch (...) {
                // fallback: hash a fixed string so the pipeline can still run
                for (auto const cur_ch : std::string_view{"foresight"}) {
                    hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(cur_ch));
                    hash *= SINGLETON_HASH_PRIME;
                }
            }
            return hash;
        }
    };

    /// Hash the pipeline's mod types via their `typeid().name()`.
    export struct [[nodiscard]] pipeline_hash_solution {
        [[nodiscard]] constexpr std::uint64_t operator()(Context auto &ctx) const noexcept {
            std::uint64_t hash = SINGLETON_HASH_INIT;
            std::apply(
              [&]<typename... ModT>(ModT &...) noexcept {
                  (hash_type<ModT>(hash), ...);
              },
              ctx.get_mods());
            return hash;
        }

      private:
        template <typename Mod>
        static constexpr void hash_type(std::uint64_t &hash) noexcept {
            auto const *name = typeid(Mod).name();
            for (auto sv = std::string_view{name}; !sv.empty(); sv.remove_prefix(1)) {
                hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(sv.front()));
                hash *= SINGLETON_HASH_PRIME;
            }
        }
    };

    /// Hash the intercept mod's device queries.  Requires `intercept` in the
    /// pipeline; returns 0 otherwise.
    export struct [[nodiscard]] intercept_hash_solution {
        [[nodiscard]] constexpr std::uint64_t operator()(Context auto &ctx) const noexcept {
            if constexpr (has_mod<basic_interceptor, decltype(ctx)>) {
                auto         &intr = ctx.template mod<basic_interceptor>();
                auto          qs   = intr.queries();
                std::uint64_t hash = SINGLETON_HASH_INIT;
                for (auto const &q : qs) {
                    hash_str(hash, q.key);
                    hash_str(hash, q.value);
                    hash ^= static_cast<std::uint64_t>(q.target);
                    hash *= SINGLETON_HASH_PRIME;
                }
                return hash;
            } else {
                return 0;
            }
        }

      private:
        static constexpr void hash_str(std::uint64_t &hash, std::string_view s) noexcept {
            for (auto c : s) {
                hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
                hash *= SINGLETON_HASH_PRIME;
            }
        }
    };

    // ---------------------------------------------------------------------------
    // The singleton mod
    // ---------------------------------------------------------------------------

    /**
     * Prevent multiple instances of the same pipeline from running concurrently.
     *
     * On `start` the mod acquires an exclusive `flock(2)` on a lock file
     * whose path is derived from a hash of the *solution*.  If another
     * process already holds the lock, the pipeline exits immediately.
     *
     * Two lock locations are tried: the system-wide `/run/lock/foresight`
     * and the user-wide `$XDG_RUNTIME_DIR/foresight`.  Both are attempted
     * so that a root process and a user process prevent each other from
     * running duplicates.  If both locks succeed, the user-wide one is
     * released immediately in favour of the system-wide one.
     *
     * Usage:
     *   singleton                              // hash /proc/self/exe basename
     *   singleton["pen2mice"]                  // hash a user-provided string
     *   singleton[pipeline_hash]               // hash all mod types in the pipeline
     *   singleton[intercept_hash]              // hash intercept's device queries
     *   singleton[my_solution]                 // custom callable (uint64_t(auto&))
     */
    export template <typename Solution = exe_hash_solution>
    struct [[nodiscard]] basic_singleton : consteval_copyable {
        using consteval_copyable::consteval_copyable;

      private:
        Solution solution;
        int      lock_fd = -1;

      public:
        constexpr basic_singleton() noexcept = default;

        consteval explicit basic_singleton(Solution inp) noexcept : solution{inp} {}

        /// Variable introspection: report no pipeline variables.
        [[nodiscard]] consteval std::array<std::string_view, 0> operator[](get_variables_tag) const noexcept {
            return {};
        }

        /// Compile-time factory: `singleton["name"]` or `singleton[my_solution]`.
        template <typename Arg>
            requires(!std::same_as<std::remove_cvref_t<Arg>, get_variables_tag>)
        consteval basic_singleton operator[](Arg const &arg) const noexcept {
            auto copy = *this;
            if constexpr (requires { std::string_view{arg}; }) {
                copy.solution = named_solution{std::string_view{arg}};
            } else {
                copy.solution = Arg{arg};
            }
            return copy;
        }

        // -- dispatch ----------------------------------------------------------

        /// Normal events: always pass through.
        context_action operator()() const noexcept {
            return context_action::next;
        }

        /// Start: try to acquire the exclusive lock.
        template <Context CtxT>
        context_action operator()(CtxT &ctx, start_tag) noexcept {
            if (lock_fd >= 0) {
                return context_action::next;
            }
            auto const h = solution(ctx);
            return try_acquire_lock(h);
        }

      private:
        context_action try_acquire_lock(std::uint64_t hash) noexcept;
    };

    /// Default singleton: hash the executable name.
    export constexpr basic_singleton<> singleton;

    /// Singleton keyed on the pipeline's mod types.
    export constexpr basic_singleton pipeline_singleton{pipeline_hash_solution{}};

    /// Singleton keyed on the intercept queries.
    export constexpr basic_singleton intercept_singleton{intercept_hash_solution{}};

} // namespace fs8
