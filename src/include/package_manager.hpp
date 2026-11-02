#ifndef ASCEND_PACKAGE_MANAGER_HPP
#define ASCEND_PACKAGE_MANAGER_HPP

#include "interpreter.hpp"
#include "packages.hpp"

#include <filesystem>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ascend {

// The registry used when ASCEND_REGISTRY isn't set: registry/index.json in
// the Ascend repository.
constexpr const char* kDefaultRegistry = "https://github.com/csubbdev/Ascend.git";

// Where a package comes from, parsed from what the user typed:
//   colors                      a package from the registry (latest version)
//   colors@1.2.0                a specific version from the registry
//   github:user/repo[/dir][@tag]  a GitHub repository (optionally a folder in it)
//   git+<url>[@tag][#dir]       any git repository
//   ./path/to/folder            a folder on this computer (also file:path)
struct PackageSource {
    enum class Kind { Registry, Local, Git, RegistryPath };

    Kind kind = Kind::Registry;
    std::string spec;      // as written
    std::string name;      // Registry: the package name
    std::string version;   // Registry: the version asked for ("" = latest)
    std::string location;  // Local: folder; Git: URL; RegistryPath: folder inside the registry
    std::string ref;       // Git: tag or branch ("" = the default branch)
    std::string subdir;    // Git: folder inside the repository
};

// Throws std::runtime_error with a readable message for invalid specs.
PackageSource parsePackageSpec(const std::string& spec, const std::filesystem::path& baseDirectory);

struct PackageOptions {
    std::filesystem::path home;              // user packages and caches (ASCEND_HOME)
    std::string registry;                    // registry: a git URL or a local folder
    std::filesystem::path workingDirectory;  // where the command was run
    bool global = false;                     // use the user's packages even inside a project
    bool upgrade = false;                    // reinstall packages that are already installed
};

// Options from the environment: ASCEND_HOME, ASCEND_REGISTRY and the current folder.
PackageOptions defaultPackageOptions();

// Installs, removes and lists packages. Each public command prints what it
// does to `out`, problems to `err`, and returns a process exit code.
//
// Inside a project (a folder with ascend.json, or below one) packages go into
// the project's ascend_packages folder and are recorded in its ascend.json.
// Elsewhere, or with `global`, they go into the user's packages folder.
class PackageManager {
public:
    PackageManager(PackageOptions options, std::ostream& out, std::ostream& err);

    int init(const std::string& name);
    int install(const std::vector<std::string>& specs);
    int uninstall(const std::vector<std::string>& names);
    int list();
    int show(const std::string& name);
    int search(const std::string& term);

    const std::filesystem::path& packagesDir() const { return packagesDir_; }
    const std::filesystem::path& projectRoot() const { return projectRoot_; }
    bool inProject() const { return !projectRoot_.empty(); }

private:
    struct RegistryEntry {
        std::string description;
        std::string source;
    };

    std::string installSpec(const std::string& spec, const std::filesystem::path& base, const std::string& wantedName,
                            const std::string& requiredBy);
    std::string installFrom(const PackageSource& source, const std::string& spec, const std::string& expectedName,
                            const std::string& expectedVersion, const std::string& requiredBy);
    void installDependencies(const Manifest& manifest, const std::filesystem::path& base);
    // --upgrade reinstalls the packages that were asked for; their
    // dependencies are only installed or changed when needed.
    bool upgrading(const std::string& requiredBy) const { return options_.upgrade && requiredBy.empty(); }

    const std::filesystem::path& registryRoot();
    const std::map<std::string, RegistryEntry>& registryIndex();
    int git(const std::vector<std::string>& args, bool quiet = false);

    bool installedManifest(const std::string& name, Manifest& out);
    std::map<std::string, std::string> installInfo(const std::string& name);
    std::vector<std::string> installedNames();
    void recordDependencies(const std::vector<std::pair<std::string, std::string>>& added,
                            const std::vector<std::string>& removed);

    PackageOptions options_;
    std::ostream& out_;
    std::ostream& err_;
    std::ostringstream discard_;
    std::istringstream noInput_;
    Interpreter json_;  // only used to read and write JSON

    std::filesystem::path projectRoot_;
    std::filesystem::path packagesDir_;
    std::filesystem::path registryRoot_;
    std::map<std::string, RegistryEntry> registryIndex_;
    bool registryLoaded_ = false;
    std::set<std::string> handled_;  // packages installed (or checked) by this command
    int tempCounter_ = 0;
};

// Deletes a folder and everything in it, including read-only files (which
// git creates on Windows). Errors are ignored.
void removeTree(const std::filesystem::path& path);

}  // namespace ascend

#endif  // ASCEND_PACKAGE_MANAGER_HPP
