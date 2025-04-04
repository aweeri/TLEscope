#include "raylib.h"

typedef struct Point {
    Texture2D texture;
    Vector3 position;
    float scale;
} Point;

Point CreatePoint(const char *imagePath, Vector3 position, float scale);


void UnloadPoint(Point *point);