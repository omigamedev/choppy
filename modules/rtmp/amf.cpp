#include "amf.h"

double amf::Message::read_number() noexcept
{
    double value = from_big_endian<double>(std::span(m_data.begin() + offset, sizeof(double)));
    offset += sizeof(double);
    return value;
}

void amf::Message::write_number(const double value) noexcept
{
    m_data.push_back(static_cast<uint8_t>(Type::Number));
    m_data.append_range(to_big_endian(value));
}

bool amf::Message::read_bool() noexcept
{
    return m_data[offset++];
}

void amf::Message::write_bool(const bool value) noexcept
{
    m_data.push_back(static_cast<uint8_t>(Type::Boolean));
    m_data.push_back(value ? 0x01 : 0x00);
}

std::string amf::Message::read_string() noexcept
{
    uint16_t len = from_big_endian<uint16_t>(std::span(m_data.begin() + offset, sizeof(uint16_t)));
    const char* ptr = reinterpret_cast<const char*>(m_data.data()) + offset + sizeof(uint16_t);
    return { ptr, ptr + len };
}

void amf::Message::write_string(const std::string_view str) noexcept
{
    m_data.push_back(static_cast<uint8_t>(Type::String));
    m_data.append_range(to_big_endian(static_cast<uint16_t>(str.size())));
    m_data.append_range(str);
    //std::println("write amf string {}", str);
}

void amf::Message::write_object(
        const std::vector<std::pair<std::string, std::string>> &properties) noexcept
{
    m_data.push_back(static_cast<uint8_t>(Type::ObjectStart));
    for (auto& [key, value] : properties)
    {
        m_data.append_range(to_big_endian(static_cast<uint16_t>(key.size())));
        m_data.append_range(key);
        m_data.push_back(static_cast<uint8_t>(Type::String));
        m_data.append_range(to_big_endian(static_cast<uint16_t>(value.size())));
        m_data.append_range(value);
    }
    m_data.insert(m_data.end(), { 0, 0, static_cast<uint8_t>(Type::ObjectEnd) });
}

void amf::Message::write_null() noexcept
{
    m_data.push_back(0x05);
}

std::optional<amf::Message::AMFValue> amf::Message::read()
{
    while (offset < m_data.size())
    {
        Type type = Type::Null;
        if (object_state.empty())
        {
            type = static_cast<Type>(m_data[offset++]);
        }
        else if (object_state.back() == 1)
        {
            type = Type::String;
            object_state.back() = 2;
        }
        else if (object_state.back() == 2)
        {
            type = static_cast<Type>(m_data[offset++]);
            object_state.back() = 1;
        }
        switch (type)
        {
            case Type::Number: return read_number();
            case Type::Boolean: return read_bool();
            case Type::String: return read_string();
            case Type::ObjectStart:
                object_state.push_back(1);
                return "ObjectStart";
            case Type::Null: return nullptr;
            case Type::ECMAArray:
                object_state.push_back(1);
                offset += 4;
                return "ECMAArray";
            case Type::ObjectEnd:
                object_state.pop_back();
                return "ObjectEnd";
            default:
                return "UNKNOWN";
        }
    }
    return std::nullopt;
}
