#include "tile_calculator.h"
#include <math.h>
#include <M5Unified.h> // For M5.Display.width() and M5.Display.height()

void initTileCalculator() {
    // No specific initialization needed for now, but can be extended later.
}

// Function to dynamically determine the zoom level based on speed and display size
int calculateZoomLevel(double speed_kmph, int display_width, int display_height) {
    int zoom = 0;

    // Simple heuristic for speed-based zoom
    if (speed_kmph < 5.0) { // Stationary or very slow
        zoom = 15;
    } else if (speed_kmph < 20.0) { // Walking/Cycling speed
        zoom = 13;
    } else { // Driving speed
        zoom = 12;
    }

    // Optional: Further refine zoom based on display size if needed
    // For example, if display is very small, might need to adjust zoom to show more context.
    // This part can be expanded based on specific display requirements.
    // For now, we'll primarily rely on speed.

    return zoom;
}

// Function to convert latitude and longitude to Web Mercator tile X and Y coordinates
void latLngToTile(double lat, double lng, int zoom, int* tileX, int* tileY) {
    double lat_rad = lat * M_PI / 180.0;
    double n = pow(2.0, zoom);

    *tileX = (int)floor((lng + 180.0) / 360.0 * n);
    *tileY = (int)floor((1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * n);
}

// Function to calculate pixel offset within a tile
extern const int TILE_SIZE; // Assuming TILE_SIZE is globally available

void latLngToPixelOffset(double lat, double lng, int zoom, int* pixelX, int* pixelY) {
    double lat_rad = lat * M_PI / 180.0;
    double n = pow(2.0, zoom);

    double tileX_double = (lng + 180.0) / 360.0 * n;
    double tileY_double = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * n;

    int tileX_int = (int)floor(tileX_double);
    int tileY_int = (int)floor(tileY_double);

    *pixelX = (int)((tileX_double - tileX_int) * TILE_SIZE);
    *pixelY = (int)((tileY_double - tileY_int) * TILE_SIZE);
}
