#ifndef LOCATION_H
#define LOCATION_H

/** target index for the next pick-on-map action; -1 = home location */
extern int pick_location_index;

/**
 * @file location.h
 * @brief Unified, persisting locations system (markers + home)
 *
 * Markers and the home location are merged into a single named list of
 * locations. "Home" is just a flag on one entry; exactly one location is
 * home at a time. All consumers (passes, scope, polar plot, ground tracks,
 * rotator) read the home location from this unified list.
 */

#include "types.h"

/** index of the location currently flagged as home, or -1 if none */
int GetHomeLocationIndex(void);

/** pointer to the home location, or NULL if none is set */
Location *GetHomeLocation(void);

/** mark the given location as home, clearing the flag on all others */
void SetHomeLocation(int idx);

/** add a new location and return its index (or -1 if the list is full) */
int AddLocation(const char *name, float lat, float lon, float alt);

/** remove a location by index, compacting the list and fixing home */
void RemoveLocation(int idx);

/** update a location's fields in place */
void UpdateLocation(int idx, const char *name, float lat, float lon, float alt);

/** find a location by exact name, or -1 if not present */
int FindLocationByName(const char *name);

#endif // LOCATION_H