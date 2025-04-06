#ifndef TLE_SCOPE_CONVERSIONS_H
#define TLE_SCOPE_CONVERSIONS_H

#include <raylib.h>

// Performs a linear interpolation between start and end using t.
float LerpFloat(float start, float end, float t);

// Converts latitude and longitude (in degrees) to a 3D position on a sphere.
Vector3 LatLonToXYZ(float latitude, float longitude, float earthRadius);

#endif