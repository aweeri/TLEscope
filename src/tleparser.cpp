#include "TLEParser.h"
#include <fstream>
#include <iostream>

std::vector<TLEEntry> parseTLEFile(const std::string& filename) {
    std::vector<TLEEntry> entries;
    std::ifstream infile(filename);

    // Check if the file is open
    if (!infile.is_open()) {
        std::cerr << "Error: File not found: " << filename << std::endl;
        return entries;
    }

    std::string line;
    while (std::getline(infile, line)) {
        // Skip blank
        if (line.empty())
            continue;

        TLEEntry entry;
        entry.name = line;

        // Read the next two lines
        if (!std::getline(infile, entry.line1)) {
            std::cerr << "Incomplete TLE entry for satellite: " << entry.name << std::endl;
            break;
        }
        if (!std::getline(infile, entry.line2)) {
            std::cerr << "Incomplete TLE entry for satellite: " << entry.name << std::endl;
            break;
        }

        entries.push_back(entry);
    }

    return entries;
}
