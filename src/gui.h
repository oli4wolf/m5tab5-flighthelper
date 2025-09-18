#ifndef GUI_H
#define GUI_H
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef __cplusplus
// C++ specific includes
#include <vector>
#include <list>
#include <map>
#include <string>
#include <freertos/semphr.h> // For SemaphoreHandle_t

extern "C" {
#endif

// C-linkage function declarations
void drawImageMatrixTask(void *pvParameters);
bool drawJpgFromSD(const char* filePath);

#ifdef __cplusplus
} // extern "C"

// C++ specific declarations
struct TileCacheEntry {
    std::string path;
    std::vector<uint8_t> data;
    int z, x, y; // Tile coordinates for debugging/logging

    TileCacheEntry(const std::string& p, const std::vector<uint8_t>& d, int tz, int tx, int ty)
        : path(p), data(d), z(tz), x(tx), y(ty) {}
};

class LRUCache {
private:
    std::list<TileCacheEntry> cacheList;
    std::map<std::string, std::list<TileCacheEntry>::iterator> cacheMap;
    size_t currentSize; // Current size in bytes
    size_t maxSize;     // Maximum size in bytes
    SemaphoreHandle_t cacheMutex;

    void evict();

public:
    LRUCache(size_t maxBytes);
    ~LRUCache();
    bool get(const std::string& key, std::vector<uint8_t>& outData);
    void put(const std::string& key, const std::vector<uint8_t>& data, int z, int x, int y);
    bool contains(const std::string& key);
    size_t getCurrentSize() const { return currentSize; }
    size_t getMaxSize() const { return maxSize; }
};

extern LRUCache tileCache;
extern M5Canvas screenBufferCanvas;

#endif // __cplusplus

extern const int TILE_SIZE;
// Declarations for GUI-related functions will go here.

#endif // GUI_H