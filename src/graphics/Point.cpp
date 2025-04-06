#include "graphics/Point.h"
#include <raylib.h>

Point CreatePoint(const char *imagePath, Vector3 position, float scale) {
    Point point = { 0 };
    point.texture = LoadTexture(imagePath);
    point.position = position;
    point.scale = scale;
    return point;
}

void UnloadPoint(Point *point) {
    UnloadTexture(point->texture);
}
