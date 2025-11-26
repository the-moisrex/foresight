module;
#include <cmath>
#include <concepts>
#include <numbers>
export module foresight.utils.easings;

export namespace fs8 {


    // Concept to constrain T to floating-point types
    template <typename T>
    concept floating_point = std::floating_point<T>; // Constrains to float, double, long double [[1]]

    /**
     * @brief Linear easing: no acceleration.
     * @param t Normalized time (0 to 1)
     * @return t
     *
     * Simplest easing: output equals input. Constant speed.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T linear(T t) noexcept {
        return t;
    }

    /**
     * @brief Quadratic in: starts slow, accelerates.
     * @param t Normalized time
     * @return t²
     *
     * Speed increases quadratically. Good for objects gaining momentum.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeInQuad(T t) noexcept {
        return t * t;
    }

    /**
     * @brief Quadratic out: starts fast, decelerates to stop.
     * @param t Normalized time
     * @return t * (2 - t)
     *
     * Uses identity: 1 - (1-t)². Feels natural for stopping motion.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeOutQuad(T t) noexcept {
        return t * (static_cast<T>(2) - t);
    }

    /**
     * @brief Quadratic in-out: slow at start and end, fast in the middle.
     * @param t Normalized time
     * @return 2t² if t<0.5, else -1 + (4-2t)t
     *
     * Combines easeInQuad and easeOutQuad. Symmetric acceleration profile.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeInOutQuad(T t) noexcept {
        if (t < static_cast<T>(0.5)) {
            return static_cast<T>(2) * t * t;
        } else {
            return static_cast<T>(-1) + (static_cast<T>(4) - static_cast<T>(2) * t) * t;
        }
    }

    /**
     * @brief Cubic in: stronger acceleration than quad.
     * @param t Normalized time
     * @return t³
     *
     * More pronounced slow start than quad. Feels heavier.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeInCubic(T t) noexcept {
        return t * t * t;
    }

    /**
     * @brief Cubic out: strong deceleration at end.
     * @param t Normalized time
     * @return 1 - (1-t)³
     *
     * Inverse of easeInCubic. Feels like a hard stop.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeOutCubic(T t) noexcept {
        T const tmp = t - static_cast<T>(1);
        return tmp * tmp * tmp + static_cast<T>(1);
    }

    /**
     * @brief Cubic in-out: strong acceleration at edges, fast center.
     * @param t Normalized time
     * @return 4t³ if t<0.5, else 1 - 4(1-t)³
     *
     * Sharper than quad in-out. Good for dramatic transitions.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeInOutCubic(T t) noexcept {
        if (t < static_cast<T>(0.5)) {
            return static_cast<T>(4) * t * t * t;
        } else {
            T const tmp = static_cast<T>(2) * t - static_cast<T>(2);
            return static_cast<T>(0.5) * tmp * tmp * tmp + static_cast<T>(1);
        }
    }

    /**
     * @brief Quartic in: very slow start, rapid acceleration.
     * @param t Normalized time
     * @return t⁴
     *
     * Even more delayed start than cubic. Feels like a spring loading.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeInQuart(T t) noexcept {
        T const t2 = t * t;
        return t2 * t2;
    }

    /**
     * @brief Quartic out: rapid start, very slow stop.
     * @param t Normalized time
     * @return 1 - (1-t)⁴
     *
     * Inverse of easeInQuart. Feels like hitting a soft wall.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeOutQuart(T t) noexcept {
        T const tmp  = t - static_cast<T>(1);
        T const tmp2 = tmp * tmp;
        return static_cast<T>(1) - tmp2 * tmp2;
    }

    /**
     * @brief Quartic in-out: extreme at edges, fast center.
     * @param t Normalized time
     * @return 8t⁴ if t<0.5, else 1 - 8(1-t)⁴
     *
     * Strong ease-in and ease-out. Use sparingly for dramatic effect.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeInOutQuart(T t) noexcept {
        if (t < static_cast<T>(0.5)) {
            T const t2 = t * t;
            return static_cast<T>(8) * t2 * t2;
        } else {
            T const tmp  = static_cast<T>(2) * t - static_cast<T>(2);
            T const tmp2 = tmp * tmp;
            return static_cast<T>(1) - static_cast<T>(0.5) * tmp2 * tmp2;
        }
    }

    /**
     * @brief Quintic in: extremely slow start.
     * @param t Normalized time
     * @return t⁵
     *
     * Even more pronounced delay than quartic. Feels like inertia.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeInQuint(T t) noexcept {
        T const t2 = t * t;
        return t2 * t2 * t;
    }

    /**
     * @brief Quintic out: extremely slow stop.
     * @param t Normalized time
     * @return 1 - (1-t)⁵
     *
     * Inverse of easeInQuint. Feels like sinking into foam.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeOutQuint(T t) noexcept {
        T const tmp  = t - static_cast<T>(1);
        T const tmp2 = tmp * tmp;
        return static_cast<T>(1) + tmp2 * tmp2 * tmp;
    }

    /**
     * @brief Quintic in-out: extreme ease at both ends.
     * @param t Normalized time
     * @return 16t⁵ if t<0.5, else 1 + 16(1-t)⁵
     *
     * Strongest in-out easing. Use for high-impact animations.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeInOutQuint(T t) noexcept {
        if (t < static_cast<T>(0.5)) {
            T const t2 = t * t;
            return static_cast<T>(16) * t2 * t2 * t;
        } else {
            T const tmp  = static_cast<T>(2) * t - static_cast<T>(2);
            T const tmp2 = tmp * tmp;
            return static_cast<T>(1) + static_cast<T>(0.5) * tmp2 * tmp2 * tmp;
        }
    }

    /**
     * @brief Sinusoidal in: smooth acceleration like a sine wave.
     * @param t Normalized time
     * @return 1 - cos(t * π/2)
     *
     * Smooth, organic start. Feels natural and fluid.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeInSine(T t) noexcept {
        using std::numbers::pi_v;
        return static_cast<T>(1) - std::cos(t * pi_v<T> / static_cast<T>(2));
    }

    /**
     * @brief Sinusoidal out: smooth deceleration.
     * @param t Normalized time
     * @return sin(t * π/2)
     *
     * Smooth, natural stop. Often used for fading or sliding.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeOutSine(T t) noexcept {
        using std::numbers::pi_v;
        return std::sin(t * pi_v<T> / static_cast<T>(2));
    }

    /**
     * @brief Sinusoidal in-out: smooth start and stop.
     * @param t Normalized time
     * @return (1 - cos(π*t)) / 2
     *
     * Symmetric and very smooth. Feels weightless and elegant.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeInOutSine(T t) noexcept {
        using std::numbers::pi_v;
        return (static_cast<T>(1) - std::cos(pi_v<T> * t)) / static_cast<T>(2);
    }

    /**
     * @brief Exponential in: starts very slow, then sudden acceleration.
     * @param t Normalized time
     * @return 0 if t<=0, else 2^(10t - 10)
     *
     * Extremely delayed start. Use for dramatic reveals.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeInExpo(T t) noexcept {
        if (t <= static_cast<T>(0)) {
            return static_cast<T>(0);
        }
        return std::pow(static_cast<T>(2), static_cast<T>(10) * t - static_cast<T>(10));
    }

    /**
     * @brief Exponential out: starts fast, then very slow stop.
     * @param t Normalized time
     * @return 1 if t>=1, else 1 - 2^(-10t)
     *
     * Opposite of easeInExpo. Feels like energy draining away.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeOutExpo(T t) noexcept {
        if (t >= static_cast<T>(1)) {
            return static_cast<T>(1);
        }
        return static_cast<T>(1) - std::pow(static_cast<T>(2), -static_cast<T>(10) * t);
    }

    /**
     * @brief Exponential in-out: very slow at ends, explosive in middle.
     * @param t Normalized time
     * @return Piecewise exponential curve
     *
     * Most extreme in-out easing. Use for high-drama transitions.
     */
    template <floating_point T>
    [[nodiscard]] constexpr T easeInOutExpo(T t) noexcept {
        if (t <= static_cast<T>(0)) {
            return static_cast<T>(0);
        }
        if (t >= static_cast<T>(1)) {
            return static_cast<T>(1);
        }
        if (t < static_cast<T>(0.5)) {
            return std::pow(static_cast<T>(2), static_cast<T>(20) * t - static_cast<T>(10))
                   / static_cast<T>(2);
        }
        return (static_cast<T>(2) - std::pow(static_cast<T>(2), -static_cast<T>(20) * t + static_cast<T>(10)))
               / static_cast<T>(2);
    }

} // namespace fs8
