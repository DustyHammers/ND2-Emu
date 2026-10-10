#include "skin_discovery.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace skin {

std::vector<SkinEntry> discoverSkins(const std::string& root) {
    std::vector<SkinEntry> out;
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(root), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        const std::string name = it->path().filename().u8string();
        if (name.empty() || name[0] == '.') continue;
        if (fs::is_regular_file(it->path() / fs::u8path(name + ".rml"), ec)) out.push_back({name, it->path().u8string()});
    }
    auto lower = [](std::string s) { for (char& c : s) c = char(std::tolower(uint8_t(c))); return s; };
    std::sort(out.begin(), out.end(), [&](const SkinEntry& a, const SkinEntry& b) { return lower(a.name) < lower(b.name); });
    return out;
}

}  // namespace skin
