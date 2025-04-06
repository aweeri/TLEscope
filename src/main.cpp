#include "TLEscope.h"
#include "raylib.h"

int main()
{
    gpBase = new cTLEscope;

    if (gpBase->Init())
    {
        gpBase->GetScene()->GenEarth();
        gpBase->GetRenderer()->SetSceneSpecific();
    }

    // Main Application Loop
    while (!WindowShouldClose())
    {
        gpBase->GetRenderer()->Render();
    }

    delete gpBase;
    CloseWindow();

    return 0;
}
