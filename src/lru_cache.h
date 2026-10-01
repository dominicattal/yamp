#ifndef LRU_CACHE
#define LRU_CACHE

#include <cassert>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <list>

template<typename val_t>
class LruCache;

template<typename val_t>
class LruCacheRef;

template<typename val_t>
void reset_callback(LruCache<val_t>* cache, LruCacheRef<val_t>* cache_ref);

template<typename val_t>
class LruCacheRef
{
public:
    LruCacheRef()
        : m_lru_cache{}, m_key{}, m_ptr{}
    {
    }

    LruCacheRef(std::nullptr_t)
        : m_lru_cache{}, m_key{}, m_ptr{}
    {
    }

    LruCacheRef(const LruCacheRef& other) = delete;

    LruCacheRef& operator=(const LruCacheRef& other) = delete;

    LruCacheRef(LruCache<val_t>* weak_cache, int key, val_t* ptr)
        : m_lru_cache{weak_cache}, m_key{key}, m_ptr{ptr}
    {
    }

    LruCacheRef(LruCacheRef&& other)
        : m_lru_cache{other.m_lru_cache}, m_key{other.m_key}, m_ptr{std::move(other.m_ptr)}
    {
        other.m_lru_cache = nullptr;
        other.m_key = 0;
        other.m_ptr = nullptr;
    }

    LruCacheRef& operator=(LruCacheRef&& other)
    {
        if (this == &other)
            return *this;

        m_lru_cache = other.m_lru_cache;
        m_key = other.m_key;
        m_ptr = std::move(other.m_ptr);
        other.m_lru_cache = nullptr;
        other.m_key = 0;
        return *this;
    }

    LruCacheRef& operator=(std::nullptr_t)
    {
        release();

        m_lru_cache = nullptr;
        m_key = 0;
        m_ptr = nullptr;
        return *this;
    }

    operator bool() { return m_ptr != nullptr; }
    operator val_t*() { return m_ptr; }

    bool operator==(std::nullptr_t)
    {
        return m_lru_cache == nullptr;
    }

    val_t* get()
    {
        return m_ptr;
    }

    val_t& operator*()
    {
        return *m_ptr;
    }

    val_t* operator->()
    {
        return m_ptr;
    }

    const val_t* operator->() const
    {
        return m_ptr;
    }

    void release()
    {
        if (m_lru_cache)
            reset_callback(m_lru_cache, this);
        m_ptr = nullptr;
        m_lru_cache = nullptr;
    }
    
    ~LruCacheRef()
    {
        release();
    }

private:

    LruCache<val_t>* m_lru_cache;
    int m_key;
    val_t* m_ptr;

    friend void reset_callback<val_t>(LruCache<val_t>* cache, LruCacheRef<val_t>* cache_ref);
};

template<typename val_t>
class LruCache
{
    struct LruCacheEntry
    {
        int ref_count;
        int key;
        val_t val;
    };
public:
    using DestructorCallback = std::function<void(val_t*)>;
    using Iterator = typename std::list<LruCacheEntry>::iterator;

    LruCache()
        : m_max_size{256}, m_mutex{}, m_list{}, m_destructor_callback{}, m_map{}
    {
    }
    LruCacheRef<val_t> get(int key)
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        if (auto it = m_map.find(key); it != m_map.end())
        {
            ++it->second->ref_count;
            m_list.splice(m_list.begin(), m_list, it->second);
            return LruCacheRef<val_t>{this, key, std::addressof(it->second->val)};
        }
        return nullptr;
    }
    LruCacheRef<val_t> put(int key, val_t&& val)
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        assert(m_map.find(key) == m_map.end());
        m_list.emplace_front(1, key, std::move(val));
        m_map[key] = m_list.begin();
        if (m_list.size() > m_max_size)
        {
            if (auto it = std::prev(m_list.end()); it->ref_count == 0)
            {
                m_map.erase(m_map.find(it->key));
                m_list.erase(it);
            }
        }
        return LruCacheRef<val_t>{this, key, std::addressof(m_list.front().val)};
    }
    void set_destructor_callback(DestructorCallback callback)
    {
        m_destructor_callback = callback;
    }
private:
    size_t m_max_size;
    std::mutex m_mutex;
    std::list<LruCacheEntry> m_list;
    DestructorCallback m_destructor_callback;
    std::unordered_map<int, Iterator> m_map;

    friend void reset_callback<val_t>(LruCache<val_t>* cache, LruCacheRef<val_t>* cache_ref);
};

template<typename val_t>
void reset_callback(LruCache<val_t>* cache, LruCacheRef<val_t>* cache_ref)
{
    std::lock_guard<std::mutex> lock{cache->m_mutex};
    auto it = cache->m_map.find(cache_ref->m_key);
    assert(it != cache->m_map.end());
    assert(it->second->ref_count > 0);
    if (--it->second->ref_count == 0)
    {
        if (cache->m_list.size() > cache->m_max_size)
        {
            if (cache->m_destructor_callback)
                cache->m_destructor_callback(cache_ref->m_ptr);
            cache->m_list.erase(it->second);
            cache->m_map.erase(it);
        }
        else
        {
            cache->m_list.splice(cache->m_list.end(), cache->m_list, it->second);
        }
    }
}

#endif
