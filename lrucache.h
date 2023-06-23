#ifndef LRUCACHE_H
#define LRUCACHE_H
#include <list>
#include <mutex>
#include <unordered_map>
#include <utility>

template <typename Key, typename Value>
class LRUCache {
   public:
    LRUCache(int capacity) : cap(capacity) {}
    LRUCache() : LRUCache(4096) {}

    bool safe_get(const Key &key, Value *&v) {
        bool r{false};
        {
            std::lock_guard<std::mutex> l(this->m_);
            r = this->get(key, v);
        }
        return r;
    }

    void safe_put(const Key &key, Value *value) {
        std::lock_guard<std::mutex> l(this->m_);
        this->put(key, value);
    }

    bool get(const Key &key, Value *&v) {
        auto iter = this->cache.find(key);
        if (iter == cache.end()) {
            return false;
        }

        auto value = iter->second->second;
        list.erase(iter->second);
        list.insert(list.begin(), {key, value});
        cache[key] = list.begin();
        v = value;
        return true;
    }

    void put(const Key &key, Value *value) {
        auto iter = this->cache.find(key);
        if (iter == cache.end()) {
            this->list.insert(list.begin(), {key, value});
            this->cache[key] = list.begin();
            if (list.size() > cap) {
                auto last = list.back().first;
                list.pop_back();
                cache.erase(last);
            }
        } else {
            //找到了,擦除旧的，并挪动到头部
            list.erase(iter->second);
            this->list.insert(list.begin(), {key, value});
            this->cache[key] = list.begin();
        }
    }

   private:
    std::list<std::pair<Key, Value *>> list;
    std::unordered_map<Key,
                       typename std::list<std::pair<Key, Value *>>::iterator>
        cache;
    std::mutex m_;
    int cap;
};

#endif  // LRUCACHE_H
