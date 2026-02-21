#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#ifdef _WIN32
    #include <direct.h>
    #define MKDIR(dir) _mkdir(dir)
#else
    #include <sys/stat.h>
    #define MKDIR(dir) mkdir(dir, 0755)
#endif

#define OUTPUT_DIR "/"
#define OUTPUT_FILE "data.tle"

typedef struct {
    const char *id;
    const char *name;
    const char *url;
} TLESource;

// links to grab the satellite files
TLESource SOURCES[] = {
    {"1", "Last 30 Days' Launches", "https://celestrak.org/NORAD/elements/gp.php?GROUP=last-30-days&FORMAT=tle"},
    {"2", "Space Stations", "https://celestrak.org/NORAD/elements/gp.php?GROUP=stations&FORMAT=tle"},
    {"3", "100 Brightest", "https://celestrak.org/NORAD/elements/gp.php?GROUP=visual&FORMAT=tle"},
    {"4", "Active Satellites", "https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=tle"},
    {"5", "Analyst Satellites", "https://celestrak.org/NORAD/elements/gp.php?GROUP=analyst&FORMAT=tle"},
    {"6", "Russian ASAT (COSMOS 1408)", "https://celestrak.org/NORAD/elements/gp.php?GROUP=cosmos-1408-debris&FORMAT=tle"},
    {"7", "Chinese ASAT (FENGYUN 1C)", "https://celestrak.org/NORAD/elements/gp.php?GROUP=fengyun-1c-debris&FORMAT=tle"},
    {"8", "IRIDIUM 33 Debris", "https://celestrak.org/NORAD/elements/gp.php?GROUP=iridium-33-debris&FORMAT=tle"},
    {"9", "COSMOS 2251 Debris", "https://celestrak.org/NORAD/elements/gp.php?GROUP=cosmos-2251-debris&FORMAT=tle"},
    {"10", "Weather", "https://celestrak.org/NORAD/elements/gp.php?GROUP=weather&FORMAT=tle"},
    {"11", "NOAA", "https://celestrak.org/NORAD/elements/gp.php?GROUP=noaa&FORMAT=tle"},
    {"12", "GOES", "https://celestrak.org/NORAD/elements/gp.php?GROUP=goes&FORMAT=tle"},
    {"13", "Earth Resources", "https://celestrak.org/NORAD/elements/gp.php?GROUP=resource&FORMAT=tle"},
    {"14", "SARSAT", "https://celestrak.org/NORAD/elements/gp.php?GROUP=sarsat&FORMAT=tle"},
    {"15", "Disaster Monitoring", "https://celestrak.org/NORAD/elements/gp.php?GROUP=dmc&FORMAT=tle"},
    {"16", "TDRSS", "https://celestrak.org/NORAD/elements/gp.php?GROUP=tdrss&FORMAT=tle"},
    {"17", "ARGOS", "https://celestrak.org/NORAD/elements/gp.php?GROUP=argos&FORMAT=tle"},
    {"18", "Planet", "https://celestrak.org/NORAD/elements/gp.php?GROUP=planet&FORMAT=tle"},
    {"19", "Spire", "https://celestrak.org/NORAD/elements/gp.php?GROUP=spire&FORMAT=tle"},
    {"20", "Starlink", "https://celestrak.org/NORAD/elements/gp.php?GROUP=starlink&FORMAT=tle"},
    {"21", "OneWeb", "https://celestrak.org/NORAD/elements/gp.php?GROUP=oneweb&FORMAT=tle"},
    {"22", "GPS Operational", "https://celestrak.org/NORAD/elements/gp.php?GROUP=gps-ops&FORMAT=tle"},
    {"23", "Galileo", "https://celestrak.org/NORAD/elements/gp.php?GROUP=galileo&FORMAT=tle"},
    {"24", "Amateur Radio", "https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle"},
    {"25", "CubeSats", "https://celestrak.org/NORAD/elements/gp.php?GROUP=cubesat&FORMAT=tle"}
};

const int SOURCE_COUNT = sizeof(SOURCES) / sizeof(SOURCES[0]);

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}

// downloading the data
void download_tle(CURL *curl, const char *url, const char *name) {
    FILE *fp = fopen(OUTPUT_FILE, "ab");
    if (!fp) return;

    printf("Fetching: %s...\n", name);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Error fetching %s: %s\n", name, curl_easy_strerror(res));
    }
    
    fputs("\n", fp); 
    fclose(fp);
}

int main() {
    printf("--- CelesTrak TLE Downloader ---\n");
    for (int i = 0; i < SOURCE_COUNT; i++) {
        printf("[%s] %s\n", SOURCES[i].id, SOURCES[i].name);
    }

    char input[256];
    printf("\nEnter numbers to download (separated by space): ");
    if (!fgets(input, sizeof(input), stdin)) return 1;

    // make sure we have a folder
    MKDIR(OUTPUT_DIR);
    FILE *clear = fopen(OUTPUT_FILE, "wb");
    if (clear) fclose(clear);

    curl_global_init(CURL_GLOBAL_ALL);
    CURL *curl = curl_easy_init();

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        char *token = strtok(input, " \n");
        while (token) {
            int found = 0;
            for (int i = 0; i < SOURCE_COUNT; i++) {
                if (strcmp(token, SOURCES[i].id) == 0) {
                    download_tle(curl, SOURCES[i].url, SOURCES[i].name);
                    found = 1;
                    break;
                }
            }
            if (!found) printf("Invalid option: %s\n", token);
            token = strtok(NULL, " \n");
        }
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    printf("\nDone. Data saved to %s\n", OUTPUT_FILE);
    return 0;
}