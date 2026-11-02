#include "ui/planet.h"

/*
 * Test-planetary: the selected body is process-global state, not persisted to
 * settings.json. Keeping it out of AppConfig means this branch cannot corrupt
 * a user's saved config while the planet work is still experimental.
 */

static PlanetId g_selected = PLANET_EARTH;

static const char *const kPlanetNames[PLANET_COUNT] = {
    "Mercury",
    "Venus",
    "Earth",
    "Mars",
    "Jupiter",
    "Saturn",
    "Uranus",
    "Neptune",
};

const char *PlanetName(PlanetId id)
{
    if (id < 0 || id >= PLANET_COUNT)
        return "Unknown";
    return kPlanetNames[id];
}

PlanetId PlanetSelected(void)
{
    return g_selected;
}

void PlanetSelect(PlanetId id)
{
    if (id < 0 || id >= PLANET_COUNT)
        return;
    g_selected = id;
}
