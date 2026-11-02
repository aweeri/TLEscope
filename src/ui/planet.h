#ifndef PLANET_H
#define PLANET_H

/**
 * @file planet.h
 * @brief Planetary body selection for the test-planetary branch.
 *
 * Only the selection is tracked here — nothing in the renderer reads it yet.
 * Rendering still assumes Earth; this exists so future planet/shader work has
 * a single place to ask "which body is selected?".
 *
 * Single selection, Earth by default.
 */

typedef enum PlanetId
{
    PLANET_MERCURY = 0,
    PLANET_VENUS,
    PLANET_EARTH,
    PLANET_MARS,
    PLANET_JUPITER,
    PLANET_SATURN,
    PLANET_URANUS,
    PLANET_NEPTUNE,
    PLANET_COUNT
} PlanetId;

/** Human-readable name, e.g. "Earth". Never NULL. */
const char *PlanetName(PlanetId id);

/** Currently selected body. Defaults to PLANET_EARTH. */
PlanetId PlanetSelected(void);

/** Select a body. Out-of-range ids are ignored. */
void PlanetSelect(PlanetId id);

#endif /* PLANET_H */
