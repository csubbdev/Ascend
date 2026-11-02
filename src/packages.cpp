#include "packages.hpp"

#include "interpreter.hpp"
#include "process.hpp"
#include "stdlib.hpp"

namespace fs = std::filesystem;

namespace ascend {

namespace {

// A relative path that stays inside its folder (no "..", not absolute).
bool isSafeRelativePath(const std::string& text) {
    if (text.empty()) return false;
    fs::path path(text);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& part : path) {
        if (part == "..") return false;
    }
    return true;
}

const Value* field(const MapObj& map, const char* name) { return map.find(Value::fromString(name)); }

}  // namespace

bool isValidPackageName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    if (!((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9'))) return false;
    for (char c : name) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

fs::path ascendHome() {
    std::string home = environmentVariable("ASCEND_HOME");
    if (!home.empty()) return fs::path(home);
#if defined(_WIN32)
    std::string user = environmentVariable("USERPROFILE");
#else
    std::string user = environmentVariable("HOME");
#endif
    return (user.empty() ? fs::current_path() : fs::path(user)) / ".ascend";
}

fs::path globalPackagesDir() { return ascendHome() / "packages"; }

bool readManifest(Interpreter& interpreter, const fs::path& file, Manifest& out, std::string& error) {
    std::string text;
    if (!readTextFile(file.string(), text)) {
        error = "there is no " + std::string(kManifestFile) + " in " + file.parent_path().string() +
                ", so it isn't an Ascend package";
        return false;
    }

    Value root;
    try {
        root = jsonParse(interpreter, text);
    } catch (const RuntimeError& problem) {
        runtimeErrorPending() = false;  // handled here
        error = file.string() + " isn't valid JSON: " + problem.what();
        return false;
    }
    if (!root.isMap()) {
        error = file.string() + " should contain a JSON object";
        return false;
    }
    const MapObj& map = root.asMap();

    auto text_field = [&](const char* name, std::string& target, bool required) {
        const Value* value = field(map, name);
        if (value == nullptr || value->isNull()) {
            if (required) error = file.string() + " needs a \"" + name + "\"";
            return !required;
        }
        if (!value->isString()) {
            error = "\"" + std::string(name) + "\" in " + file.string() + " should be a string";
            return false;
        }
        target = value->asString();
        return true;
    };

    Manifest manifest;
    if (!text_field("name", manifest.name, true) || !text_field("version", manifest.version, true) ||
        !text_field("description", manifest.description, false) || !text_field("main", manifest.main, false)) {
        return false;
    }
    if (!isValidPackageName(manifest.name)) {
        error = "\"" + manifest.name + "\" in " + file.string() +
                " isn't a valid package name (use lowercase letters, digits, '-' and '_')";
        return false;
    }
    if (manifest.version.empty()) {
        error = file.string() + " needs a non-empty \"version\"";
        return false;
    }
    if (!isSafeRelativePath(manifest.main)) {
        error = "\"main\" in " + file.string() + " should be a file inside the package";
        return false;
    }

    if (const Value* dependencies = field(map, "dependencies")) {
        if (!dependencies->isNull() && !dependencies->isMap()) {
            error = "\"dependencies\" in " + file.string() + " should be an object";
            return false;
        }
        if (dependencies->isMap()) {
            for (const auto& entry : dependencies->asMap().entries) {
                if (!entry.first.isString() || !isValidPackageName(entry.first.asString()) || !entry.second.isString()) {
                    error = "each dependency in " + file.string() +
                            " should be a package name and a version or source, like \"colors\": \"*\"";
                    return false;
                }
                manifest.dependencies.emplace_back(entry.first.asString(), entry.second.asString());
            }
        }
    }
    out = std::move(manifest);
    return true;
}

bool findPackageModule(Interpreter& interpreter, const std::string& request, const fs::path& fromDirectory,
                       fs::path& out, std::string& error) {
    size_t slash = request.find('/');
    std::string name = request.substr(0, slash);
    std::string rest = slash == std::string::npos ? "" : request.substr(slash + 1);
    if (!isValidPackageName(name)) return false;

    // Project packages first, from the nearest folder up; then the user's packages.
    fs::path packageDir;
    std::error_code ignored;
    fs::path dir = fs::absolute(fromDirectory, ignored).lexically_normal();
    if (!dir.has_filename() && dir.has_relative_path()) dir = dir.parent_path();  // "a/b/" -> "a/b"
    while (!dir.empty()) {
        fs::path candidate = dir / kProjectPackagesDir / name;
        if (fs::is_directory(candidate, ignored)) {
            packageDir = candidate;
            break;
        }
        fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    if (packageDir.empty()) {
        fs::path candidate = globalPackagesDir() / name;
        if (fs::is_directory(candidate, ignored)) packageDir = candidate;
    }
    if (packageDir.empty()) return false;

    fs::path file;
    if (rest.empty()) {
        Manifest manifest;
        if (!readManifest(interpreter, packageDir / kManifestFile, manifest, error)) return false;
        file = packageDir / manifest.main;
    } else {
        if (!isSafeRelativePath(rest)) {
            error = "'" + rest + "' isn't a file inside the package";
            return false;
        }
        file = packageDir / rest;
        if (!file.has_extension()) file += ".asc";
    }
    if (!fs::is_regular_file(file, ignored)) {
        error = "the package '" + name + "' has no file '" + file.filename().string() + "'";
        return false;
    }
    out = file.lexically_normal();
    return true;
}

}  // namespace ascend
