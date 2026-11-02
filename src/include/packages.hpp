#ifndef ASCEND_PACKAGES_HPP
#define ASCEND_PACKAGES_HPP

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ascend {

class Interpreter;

// A package's ascend.json.
struct Manifest {
    std::string name;
    std::string version;
    std::string description;
    std::string main = "main.asc";  // the file import("name") runs
    // name -> what to install: "*" (any version from the registry), a version
    // like "1.2.0", or a source such as "github:user/repo@v1.0".
    std::vector<std::pair<std::string, std::string>> dependencies;
};

constexpr const char* kManifestFile = "ascend.json";
constexpr const char* kProjectPackagesDir = "ascend_packages";

// Package names: lowercase letters, digits, '-' and '_', starting with a
// letter or digit.
bool isValidPackageName(const std::string& name);

// Where per-user packages and caches live: $ASCEND_HOME, or ~/.ascend.
std::filesystem::path ascendHome();

// Packages installed for the user (outside any project): <home>/packages.
std::filesystem::path globalPackagesDir();

// Reads and checks a package manifest. On failure returns false and
// describes the problem in `error`.
bool readManifest(Interpreter& interpreter, const std::filesystem::path& file, Manifest& out, std::string& error);

// Finds the file that import("name") or import("name/path/in/package")
// refers to. Looks in the ascend_packages folder of `fromDirectory` and each
// folder above it, then in the user's packages. Returns false if there is no
// such package (error stays empty) or the package is broken (error says why).
bool findPackageModule(Interpreter& interpreter, const std::string& request, const std::filesystem::path& fromDirectory,
                       std::filesystem::path& out, std::string& error);

}  // namespace ascend

#endif  // ASCEND_PACKAGES_HPP
