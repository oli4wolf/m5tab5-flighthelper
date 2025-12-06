#ifndef TILE_CACHE_H
#define TILE_CACHE_H

#include <M5Unified.h>
#include <list>
#include <unordered_map>
#include <string>

// Define the maximum number of tiles to cache
// This will depend on available memory and TILE_SIZE
// For example, 10 tiles of 256x256 pixels with 16-bit color depth:
// 10 * 256 * 256 * 2 bytes = 1.3MB. Adjust as needed.
#define MAX_TILE_CACHE_SIZE 10

class TileCache {
public:
    TileCache(size_t capacity);
    ~TileCache();

    M5Canvas* get(const std::string& key);
    void put(const std::string& key, M5Canvas* canvas);
    void clear();

private:
    size_t _capacity;
    std::list<std::string> _lruList; // Stores keys in LRU order
    std::unordered_map<std::string, M5Canvas*> _cacheMap; // Stores actual canvases
};

#endif // TILE_CACHE_H