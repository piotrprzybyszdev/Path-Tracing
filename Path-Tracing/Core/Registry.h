#pragma once

#include <map>

namespace PathTracing
{

template<typename Key, typename Value> struct RegistryStorage
{
    std::map<Key, Value> m_Content;
};

struct RegistryStorageEmpty
{
};

template<typename Key, typename Value, const Value &Default, bool IsEnabled>
class Registry : protected std::conditional_t<IsEnabled, RegistryStorage<Key, Value>, RegistryStorageEmpty>
{
public:
    void Set(Key key, const Value &value);
    [[nodiscard]] const Value &Get(Key key) const;
};

template<typename Key, typename Value, const Value &Default, bool IsEnabled>
inline void Registry<Key, Value, Default, IsEnabled>::Set(Key key, const Value &value)
{
    if constexpr (IsEnabled)
    {
        this->m_Content[key] = value;
    }
}

template<typename Key, typename Value, const Value &Default, bool IsEnabled>
inline const Value &Registry<Key, Value, Default, IsEnabled>::Get(Key key) const
{
    if constexpr (IsEnabled)
    {
        auto it = this->m_Content.find(key);
        if (it != this->m_Content.end())
            return it->second;
    }
    return Default;
}

}
