#pragma once

#include <queue>
#include <ranges>
#include <unordered_map>

namespace PathTracing
{

template<typename T> struct FNVHash
{
    static constexpr size_t Offset = 0xcbf29ce484222325ull;
    static constexpr size_t Prime = 0x100000001b3ull;

    constexpr size_t operator()(const T &config) const noexcept;
};

template<typename T> inline constexpr size_t FNVHash<T>::operator()(const T &config) const noexcept
{
    auto data = std::span<const std::byte>(reinterpret_cast<const std::byte *>(&config), sizeof(T));

    size_t hash = Offset;
    for (std::byte byte : data)
    {
        hash ^= std::to_integer<size_t>(byte);
        hash *= Prime;
    }

    return hash;
}

template<typename K, typename V, size_t MaxSize> class LRUCache
{
public:
    [[nodiscard]] V Insert(const K &key, const V &value);
    [[nodiscard]] V Insert(const K &key, const V &&value);
    [[nodiscard]] bool Contains(const K &key);
    [[nodiscard]] const V &Get(const K &key);

    [[nodiscard]] auto GetKeys();
    [[nodiscard]] auto GetValues();

    void Clear();

private:
    std::unordered_map<K, V> m_Cache;
    std::queue<K> m_LRUQueue;

private:
    V MakeSpace();
};

template<typename K, typename V, size_t MaxSize>
inline V LRUCache<K, V, MaxSize>::Insert(const K &key, const V &value)
{
    V removed = MakeSpace();

    m_LRUQueue.push(key);
    m_Cache[key] = value;

    return removed;
}

template<typename K, typename V, size_t MaxSize>
inline V LRUCache<K, V, MaxSize>::Insert(const K &key, const V &&value)
{
    V removed = MakeSpace();

    m_LRUQueue.push(key);
    m_Cache[key] = std::move(value);

    return removed;
}

template<typename K, typename V, size_t MaxSize> inline bool LRUCache<K, V, MaxSize>::Contains(const K &key)
{
    return m_Cache.contains(key);
}

template<typename K, typename V, size_t MaxSize> inline const V &LRUCache<K, V, MaxSize>::Get(const K &key)
{
    assert(m_Cache.contains(key));
    return m_Cache[key];
}

template<typename K, typename V, size_t MaxSize> inline auto LRUCache<K, V, MaxSize>::GetKeys()
{
    return m_Cache | std::views::keys;
}

template<typename K, typename V, size_t MaxSize> inline auto LRUCache<K, V, MaxSize>::GetValues()
{
    return m_Cache | std::views::values;
}

template<typename K, typename V, size_t MaxSize> inline void LRUCache<K, V, MaxSize>::Clear()
{
    std::queue<K> q;
    m_LRUQueue.swap(q);
    m_Cache.clear();
}

template<typename K, typename V, size_t MaxSize> inline V LRUCache<K, V, MaxSize>::MakeSpace()
{
    if (m_LRUQueue.size() < MaxSize)
        return {};

    K key = m_LRUQueue.front();
    m_LRUQueue.pop();
    V removed = m_Cache[key];
    m_Cache.erase(key);

    return removed;
}

}
