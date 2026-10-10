// Skins live outside the repository, in ~/Documents/ND2/skins/<name>/: every folder that contains <name>.rml.
#pragma once
#include <string>
#include <vector>

namespace skin {

struct SkinEntry {
    std::string name;      // folder name = document name
    std::string folder;    // full path of the folder
};

// Skins found in root, sorted by name (case-insensitive). Hidden folders are skipped.
std::vector<SkinEntry> discoverSkins(const std::string& root);

}  // namespace skin
