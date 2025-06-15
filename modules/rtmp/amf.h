#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <array>
#include <ranges>
#include <condition_variable>
#include <algorithm>
#include <optional>
#include <variant>
#include <chrono>
#include <bit>

template<size_t size_to, size_t size_from>
constexpr auto trunc_be(const std::array<uint8_t, size_from>&& bits)
{
    std::array<uint8_t, size_to> subarray;
    std::ranges::copy(bits.begin() + size_from - size_to, bits.end(), subarray.begin());
    return subarray;
}

template<size_t size_to, size_t size_from>
constexpr auto pad_be(const std::array<uint8_t, size_from>& bits)
{
    std::array<uint8_t, size_to> padded{0};
    std::ranges::copy(bits.begin(), bits.end(), padded.begin() + size_to - size_from);
    return padded;
}

template<typename T, size_t bytes = sizeof(T)>
constexpr auto to_big_endian(T value) noexcept
{
    auto bits = std::bit_cast<std::array<uint8_t, sizeof(T)>>(value);
    if constexpr (std::endian::native != std::endian::big)
        std::ranges::reverse(bits);
    //if constexpr ()
    return bits;
}

template<typename T, size_t size_from>
constexpr T from_big_endian(std::array<uint8_t, size_from> bits) noexcept
{
    if constexpr (bits.size() != sizeof(T))
    {
        const auto padded = pad_be<sizeof(T)>(bits);
        return from_big_endian<T>(padded);
    }
    else
    {
        if constexpr (std::endian::native != std::endian::big)
            std::ranges::reverse(bits);
        return std::bit_cast<T>(bits);
    }
}
template<typename T>
constexpr T from_big_endian(std::span<uint8_t> data) noexcept
{
    std::array<uint8_t, sizeof(T)> bits;
    std::ranges::copy_n(data.begin(), sizeof(T), bits.begin());
    return from_big_endian<T>(bits);
}

template<typename T>
constexpr auto to_little_endian(T value) noexcept
{
    auto bits = std::bit_cast<std::array<uint8_t, sizeof(T)>>(value);
    if constexpr (std::endian::native != std::endian::little)
        std::ranges::reverse(bits);
    return bits;
}

template<size_t size_to, size_t size_from>
constexpr auto trunc_le(const std::array<uint8_t, size_from>&& bits)
{
    constexpr off_t offset = size_from - size_to;
    return std::array<uint8_t, size_to>{ bits.begin(), bits.end() - offset };
}

namespace amf
{
    class Message
    {
        std::vector<uint8_t> m_data;
        off_t offset = 0;
        std::vector<int> object_state;
    public:
        Message() = default;
        Message(const std::vector<uint8_t>& buffer) : m_data(buffer) { }
        using AMFValue = std::variant<std::nullptr_t, bool, double, std::string>;
        enum class Type : uint8_t
        {
            Number = 0x00,
            Boolean = 0x01,
            String = 0x02,
            ObjectStart = 0x03,
            Null = 0x05,
            ECMAArray = 0x08,
            ObjectEnd = 0x09,
        };
        double read_number() noexcept;
        void write_number(const double value) noexcept;
        bool read_bool() noexcept;
        void write_bool(const bool value) noexcept;
        std::string read_string() noexcept;
        void write_string(const std::string_view str) noexcept;
        void write_object(const std::vector<std::pair<std::string, std::string>>& properties) noexcept;
        void write_null() noexcept;
        const std::vector<uint8_t>& data() const noexcept { return m_data; }
        const size_t size() const noexcept { return m_data.size(); }
        std::optional<AMFValue> read();
    };
}
