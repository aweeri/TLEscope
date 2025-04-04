// linear interpolation for floats
#include <raylib.h>
#include <math.h>
#include <conversions.h>

float LerpFloat(float start, float end, float t) {
    return start + t * (end - start);
}

// Converts latitude and longitude (in degrees) to a 3D position on a sphere.
Vector3 LatLonToXYZ(float latitude, float longitude, float earthRadius) {
    
    // Longitude correction
    longitude += 0.0f;

    // Convert degrees to radians
    float latRad = latitude * DEG2RAD;
    float lonRad = -longitude * DEG2RAD;
    
    Vector3 pos;
    pos.x = earthRadius * cosf(latRad) * cosf(lonRad);
    pos.y = earthRadius * sinf(latRad);
    pos.z = earthRadius * cosf(latRad) * sinf(lonRad);
    return pos;
}
