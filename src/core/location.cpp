#include "location.h"
#include <string.h>

Location locations[MAX_LOCATIONS];
int location_count = 0;

/* target index for the map picker; -1 means the home location */
int pick_location_index = -1;

int GetHomeLocationIndex(void)
{
    for (int i = 0; i < location_count; i++)
    {
        if (locations[i].is_home)
            return i;
    }
    return -1;
}

Location *GetHomeLocation(void)
{
    int idx = GetHomeLocationIndex();
    return (idx >= 0) ? &locations[idx] : NULL;
}

void SetHomeLocation(int idx)
{
    if (idx < 0 || idx >= location_count)
        return;

    for (int i = 0; i < location_count; i++)
        locations[i].is_home = (i == idx);
}

int AddLocation(const char *name, float lat, float lon, float alt)
{
    if (location_count >= MAX_LOCATIONS)
        return -1;

    Location *loc = &locations[location_count];
    memset(loc, 0, sizeof(Location));
    if (name)
    {
        strncpy(loc->name, name, sizeof(loc->name) - 1);
        loc->name[sizeof(loc->name) - 1] = '\0';
    }
    loc->lat = lat;
    loc->lon = lon;
    loc->alt = alt;
    loc->is_home = false;

    return location_count++;
}

void RemoveLocation(int idx)
{
    if (idx < 0 || idx >= location_count)
        return;

    bool was_home = locations[idx].is_home;

    /* compact the list */
    for (int i = idx; i < location_count - 1; i++)
        locations[i] = locations[i + 1];
    location_count--;

    /* if we removed the home location, promote the first remaining one */
    if (was_home && location_count > 0)
        locations[0].is_home = true;
}

void UpdateLocation(int idx, const char *name, float lat, float lon, float alt)
{
    if (idx < 0 || idx >= location_count)
        return;

    Location *loc = &locations[idx];
    if (name)
    {
        strncpy(loc->name, name, sizeof(loc->name) - 1);
        loc->name[sizeof(loc->name) - 1] = '\0';
    }
    loc->lat = lat;
    loc->lon = lon;
    loc->alt = alt;
}

int FindLocationByName(const char *name)
{
    if (!name)
        return -1;
    for (int i = 0; i < location_count; i++)
    {
        if (strcmp(locations[i].name, name) == 0)
            return i;
    }
    return -1;
}