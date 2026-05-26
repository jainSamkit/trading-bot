#pragma once
#include <cstdint>
#include <ostream>

enum class Tick : int32_t {};

constexpr Tick operator+(Tick a, Tick b) noexcept {
    return Tick{static_cast<int32_t>(a) + static_cast<int32_t>(b)};
}
constexpr Tick operator-(Tick a, Tick b) noexcept {
    return Tick{static_cast<int32_t>(a) - static_cast<int32_t>(b)};
}
constexpr Tick& operator+=(Tick& a, Tick b) noexcept { return a = a + b; }
constexpr Tick& operator-=(Tick& a, Tick b) noexcept { return a = a - b; }

constexpr Tick operator-(Tick a) noexcept {
    return Tick{-static_cast<int32_t>(a)};
}

constexpr Tick operator*(Tick a, int32_t k) noexcept {
    return Tick{static_cast<int32_t>(a) * k};
}
constexpr Tick operator*(int32_t k, Tick a) noexcept { return a * k; }

constexpr int32_t operator/(Tick a, Tick b) noexcept {
    return static_cast<int32_t>(a) / static_cast<int32_t>(b);
}

constexpr Tick abs(Tick a) noexcept { return a < Tick{0} ? -a : a;} 

constexpr int32_t to_int(Tick a) noexcept { return static_cast<int32_t>(a); }

inline std::ostream& operator<<(std::ostream& os, Tick a) {
    return os << static_cast<int32_t>(a);
}
