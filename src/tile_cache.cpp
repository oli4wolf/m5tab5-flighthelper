#include "tile_cache.h"
#include <Arduino.h> // For ESP_LOGI, ESP_LOGE

TileCache::TileCache(size_t capacity) : _capacity(capacity) {}

TileCache::~TileCache() {
    clear();
}

M5Canvas* TileCache::get(const std::string& key) {
    auto it = _cacheMap.find(key);
    if (it == _cacheMap.end()) {
        return nullptr; // Not found
    }

    // Move the accessed item to the front of the LRU list
    _lruList.remove(key);
    _lruList.push_front(key);
    return it->second;
}

void TileCache::put(const std::string& key, M5Canvas* canvas) {
    // If already exists, update and move to front
    auto it = _cacheMap.find(key);
    if (it != _cacheMap.end()) {
        _lruList.remove(key);
        _lruList.push_front(key);
        // Delete old canvas to prevent memory leak
        delete it->second; 
        it->second = canvas;
        return;
    }

    // If cache is full, remove the least recently used item
    if (_cacheMap.size() >= _capacity) {
        std::string lruKey = _lruList.back();
        _lruList.pop_back();
        delete _cacheMap[lruKey]; // Free the canvas memory
        _cacheMap.erase(lruKey);
        ESP_LOGI("TileCache", "Evicted tile from cache: %s", lruKey.c_str());
    }

    // Add new item
    _lruList.push_front(key);
    _cacheMap[key] = canvas;
    ESP_LOGI("TileCache", "Added tile to cache: %s", key.c_str());
}

void TileCache::clear() {
    for (auto const& [key, val] : _cacheMap) {
        delete val; // Free canvas memory
    }
    _cacheMap.clear();
    _lruList.clear();
    ESP_LOGI("TileCache", "Cache cleared.");
}