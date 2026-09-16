#ifndef WEAK_CACHE_H
#define WEAK_CACHE_H

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

template<class val_t>
class WeakCache;

template<class val_t>
void reset_callback(WeakCache<val_t>* weak_cache, int key);

template<class val_t>
class WeakCacheRef
{
public:
    WeakCacheRef(WeakCache<val_t>* weak_cache, int key, std::shared_ptr<val_t> ptr)
        : m_weak_cache{weak_cache}, m_key{key}, m_ptr{std::move(ptr)}
    {
    }
    WeakCacheRef(const WeakCacheRef& other)
        : m_weak_cache{other.m_weak_cache}, m_key{other.m_key}, m_ptr{other.m_ptr}
    {
    }
    WeakCacheRef(WeakCacheRef&& other)
        : m_weak_cache{other.m_weak_cache}, m_key{other.m_key}, m_ptr{std::move(other.m_ptr)}
    {
        other.m_weak_cache = nullptr;
        other.m_key = 0;
    }
    WeakCacheRef& operator=(const WeakCacheRef& other)
    {
        if (this == &other)
            return *this;

        if (m_key != other.m_key || m_weak_cache != other.m_weak_cache)
            release();

        m_weak_cache = other.m_weak_cache;
        m_key = other.m_key;
        m_ptr = other.m_ptr;
        return *this;
    }
    WeakCacheRef& operator=(WeakCacheRef&& other)
    {
        if (this == &other)
            return *this;

        if (m_key != other.m_key || m_weak_cache != other.m_weak_cache)
            release();

        m_weak_cache = other.m_weak_cache;
        m_key = other.m_key;
        m_ptr = std::move(other.m_ptr);
        other.m_weak_cache = nullptr;
        other.m_key = 0;
        return *this;
    }
    val_t& operator->()
    {
        return *m_ptr;
    }
    ~WeakCacheRef()
    {
        release();
    }

private:
    void release()
    {
        m_ptr.reset();
        reset_callback(m_weak_cache, m_key);
    }
    WeakCache<val_t>* m_weak_cache;
    int m_key;
    std::shared_ptr<val_t> m_ptr;
};

template<class val_t>
class WeakCache
{
    using Callback = std::function<std::shared_ptr<val_t>(int)>;
public:
    WeakCache(Callback callback)
        : m_callback{callback}
    {
    }
    WeakCacheRef<val_t> get(int key)
    {
        if (auto it = m_map.find(key); it != m_map.end())
            if (auto ptr = it->second.lock())
                return WeakCacheRef<val_t>{this, key, std::move(ptr)};

        std::shared_ptr<val_t> ptr = m_callback(key);
        m_map[key] = ptr;
        return WeakCacheRef<val_t>{this, key, std::move(ptr)};
    }

private:
    Callback m_callback;
    std::unordered_map<int, std::weak_ptr<val_t>> m_map;

    friend void reset_callback<val_t>(WeakCache<val_t>* weak_cache, int key);
};


template<class val_t>
void reset_callback(WeakCache<val_t>* weak_cache, int key)
{
    auto it = weak_cache->m_map.find(key);
    if (it != weak_cache->m_map.end() && it->second.expired())
        weak_cache->m_map.erase(it);
}

#endif
