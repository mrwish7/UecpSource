#include "config_store.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ucs {

namespace {

std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

} // namespace

bool ConfigStore::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') continue;

        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;

        std::string key = Trim(trimmed.substr(0, eq));
        std::string value = Trim(trimmed.substr(eq + 1));
        if (!key.empty()) values_[key] = value;
    }
    return true;
}

bool ConfigStore::save(const std::string& path) const {
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return false;

    for (const auto& kv : values_) {
        file << kv.first << "=" << kv.second << "\n";
    }
    return true;
}

std::string ConfigStore::getString(const std::string& key, const std::string& default_value) const {
    auto it = values_.find(key);
    return (it != values_.end()) ? it->second : default_value;
}

int ConfigStore::getInt(const std::string& key, int default_value) const {
    auto it = values_.find(key);
    if (it == values_.end() || it->second.empty()) return default_value;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return default_value;
    }
}

float ConfigStore::getFloat(const std::string& key, float default_value) const {
    auto it = values_.find(key);
    if (it == values_.end() || it->second.empty()) return default_value;
    try {
        return std::stof(it->second);
    } catch (...) {
        return default_value;
    }
}

bool ConfigStore::getBool(const std::string& key, bool default_value) const {
    auto it = values_.find(key);
    if (it == values_.end() || it->second.empty()) return default_value;
    return it->second == "1" || it->second == "true";
}

void ConfigStore::setString(const std::string& key, const std::string& value) {
    values_[key] = value;
}

void ConfigStore::setInt(const std::string& key, int value) {
    values_[key] = std::to_string(value);
}

void ConfigStore::setFloat(const std::string& key, float value) {
    std::ostringstream oss;
    oss.precision(6);
    oss << value;
    values_[key] = oss.str();
}

void ConfigStore::setBool(const std::string& key, bool value) {
    values_[key] = value ? "1" : "0";
}

} // namespace ucs
