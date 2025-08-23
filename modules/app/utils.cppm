module;
#include <cstdint>
export module ce.app.utils;

export namespace ce::app
{
constexpr int32_t pow(const int32_t base, const uint32_t exp)
{
    return (exp == 0) ? 1 :
           (exp % 2 == 0) ? pow(base * base, exp / 2) :
                            base * pow(base, exp - 1);
}
struct NoCopy
{
    NoCopy() = default;
    NoCopy(const NoCopy& other) = delete;
    NoCopy(NoCopy&& other) noexcept = default;
    NoCopy& operator=(const NoCopy& other) = delete;
    NoCopy& operator=(NoCopy&& other) noexcept = default;
};
}
