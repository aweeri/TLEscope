#ifndef LOCATION_H
#define LOCATION_H

/** target index for the next pick-on-map action; -1 = home location */
extern int pick_location_index;

/* Unified locations list (markers + home); home is just a flag on one entry. */

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

/** DEMO-ONLY: swap in a neutral aerospace marker set (restored on exit). */
void LoadDemoAerospaceMarkers(void);

#endif // LOCATION_H