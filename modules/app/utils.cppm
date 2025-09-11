module;
#include <cstdint>
#include <algorithm>
#include <ranges>
#include <vector>
#include <cstddef>
#include <numeric>
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
template <
    std::ranges::random_access_range R,
    typename Comp = std::ranges::less,
    typename Proj = std::identity
>
auto sorted_view(R& r, Comp comp = {}, Proj proj = {})
{
   using size_type = std::size_t;

    // Build and sort indices
    std::vector<size_type> idx(std::ranges::size(r));
    std::iota(idx.begin(), idx.end(), size_type{0});

    std::ranges::stable_sort(idx,
        [&](size_type i, size_type j)
        {
            using std::ranges::begin;
            return std::invoke(comp,
                std::invoke(proj, *(begin(r) + i)),
                std::invoke(proj, *(begin(r) + j)));
        });

    // Keep indices alive inside the returned view
    auto ownedIdx = std::ranges::owning_view{std::move(idx)};

    using Range = std::remove_reference_t<R>;
    Range* ptr = std::addressof(r); // r must outlive the returned view

    // Return a lazy view of elements in sorted order
    return std::move(ownedIdx)
        | std::views::transform([ptr](size_type i) -> decltype(auto)
        {
            using std::ranges::begin;
            return *(begin(*ptr) + i);
        });
    }
}
