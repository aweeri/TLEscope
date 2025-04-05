#include <string>
#include <vector>

struct TLEEntry {
    std::string name;
    std::string line1;
    std::string line2;
};

// Parses a file containing TLE data and returns a vector of TLEEntry objects.
std::vector<TLEEntry> parseTLEFile(const std::string& filename);
