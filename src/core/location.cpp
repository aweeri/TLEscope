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
    loc->is_home = (location_count == 0);

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

    /* Always preserve the invariant when at least one location remains.
     * This also repairs malformed state where no location was marked home. */
    if (location_count > 0 && (was_home || GetHomeLocationIndex() < 0))
        SetHomeLocation(0);
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

/* -- Demo-only aerospace markers ------------------------------------------- */
typedef struct
{
    const char *name;
    float lat;
    float lon;
} DemoMarker;

static const DemoMarker kDemoAerospaceMarkers[] = {
    {"Gliwice", 50.2945f, 18.6714f},   /* home for the demo (Poland)      */
    {"ESOC", 49.8685f, 8.6228f},       /* ESA ESOC, Darmstadt, Germany    */
    {"ESTEC", 52.2183f, 4.4200f},      /* ESA ESTEC, Noordwijk, NL        */
    {"Kourou", 5.2360f, -52.7686f},    /* Guiana Space Centre (ESA)       */
    {"KSC", 28.5729f, -80.6490f},      /* Kennedy Space Center, USA       */
    {"JPL", 34.2012f, -118.1711f},     /* NASA JPL, Pasadena, USA         */
};

void LoadDemoAerospaceMarkers(void)
{
    /* DEMO-ONLY: the director restores the user's locations on exit */
    location_count = 0;

    const int n = (int)(sizeof(kDemoAerospaceMarkers) / sizeof(kDemoAerospaceMarkers[0]));
    for (int i = 0; i < n; i++)
    {
        const DemoMarker *m = &kDemoAerospaceMarkers[i];
        AddLocation(m->name, m->lat, m->lon, 0.0f);
    }

    /* Gliwice is the home location for the duration of the demo. */
    int home = FindLocationByName("Gliwice");
    if (home >= 0)
        SetHomeLocation(home);
}