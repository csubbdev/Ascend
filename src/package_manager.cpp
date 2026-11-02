#include "package_manager.hpp"

#include "process.hpp"
#include "stdlib.hpp"
#include "value.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <random>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ascend {

namespace {

constexpr const char* kInstallInfoFile = ".apkg.json";  // how a package was installed

class PackageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

bool startsWith(const std::string& text, const std::string& prefix) { return text.compare(0, prefix.size(), prefix) == 0; }

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string trim(const std::string& text) {
    size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string lowercase(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::vector<std::string> splitOn(const std::string& text, char separator) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t end = text.find(separator, start);
        parts.push_back(text.substr(start, end - start));
        if (end == std::string::npos) return parts;
        start = end + 1;
    }
}

// A relative path that stays inside its folder.
bool isSafeRelative(const std::string& text) {
    if (text.empty()) return false;
    fs::path path(text);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& part : path) {
        if (part == "..") return false;
    }
    return true;
}

bool isValidRepoPart(const std::string& part) {
    if (part.empty() || part == "." || part == "..") return false;
    for (char c : part) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.') return false;
    }
    return true;
}

// Splits "address@ref" at an '@' that comes after the last '/', so the user
// part of "git@host:user/repo.git" isn't mistaken for a ref.
void splitRef(std::string& text, std::string& ref) {
    size_t at = text.rfind('@');
    size_t slash = text.rfind('/');
    if (at != std::string::npos && (slash == std::string::npos || at > slash)) {
        ref = text.substr(at + 1);
        text = text.substr(0, at);
    }
}

bool looksLikePath(const std::string& spec) {
    return spec[0] == '.' || spec[0] == '/' || spec[0] == '\\' || spec[0] == '~' ||
           (spec.size() > 1 && spec[1] == ':') || spec.find('/') != std::string::npos ||
           spec.find('\\') != std::string::npos;
}

// What to install for a dependency written in ascend.json as name -> value.
std::string dependencySpec(const std::string& name, const std::string& value) {
    if (value.empty() || value == "*" || value == "latest") return name;
    if (std::isdigit(static_cast<unsigned char>(value[0]))) return name + "@" + value;
    return value;
}

std::string timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &now) != 0) return "";
#else
    if (localtime_r(&now, &local) == nullptr) return "";
#endif
    char buffer[32] = "";
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M", &local);
    return buffer;
}

std::string randomSuffix() {
    std::random_device device;
    const char* digits = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 8; ++i) out += digits[device() % 16];
    return out;
}

fs::path findProjectRoot(const fs::path& start) {
    std::error_code ignored;
    fs::path dir = fs::absolute(start, ignored).lexically_normal();
    if (!dir.has_filename() && dir.has_relative_path()) dir = dir.parent_path();  // "a/b/" -> "a/b"
    while (!dir.empty()) {
        if (fs::is_regular_file(dir / kManifestFile, ignored)) return dir;
        fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

// Copies a package's files, leaving out version control data and installed packages.
void copyPackageFiles(const fs::path& from, const fs::path& to) {
    fs::create_directories(to);
    for (const auto& entry : fs::directory_iterator(from)) {
        std::string name = entry.path().filename().string();
        if (name == ".git" || name == kProjectPackagesDir || name == kInstallInfoFile) continue;
        fs::path target = to / entry.path().filename();
        if (entry.is_directory()) {
            copyPackageFiles(entry.path(), target);
        } else if (entry.is_regular_file()) {
            fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
        }
    }
}

// The commit a fresh clone has checked out, read straight from .git.
std::string readGitCommit(const fs::path& repo) {
    std::string head;
    if (!readTextFile((repo / ".git" / "HEAD").string(), head)) return "";
    head = trim(head);
    if (!startsWith(head, "ref: ")) return head;
    std::string ref = head.substr(5);
    std::string commit;
    if (readTextFile((repo / ".git" / ref).string(), commit)) return trim(commit);
    std::string packed;
    if (readTextFile((repo / ".git" / "packed-refs").string(), packed)) {
        for (const auto& line : splitOn(packed, '\n')) {
            if (endsWith(trim(line), " " + ref)) return line.substr(0, line.find(' '));
        }
    }
    return "";
}

void writeTextFile(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) throw PackageError("can't write " + path.string());
    file << text;
    if (!file) throw PackageError("can't write " + path.string());
}

std::string padded(const std::string& text, size_t width) {
    return text.size() >= width ? text : text + std::string(width - text.size(), ' ');
}

// A folder name turned into a package name: "My Project!" -> "my-project".
std::string packageNameFrom(const std::string& folder) {
    std::string name;
    for (char c : lowercase(folder)) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        name += ok ? c : '-';
    }
    while (!name.empty() && (name[0] == '-' || name[0] == '_')) name.erase(0, 1);
    while (!name.empty() && name.back() == '-') name.pop_back();
    if (name.size() > 64) name.resize(64);
    return name.empty() ? "my-package" : name;
}

}  // namespace

void removeTree(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return;
    for (fs::recursive_directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ignored;
        fs::permissions(it->path(), fs::perms::owner_write, fs::perm_options::add, ignored);
    }
    fs::remove_all(path, ec);
}

PackageSource parsePackageSpec(const std::string& spec, const fs::path& baseDirectory) {
    std::string text = trim(spec);
    if (text.empty()) throw PackageError("an empty package name was given");
    PackageSource source;
    source.spec = text;

    auto checkRefAndFolder = [&] {
        if (!source.ref.empty() && (source.ref[0] == '-' || source.ref.find("..") != std::string::npos)) {
            throw PackageError("'" + source.ref + "' isn't a valid tag or branch name");
        }
        if (!source.subdir.empty() && !isSafeRelative(source.subdir)) {
            throw PackageError("'" + source.subdir + "' isn't a folder inside the repository");
        }
    };

    if (startsWith(text, "registry:")) {
        source.kind = PackageSource::Kind::RegistryPath;
        source.location = text.substr(9);
        if (!isSafeRelative(source.location)) throw PackageError("'" + text + "' isn't a folder inside the registry");
        return source;
    }

    if (startsWith(text, "github:")) {
        std::string rest = text.substr(7);
        splitRef(rest, source.ref);
        std::vector<std::string> parts = splitOn(rest, '/');
        if (parts.size() < 2 || !isValidRepoPart(parts[0]) || !isValidRepoPart(parts[1])) {
            throw PackageError("'" + text + "' should look like github:user/repo, github:user/repo@v1.0 or "
                               "github:user/repo/folder");
        }
        std::string repo = parts[1];
        if (endsWith(repo, ".git")) repo.resize(repo.size() - 4);
        source.kind = PackageSource::Kind::Git;
        source.location = "https://github.com/" + parts[0] + "/" + repo + ".git";
        for (size_t i = 2; i < parts.size(); ++i) source.subdir += (source.subdir.empty() ? "" : "/") + parts[i];
        checkRefAndFolder();
        return source;
    }

    bool explicitGit = startsWith(text, "git+");
    if (explicitGit || text.find("://") != std::string::npos || startsWith(text, "git@")) {
        std::string rest = explicitGit ? text.substr(4) : text;
        size_t hash = rest.find('#');
        if (hash != std::string::npos) {
            source.subdir = rest.substr(hash + 1);
            rest = rest.substr(0, hash);
        }
        splitRef(rest, source.ref);
        if (rest.empty() || rest[0] == '-') throw PackageError("'" + text + "' isn't a valid git address");
        source.kind = PackageSource::Kind::Git;
        source.location = rest;
        checkRefAndFolder();
        return source;
    }

    if (startsWith(text, "file:") || looksLikePath(text)) {
        std::string path = startsWith(text, "file:") ? text.substr(5) : text;
        if (startsWith(path, "~")) {
#if defined(_WIN32)
            path = environmentVariable("USERPROFILE") + path.substr(1);
#else
            path = environmentVariable("HOME") + path.substr(1);
#endif
        }
        fs::path folder(path);
        if (folder.is_relative()) folder = baseDirectory / folder;
        source.kind = PackageSource::Kind::Local;
        source.location = fs::absolute(folder).lexically_normal().string();
        return source;
    }

    std::string name = text;
    size_t at = text.find('@');
    if (at != std::string::npos) {
        name = text.substr(0, at);
        source.version = text.substr(at + 1);
        if (source.version.empty()) throw PackageError("'" + text + "' is missing a version after '@'");
    }
    if (!isValidPackageName(name)) {
        throw PackageError("'" + name + "' isn't a valid package name (names use lowercase letters, digits, '-' and '_')");
    }
    source.kind = PackageSource::Kind::Registry;
    source.name = name;
    return source;
}

PackageOptions defaultPackageOptions() {
    PackageOptions options;
    options.home = ascendHome();
    options.registry = environmentVariable("ASCEND_REGISTRY");
    if (options.registry.empty()) options.registry = kDefaultRegistry;
    options.workingDirectory = fs::current_path();
    return options;
}

PackageManager::PackageManager(PackageOptions options, std::ostream& out, std::ostream& err)
    : options_(std::move(options)), out_(out), err_(err), json_(discard_, noInput_) {
    if (!options_.global) projectRoot_ = findProjectRoot(options_.workingDirectory);
    packagesDir_ = inProject() ? projectRoot_ / kProjectPackagesDir : options_.home / "packages";
    // Never let git stop to ask for a password (it does for repositories that don't exist).
    setEnvironmentVariable("GIT_TERMINAL_PROMPT", "0");
}

int PackageManager::git(const std::vector<std::string>& args, bool quiet) {
    // No "detached HEAD" lecture when cloning a tag.
    std::vector<std::string> command{"git", "-c", "advice.detachedHead=false"};
    command.insert(command.end(), args.begin(), args.end());
    out_.flush();
    err_.flush();
    int code = runProcess(command, quiet);
    if (code == -1) {
        throw PackageError("couldn't run git. apkg uses git to download packages: install it and make sure it's on "
                           "your PATH");
    }
    return code;
}

// ---------------------------------------------------------------------------
// The registry

const fs::path& PackageManager::registryRoot() {
    if (!registryRoot_.empty()) return registryRoot_;
    std::error_code ignored;
    if (fs::is_directory(options_.registry, ignored)) {
        registryRoot_ = fs::absolute(options_.registry);
        return registryRoot_;
    }

    // A git repository, kept in a cache folder named after its address.
    std::string key;
    for (char c : options_.registry) key += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    if (key.size() > 80) key = key.substr(key.size() - 80);
    fs::path cache = options_.home / "cache" / ("registry-" + key);

    if (fs::is_directory(cache / ".git", ignored)) {
        bool updated = git({"-C", cache.string(), "fetch", "--depth", "1", "--quiet", "origin"}, true) == 0 &&
                       git({"-C", cache.string(), "reset", "--hard", "--quiet", "FETCH_HEAD"}, true) == 0;
        if (!updated) err_ << "warning: couldn't update the package registry, so using the copy downloaded earlier\n";
    } else {
        out_ << "Downloading the package registry...\n";
        removeTree(cache);
        fs::create_directories(cache.parent_path());
        if (git({"clone", "--depth", "1", "--quiet", "--", options_.registry, cache.string()}) != 0) {
            removeTree(cache);
            throw PackageError("couldn't download the package registry from " + options_.registry + " (are you online?)");
        }
    }
    registryRoot_ = cache;
    return registryRoot_;
}

const std::map<std::string, PackageManager::RegistryEntry>& PackageManager::registryIndex() {
    if (registryLoaded_) return registryIndex_;
    fs::path file = registryRoot() / "registry" / "index.json";
    std::string text;
    if (!readTextFile(file.string(), text)) throw PackageError("the package registry has no registry/index.json");

    Value root;
    try {
        root = jsonParse(json_, text);
    } catch (const RuntimeError& problem) {
        runtimeErrorPending() = false;
        throw PackageError(std::string("the package registry's index.json is invalid: ") + problem.what());
    }
    const Value* packages = root.isMap() ? root.asMap().find(Value::fromString("packages")) : nullptr;
    if (packages == nullptr || !packages->isMap()) {
        throw PackageError("the package registry's index.json has no \"packages\" object");
    }
    for (const auto& entry : packages->asMap().entries) {
        if (!entry.first.isString() || !entry.second.isMap()) continue;
        const MapObj& details = entry.second.asMap();
        const Value* source = details.find(Value::fromString("source"));
        if (source == nullptr || !source->isString()) continue;
        RegistryEntry registryEntry;
        registryEntry.source = source->asString();
        const Value* description = details.find(Value::fromString("description"));
        if (description != nullptr && description->isString()) registryEntry.description = description->asString();
        registryIndex_[entry.first.asString()] = registryEntry;
    }
    registryLoaded_ = true;
    return registryIndex_;
}

// ---------------------------------------------------------------------------
// Installed packages

bool PackageManager::installedManifest(const std::string& name, Manifest& out) {
    std::string ignored;
    return readManifest(json_, packagesDir_ / name / kManifestFile, out, ignored);
}

std::map<std::string, std::string> PackageManager::installInfo(const std::string& name) {
    std::map<std::string, std::string> info;
    std::string text;
    if (!readTextFile((packagesDir_ / name / kInstallInfoFile).string(), text)) return info;
    try {
        Value root = jsonParse(json_, text);
        if (!root.isMap()) return info;
        for (const auto& entry : root.asMap().entries) {
            if (entry.first.isString() && entry.second.isString()) info[entry.first.asString()] = entry.second.asString();
        }
    } catch (const RuntimeError&) {
        runtimeErrorPending() = false;
    }
    return info;
}

std::vector<std::string> PackageManager::installedNames() {
    std::vector<std::string> names;
    std::error_code ec;
    if (!fs::is_directory(packagesDir_, ec)) return names;
    for (const auto& entry : fs::directory_iterator(packagesDir_, ec)) {
        std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.' || !entry.is_directory()) continue;
        if (fs::is_regular_file(entry.path() / kManifestFile, ec)) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

// Adds or removes entries in the project's ascend.json, keeping everything else.
void PackageManager::recordDependencies(const std::vector<std::pair<std::string, std::string>>& added,
                                        const std::vector<std::string>& removed) {
    fs::path file = projectRoot_ / kManifestFile;
    std::string text;
    if (!readTextFile(file.string(), text)) return;
    Value root;
    try {
        root = jsonParse(json_, text);
    } catch (const RuntimeError&) {
        runtimeErrorPending() = false;
        return;
    }
    if (!root.isMap()) return;
    MapObj& manifest = root.asMap();
    const Value* ownName = manifest.find(Value::fromString("name"));

    Value* dependencies = manifest.find(Value::fromString("dependencies"));
    if (dependencies == nullptr || !dependencies->isMap()) {
        manifest.set(Value::fromString("dependencies"), Value::fromMap(json_.newMap()));
        dependencies = manifest.find(Value::fromString("dependencies"));
    }
    MapObj& map = dependencies->asMap();
    bool changed = false;
    for (const auto& entry : added) {
        if (ownName != nullptr && ownName->isString() && ownName->asString() == entry.first) continue;
        const Value* existing = map.find(Value::fromString(entry.first));
        if (existing != nullptr && existing->isString() && existing->asString() == entry.second) continue;
        map.set(Value::fromString(entry.first), Value::fromString(entry.second));
        out_ << "Added " << entry.first << " to " << kManifestFile << "\n";
        changed = true;
    }
    for (const auto& name : removed) {
        if (map.remove(Value::fromString(name))) {
            out_ << "Removed " << name << " from " << kManifestFile << "\n";
            changed = true;
        }
    }
    if (changed) writeTextFile(file, jsonStringify(root, 2) + "\n");
}

// ---------------------------------------------------------------------------
// Installing

std::string PackageManager::installSpec(const std::string& spec, const fs::path& base, const std::string& wantedName,
                                        const std::string& requiredBy) {
    PackageSource source = parsePackageSpec(spec, base);
    if (source.kind != PackageSource::Kind::Registry) return installFrom(source, spec, wantedName, "", requiredBy);

    if (handled_.count(source.name) != 0) return source.name;
    Manifest installed;
    if (!upgrading(requiredBy) && installedManifest(source.name, installed) &&
        (source.version.empty() || installed.version == source.version)) {
        handled_.insert(source.name);
        if (requiredBy.empty()) {
            out_ << source.name << " " << installed.version << " is already installed (use --upgrade to reinstall it)\n";
        }
        installDependencies(installed, packagesDir_ / source.name);
        return source.name;
    }

    const auto& index = registryIndex();
    auto entry = index.find(source.name);
    if (entry == index.end()) {
        throw PackageError("there's no package named '" + source.name + "' in the registry" +
                           (requiredBy.empty() ? "" : " (" + requiredBy + " needs it)") +
                           "; to look for packages, run: apkg search " + source.name);
    }
    PackageSource resolved = parsePackageSpec(entry->second.source, registryRoot());
    if (resolved.kind == PackageSource::Kind::Registry) {
        throw PackageError("the registry entry for '" + source.name + "' should point to a folder or repository");
    }
    // Packages hosted in their own repository tag each version as v<version>.
    if (resolved.kind == PackageSource::Kind::Git && !source.version.empty() && resolved.ref.empty()) {
        resolved.ref = "v" + source.version;
    }
    return installFrom(resolved, spec, source.name, source.version, requiredBy);
}

std::string PackageManager::installFrom(const PackageSource& source, const std::string& spec,
                                        const std::string& expectedName, const std::string& expectedVersion,
                                        const std::string& requiredBy) {
    std::error_code ignored;
    fs::create_directories(packagesDir_);
    fs::path temp = packagesDir_ / (".apkg-tmp-" + std::to_string(++tempCounter_) + "-" + randomSuffix());
    struct Cleanup {
        fs::path path;
        ~Cleanup() { removeTree(path); }
    } cleanup{temp};
    fs::create_directories(temp);

    fs::path files;
    std::string commit;
    switch (source.kind) {
        case PackageSource::Kind::Local:
            files = source.location;
            if (!fs::is_directory(files, ignored)) throw PackageError("the folder '" + source.location + "' doesn't exist");
            break;
        case PackageSource::Kind::RegistryPath:
            files = registryRoot() / source.location;
            if (!fs::is_directory(files, ignored)) {
                throw PackageError("the registry has no folder '" + source.location + "'");
            }
            break;
        case PackageSource::Kind::Git: {
            fs::path clone = temp / "repo";
            std::vector<std::string> args{"clone", "--depth", "1", "--quiet"};
            if (!source.ref.empty()) {
                args.push_back("--branch");
                args.push_back(source.ref);
            }
            args.push_back("--");
            args.push_back(source.location);
            args.push_back(clone.string());
            out_ << "Downloading " << source.location << (source.ref.empty() ? "" : " (" + source.ref + ")") << "...\n";
            if (git(args) != 0) {
                throw PackageError("couldn't download " + source.location +
                                   (source.ref.empty() ? "" : " at '" + source.ref + "'") +
                                   (source.ref.empty() ? " (check the address)"
                                                       : " (check the address and that the tag or branch exists)"));
            }
            commit = readGitCommit(clone);
            files = source.subdir.empty() ? clone : clone / source.subdir;
            if (!fs::is_directory(files, ignored)) {
                throw PackageError("the repository has no folder '" + source.subdir + "'");
            }
            break;
        }
        case PackageSource::Kind::Registry:
            throw PackageError("internal error: a registry package wasn't resolved");
    }

    Manifest manifest;
    std::string error;
    if (!readManifest(json_, files / kManifestFile, manifest, error)) throw PackageError(spec + ": " + error);
    if (!expectedName.empty() && manifest.name != expectedName) {
        throw PackageError(spec + " contains the package '" + manifest.name + "', not '" + expectedName + "'");
    }
    if (!expectedVersion.empty() && manifest.version != expectedVersion) {
        throw PackageError(expectedName + " " + expectedVersion + " isn't available (the registry has version " +
                           manifest.version + ")");
    }
    if (!fs::is_regular_file(files / manifest.main, ignored)) {
        throw PackageError(manifest.name + " is broken: its main file '" + manifest.main + "' is missing");
    }
    if (handled_.count(manifest.name) != 0) return manifest.name;

    Manifest current;
    bool wasInstalled = installedManifest(manifest.name, current);
    bool sameVersion = wasInstalled && current.version == manifest.version;
    // Folders on this computer are always reinstalled, since they change while being worked on.
    if (sameVersion && !upgrading(requiredBy) && source.kind != PackageSource::Kind::Local) {
        handled_.insert(manifest.name);
        if (requiredBy.empty()) {
            out_ << manifest.name << " " << manifest.version << " is already installed (use --upgrade to reinstall it)\n";
        }
        installDependencies(manifest, files);
        return manifest.name;
    }

    // Copy the files next to the destination first, so a failure can't leave half a package.
    fs::path staged = temp / "package";
    copyPackageFiles(files, staged);
    auto info = json_.newMap();
    info->set(Value::fromString("source"), Value::fromString(spec));
    info->set(Value::fromString("version"), Value::fromString(manifest.version));
    if (!commit.empty()) info->set(Value::fromString("commit"), Value::fromString(commit));
    info->set(Value::fromString("installed"), Value::fromString(timestamp()));
    writeTextFile(staged / kInstallInfoFile, jsonStringify(Value::fromMap(info), 2) + "\n");

    fs::path target = packagesDir_ / manifest.name;
    removeTree(target);
    fs::rename(staged, target);
    handled_.insert(manifest.name);

    out_ << (!wasInstalled ? "Installed " : sameVersion ? "Reinstalled " : "Upgraded ") << manifest.name << " "
         << manifest.version;
    if (wasInstalled && !sameVersion) out_ << " (was " << current.version << ")";
    if (!requiredBy.empty()) out_ << " (needed by " << requiredBy << ")";
    out_ << "\n";

    installDependencies(manifest, files);
    return manifest.name;
}

// Installs whatever the package depends on that isn't installed yet. A local
// dependency path is relative to the folder the package came from.
void PackageManager::installDependencies(const Manifest& manifest, const fs::path& base) {
    for (const auto& dependency : manifest.dependencies) {
        const std::string& name = dependency.first;
        if (handled_.count(name) != 0) continue;
        std::string spec = dependencySpec(name, dependency.second);
        Manifest existing;
        if (installedManifest(name, existing)) {
            PackageSource source = parsePackageSpec(spec, base);
            bool satisfied = source.kind != PackageSource::Kind::Registry || source.version.empty() ||
                             source.version == existing.version;
            if (satisfied) {
                handled_.insert(name);
                installDependencies(existing, packagesDir_ / name);
                continue;
            }
            out_ << manifest.name << " needs " << name << " " << source.version << " (" << existing.version
                 << " is installed)\n";
        }
        installSpec(spec, base, name, manifest.name);
    }
}

// ---------------------------------------------------------------------------
// Commands

int PackageManager::install(const std::vector<std::string>& specs) {
    try {
        handled_.clear();
        if (specs.empty()) {
            if (!inProject()) {
                err_ << "error: say which packages to install, like: apkg install colors\n"
                     << "(in a project folder with an ascend.json, `apkg install` installs its dependencies)\n";
                return 1;
            }
            Manifest project;
            std::string error;
            if (!readManifest(json_, projectRoot_ / kManifestFile, project, error)) throw PackageError(error);
            if (project.dependencies.empty()) {
                out_ << "Nothing to install: " << project.name << " has no dependencies\n";
                return 0;
            }
            for (const auto& dependency : project.dependencies) {
                installSpec(dependencySpec(dependency.first, dependency.second), projectRoot_, dependency.first, "");
            }
            out_ << "All dependencies of " << project.name << " are installed in " << packagesDir_.string() << "\n";
            return 0;
        }

        std::vector<std::pair<std::string, std::string>> added;
        for (const auto& spec : specs) {
            PackageSource source = parsePackageSpec(spec, options_.workingDirectory);
            std::string name = installSpec(spec, options_.workingDirectory, "", "");
            if (!inProject()) continue;
            // Remember the dependency in the project's ascend.json.
            std::string value = source.spec;
            if (source.kind == PackageSource::Kind::Registry) {
                value = source.version.empty() ? "*" : source.version;
            } else if (source.kind == PackageSource::Kind::Local) {
                fs::path relative = fs::path(source.location).lexically_relative(projectRoot_);
                value = "file:" + (relative.empty() ? source.location : relative.generic_string());
            }
            added.emplace_back(name, value);
        }
        if (!added.empty()) recordDependencies(added, {});
        return 0;
    } catch (const std::exception& error) {
        err_ << "error: " << error.what() << "\n";
        return 1;
    }
}

int PackageManager::uninstall(const std::vector<std::string>& names) {
    if (names.empty()) {
        err_ << "error: say which packages to uninstall, like: apkg uninstall colors\n";
        return 1;
    }
    try {
        int status = 0;
        std::vector<std::string> removed;
        for (const auto& name : names) {
            std::error_code ignored;
            Manifest manifest;
            bool installed = isValidPackageName(name) &&
                             (installedManifest(name, manifest) || fs::is_directory(packagesDir_ / name, ignored));
            if (!installed) {
                err_ << "error: " << name << " isn't installed in " << packagesDir_.string() << "\n";
                status = 1;
                continue;
            }
            removeTree(packagesDir_ / name);
            out_ << "Uninstalled " << name << (manifest.version.empty() ? "" : " " + manifest.version) << "\n";
            removed.push_back(name);
        }
        if (inProject() && !removed.empty()) recordDependencies({}, removed);

        for (const auto& other : installedNames()) {
            Manifest manifest;
            if (!installedManifest(other, manifest)) continue;
            for (const auto& dependency : manifest.dependencies) {
                if (std::find(removed.begin(), removed.end(), dependency.first) != removed.end()) {
                    err_ << "note: " << other << " needs " << dependency.first << ", which is no longer installed\n";
                }
            }
        }
        return status;
    } catch (const std::exception& error) {
        err_ << "error: " << error.what() << "\n";
        return 1;
    }
}

int PackageManager::list() {
    try {
        std::vector<std::string> names = installedNames();
        if (names.empty()) {
            out_ << "No packages are installed in " << packagesDir_.string() << "\n";
        } else {
            std::vector<std::vector<std::string>> rows;
            for (const auto& name : names) {
                Manifest manifest;
                installedManifest(name, manifest);
                auto info = installInfo(name);
                rows.push_back({name, manifest.version, info.count("source") != 0 ? info["source"] : ""});
            }
            size_t nameWidth = 7;
            size_t versionWidth = 7;
            for (const auto& row : rows) {
                nameWidth = std::max(nameWidth, row[0].size());
                versionWidth = std::max(versionWidth, row[1].size());
            }
            out_ << "Packages in " << packagesDir_.string() << ":\n\n";
            out_ << padded("Package", nameWidth) << "  " << padded("Version", versionWidth) << "  Source\n";
            out_ << std::string(nameWidth, '-') << "  " << std::string(versionWidth, '-') << "  ------\n";
            for (const auto& row : rows) {
                out_ << padded(row[0], nameWidth) << "  " << padded(row[1], versionWidth) << "  " << row[2] << "\n";
            }
        }

        if (inProject()) {
            Manifest project;
            std::string error;
            if (readManifest(json_, projectRoot_ / kManifestFile, project, error)) {
                std::vector<std::string> missing;
                for (const auto& dependency : project.dependencies) {
                    Manifest ignored;
                    if (!installedManifest(dependency.first, ignored)) missing.push_back(dependency.first);
                }
                if (!missing.empty()) {
                    out_ << "\nNot installed yet (run `apkg install`):";
                    for (const auto& name : missing) out_ << " " << name;
                    out_ << "\n";
                }
            }
        }
        return 0;
    } catch (const std::exception& error) {
        err_ << "error: " << error.what() << "\n";
        return 1;
    }
}

int PackageManager::show(const std::string& name) {
    Manifest manifest;
    if (!isValidPackageName(name) || !installedManifest(name, manifest)) {
        err_ << "error: " << name << " isn't installed in " << packagesDir_.string() << "\n";
        return 1;
    }
    auto info = installInfo(name);
    out_ << "Name:        " << manifest.name << "\n";
    out_ << "Version:     " << manifest.version << "\n";
    if (!manifest.description.empty()) out_ << "Description: " << manifest.description << "\n";
    out_ << "Location:    " << (packagesDir_ / name).string() << "\n";
    out_ << "Main file:   " << manifest.main << "\n";
    if (info.count("source") != 0) out_ << "Source:      " << info["source"] << "\n";
    if (info.count("commit") != 0) out_ << "Commit:      " << info["commit"] << "\n";
    if (info.count("installed") != 0) out_ << "Installed:   " << info["installed"] << "\n";

    std::string needs;
    for (const auto& dependency : manifest.dependencies) needs += (needs.empty() ? "" : ", ") + dependency.first;
    out_ << "Requires:    " << (needs.empty() ? "nothing" : needs) << "\n";

    std::string requiredBy;
    for (const auto& other : installedNames()) {
        Manifest otherManifest;
        if (!installedManifest(other, otherManifest)) continue;
        for (const auto& dependency : otherManifest.dependencies) {
            if (dependency.first == name) requiredBy += (requiredBy.empty() ? "" : ", ") + other;
        }
    }
    out_ << "Required by: " << (requiredBy.empty() ? "nothing" : requiredBy) << "\n";
    return 0;
}

int PackageManager::search(const std::string& term) {
    try {
        const auto& index = registryIndex();
        std::string wanted = lowercase(term);
        std::vector<std::pair<std::string, std::string>> matches;
        for (const auto& entry : index) {
            if (wanted.empty() || lowercase(entry.first).find(wanted) != std::string::npos ||
                lowercase(entry.second.description).find(wanted) != std::string::npos) {
                matches.emplace_back(entry.first, entry.second.description);
            }
        }
        if (matches.empty()) {
            out_ << "No packages match '" << term << "'\n";
            return 0;
        }
        size_t width = 0;
        for (const auto& match : matches) width = std::max(width, match.first.size());
        for (const auto& match : matches) {
            Manifest installed;
            bool isInstalled = installedManifest(match.first, installed);
            out_ << padded(match.first, width) << "  " << match.second
                 << (isInstalled ? "  [installed " + installed.version + "]" : "") << "\n";
        }
        return 0;
    } catch (const std::exception& error) {
        err_ << "error: " << error.what() << "\n";
        return 1;
    }
}

int PackageManager::init(const std::string& name) {
    try {
        const fs::path& folder = options_.workingDirectory;
        std::error_code ignored;
        if (fs::exists(folder / kManifestFile, ignored)) {
            err_ << "error: " << (folder / kManifestFile).string() << " already exists\n";
            return 1;
        }
        std::string packageName = name.empty() ? packageNameFrom(fs::absolute(folder).filename().string()) : name;
        if (!isValidPackageName(packageName)) {
            err_ << "error: '" << packageName
                 << "' isn't a valid package name (use lowercase letters, digits, '-' and '_')\n";
            return 1;
        }

        auto manifest = json_.newMap();
        manifest->set(Value::fromString("name"), Value::fromString(packageName));
        manifest->set(Value::fromString("version"), Value::fromString("0.1.0"));
        manifest->set(Value::fromString("description"), Value::fromString(""));
        manifest->set(Value::fromString("main"), Value::fromString("main.asc"));
        manifest->set(Value::fromString("dependencies"), Value::fromMap(json_.newMap()));
        writeTextFile(folder / kManifestFile, jsonStringify(Value::fromMap(manifest), 2) + "\n");

        if (!fs::exists(folder / "main.asc", ignored)) {
            writeTextFile(folder / "main.asc", "// " + packageName + "\n\nprint(\"Hello from " + packageName + "!\")\n");
        }

        // Installed packages don't belong in version control.
        fs::path gitignore = folder / ".gitignore";
        std::string existing;
        readTextFile(gitignore.string(), existing);
        if (existing.find(kProjectPackagesDir) == std::string::npos) {
            if (!existing.empty() && existing.back() != '\n') existing += '\n';
            writeTextFile(gitignore, existing + kProjectPackagesDir + "/\n");
        }

        out_ << "Created " << kManifestFile << " for " << packageName << "\n\n"
             << "  Add packages:  apkg install colors\n"
             << "  Run it:        ascend main.asc\n";
        return 0;
    } catch (const std::exception& error) {
        err_ << "error: " << error.what() << "\n";
        return 1;
    }
}

}  // namespace ascend
