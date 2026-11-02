#pragma once

#include <map>
#include <string>

namespace ucs {

// Minimal flat key=value config file reader/writer (hand-rolled, no JSON
// dependency — both GUIs only need a flat set of settings persisted across
// runs, so a small INI-style format is simpler than pulling in a library).
class ConfigStore {
public:
    // Returns false if the file doesn't exist or can't be read — not
    // fatal, callers should just fall back to their built-in defaults.
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    std::string getString(const std::string& key, const std::string& default_value) const;
    int getInt(const std::string& key, int default_value) const;
    float getFloat(const std::string& key, float default_value) const;
    bool getBool(const std::string& key, bool default_value) const;

    void setString(const std::string& key, const std::string& value);
    void setInt(const std::string& key, int value);
    void setFloat(const std::string& key, float value);
    void setBool(const std::string& key, bool value);

private:
    std::map<std::string, std::string> values_;
};

} // namespace ucs
