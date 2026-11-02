#pragma once

#include <string>

namespace ucs {

// Directory containing the running executable, with a trailing separator.
// Empty string if it can't be determined.
std::string GetExecutableDirectory();

// Resolves the config file path: an explicit "--config <path>" command-line
// argument takes precedence; otherwise defaults to
// <executable-directory>/<default_filename>. The override exists so
// multiple instances of the same app can run concurrently, each with its
// own saved profile, instead of fighting over one shared file.
std::string ResolveConfigPath(int argc, char** argv, const std::string& default_filename);

} // namespace ucs
