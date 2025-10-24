#ifndef TILE_CALCULATOR_H
#define TILE_CALCULATOR_H

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

// Global variables to store calculated tile coordinates
extern int globalTileX;
extern int globalTileY;
extern int globalTileZ;

// Function declarations
void initTileCalculator();
int calculateZoomLevel(double speed_kmph, int display_width, int display_height);
void latLngToTile(double lat, double lng, int zoom, int* tileX, int* tileY);
void latLngToPixelOffset(double lat, double lng, int zoom, int* pixelX, int* pixelY);
void latLngToGlobalPixel(double lat, double lng, int zoom, long* pixelX, long* pixelY);
void pixelToLatLng(int pixelX, int pixelY, int zoom, double* lat, double* lng);

#ifdef __cplusplus
}
#endif

#endif // TILE_CALCULATOR_H