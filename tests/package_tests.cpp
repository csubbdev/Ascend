#include "interpreter.hpp"
#include "package_manager.hpp"
#include "process.hpp"
#include "runner.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace ascend;
namespace fs = std::filesystem;

#ifndef ASCEND_SOURCE_DIR
#error "ASCEND_SOURCE_DIR must be defined (the repository root, which holds the package registry)"
#endif

namespace {

bool contains(const std::string& text, const std::string& fragment) { return text.find(fragment) != std::string::npos; }

std::string readFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void writeFile(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// A fresh folder for one test, with a project folder and an ASCEND_HOME in it.
// Deleted (and ASCEND_HOME restored) when the test ends.
struct Sandbox {
    Sandbox() {
        std::random_device device;
        root = fs::temp_directory_path() / ("ascend-package-test-" + std::to_string(device()));
        project = root / "project";
        home = root / "home";
        fs::create_directories(project);
        fs::create_directories(home);
        savedHome = environmentVariable("ASCEND_HOME");
        setEnvironmentVariable("ASCEND_HOME", home.string());
        setEnvironmentVariable("NO_COLOR", "1");
    }
    ~Sandbox() {
        setEnvironmentVariable("ASCEND_HOME", savedHome);
        removeTree(root);
    }

    PackageOptions options(const fs::path& directory, bool global = false) const {
        PackageOptions result;
        result.home = home;
        result.registry = ASCEND_SOURCE_DIR;
        result.workingDirectory = directory;
        result.global = global;
        return result;
    }

    fs::path root;
    fs::path project;
    fs::path home;
    std::string savedHome;
};

struct Outcome {
    int code;
    std::string out;
    std::string err;
};

// Runs one apkg command, like the command line does.
template <typename Command>
Outcome apkg(const PackageOptions& options, Command command) {
    std::ostringstream out;
    std::ostringstream err;
    PackageManager manager(options, out, err);
    int code = command(manager);
    return {code, out.str(), err.str()};
}

// Runs Ascend code as if it were the file `file`, so imports start from its folder.
std::string runAt(const fs::path& file, const std::string& code) {
    std::ostringstream out;
    std::ostringstream err;
    std::istringstream in;
    RunResult result;
    {
        Interpreter interpreter(out, in);
        RunOptions options;
        options.filename = file.string();
        result = runSource(interpreter, code, options, err);
    }
    return result.status == RunStatus::Ok ? out.str() : "ERROR: " + err.str();
}

bool gitAvailable() { return runProcess({"git", "--version"}, true) == 0; }

// git, with enough settings to commit on a machine with no git configuration.
int gitIn(const fs::path& repo, std::vector<std::string> args) {
    std::vector<std::string> command{"git", "-C", repo.string(), "-c", "user.name=Test", "-c", "user.email=test@example.com",
                                     "-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false"};
    command.insert(command.end(), args.begin(), args.end());
    return runProcess(command, true);
}

std::string fileUrl(const fs::path& path) {
    std::string text = fs::absolute(path).generic_string();
    return "file://" + std::string(text[0] == '/' ? "" : "/") + text;
}

void makePackage(const fs::path& folder, const std::string& name, const std::string& version, const std::string& code,
                 const std::string& dependencies = "{}") {
    writeFile(folder / "ascend.json", "{\"name\": \"" + name + "\", \"version\": \"" + version +
                                          "\", \"main\": \"main.asc\", \"dependencies\": " + dependencies + "}");
    writeFile(folder / "main.asc", code);
}

}  // namespace

TEST(pkg_parses_package_specs) {
    fs::path base = fs::absolute("somewhere");

    PackageSource registry = parsePackageSpec("colors", base);
    CHECK(registry.kind == PackageSource::Kind::Registry);
    CHECK_EQ(registry.name, "colors");
    CHECK_EQ(registry.version, "");

    PackageSource pinned = parsePackageSpec("my-lib@1.2.0", base);
    CHECK(pinned.kind == PackageSource::Kind::Registry);
    CHECK_EQ(pinned.name, "my-lib");
    CHECK_EQ(pinned.version, "1.2.0");

    PackageSource github = parsePackageSpec("github:someone/tools/packages/fmt@v2.0", base);
    CHECK(github.kind == PackageSource::Kind::Git);
    CHECK_EQ(github.location, "https://github.com/someone/tools.git");
    CHECK_EQ(github.subdir, "packages/fmt");
    CHECK_EQ(github.ref, "v2.0");

    PackageSource git = parsePackageSpec("git+https://example.com/a/b.git@main#lib", base);
    CHECK(git.kind == PackageSource::Kind::Git);
    CHECK_EQ(git.location, "https://example.com/a/b.git");
    CHECK_EQ(git.ref, "main");
    CHECK_EQ(git.subdir, "lib");

    PackageSource ssh = parsePackageSpec("git@github.com:someone/repo.git", base);
    CHECK(ssh.kind == PackageSource::Kind::Git);
    CHECK_EQ(ssh.location, "git@github.com:someone/repo.git");
    CHECK_EQ(ssh.ref, "");

    PackageSource local = parsePackageSpec("./libs/thing", base);
    CHECK(local.kind == PackageSource::Kind::Local);
    CHECK_EQ(fs::path(local.location), (base / "libs" / "thing").lexically_normal());
    CHECK(parsePackageSpec("file:../x", base).kind == PackageSource::Kind::Local);

    PackageSource inRegistry = parsePackageSpec("registry:packages/colors", base);
    CHECK(inRegistry.kind == PackageSource::Kind::RegistryPath);
    CHECK_EQ(inRegistry.location, "packages/colors");

    auto rejects = [&](const std::string& spec, const std::string& fragment) {
        try {
            parsePackageSpec(spec, base);
        } catch (const std::exception& error) {
            return contains(error.what(), fragment);
        }
        return false;
    };
    CHECK(rejects("Colors", "isn't a valid package name"));
    CHECK(rejects("colors@", "missing a version"));
    CHECK(rejects("github:just-a-user", "should look like github:user/repo"));
    CHECK(rejects("git+https://example.com/repo.git@--upload-pack=evil", "isn't a valid tag or branch name"));
    CHECK(rejects("git+https://example.com/repo.git#../../etc", "isn't a folder inside the repository"));
    CHECK(rejects("registry:../outside", "isn't a folder inside the registry"));
    CHECK(rejects("   ", "empty package name"));
}

TEST(pkg_init_creates_a_project) {
    Sandbox sandbox;
    Outcome result = apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.init("demo"); });
    CHECK_EQ(result.code, 0);
    CHECK(contains(readFile(sandbox.project / "ascend.json"), "\"name\": \"demo\""));
    CHECK(contains(readFile(sandbox.project / ".gitignore"), "ascend_packages/"));
    CHECK_EQ(runAt(sandbox.project / "main.asc", readFile(sandbox.project / "main.asc")), "Hello from demo!\n");

    Outcome again = apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.init(""); });
    CHECK_EQ(again.code, 1);
    CHECK(contains(again.err, "already exists"));

    fs::path other = sandbox.root / "My Cool App";
    fs::create_directories(other);
    CHECK_EQ(apkg(sandbox.options(other), [](PackageManager& m) { return m.init(""); }).code, 0);
    CHECK(contains(readFile(other / "ascend.json"), "\"name\": \"my-cool-app\""));
}

TEST(pkg_installs_from_the_registry_into_a_project) {
    Sandbox sandbox;
    apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.init("app"); });

    // testing depends on colors, which gets installed with it.
    Outcome result = apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.install({"testing", "strings"}); });
    CHECK_EQ(result.err, "");
    CHECK_EQ(result.code, 0);
    CHECK(contains(result.out, "Installed testing 1.0.0\n"));
    CHECK(contains(result.out, "Installed colors 1.0.0 (needed by testing)\n"));
    CHECK(contains(result.out, "Installed strings 1.0.0\n"));
    CHECK(fs::is_regular_file(sandbox.project / "ascend_packages" / "colors" / "colors.asc"));
    CHECK(fs::is_regular_file(sandbox.project / "ascend_packages" / "testing" / ".apkg.json"));

    // Only the packages asked for are recorded.
    std::string manifest = readFile(sandbox.project / "ascend.json");
    CHECK(contains(manifest, "\"testing\": \"*\""));
    CHECK(contains(manifest, "\"strings\": \"*\""));
    CHECK(!contains(manifest, "\"colors\""));

    // Programs anywhere in the project can import them, and packages can import their dependencies.
    fs::path program = sandbox.project / "src" / "deep" / "main.asc";
    CHECK_EQ(runAt(program, "s := import(\"strings\")\nprint(s.snake_case(\"helloWorld\"))"), "hello_world\n");
    std::string tested = runAt(program,
                               "t := import(\"testing\")\n"
                               "t.test(\"adds\", func() { t.equal(1 + 1, 2) })\n"
                               "print(t.run())");
    CHECK(contains(tested, "  pass adds\n\n1 passed ("));
    CHECK(contains(tested, "s)\n1\n"));
    CHECK_EQ(runAt(program, "c := import(\"colors/colors\")\nprint(c.red(\"plain\"))"), "plain\n");

    Outcome again = apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.install({"colors"}); });
    CHECK_EQ(again.code, 0);
    CHECK(contains(again.out, "colors 1.0.0 is already installed"));
}

TEST(pkg_list_show_uninstall_and_reinstall) {
    Sandbox sandbox;
    apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.init("app"); });
    apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.install({"testing"}); });

    Outcome list = apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.list(); });
    CHECK_EQ(list.code, 0);
    CHECK(contains(list.out, "colors   1.0.0    colors\n"));
    CHECK(contains(list.out, "testing  1.0.0    testing\n"));

    Outcome show = apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.show("colors"); });
    CHECK_EQ(show.code, 0);
    CHECK(contains(show.out, "Name:        colors\n"));
    CHECK(contains(show.out, "Required by: testing\n"));

    Outcome search = apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.search("unit test"); });
    CHECK_EQ(search.code, 0);
    CHECK(contains(search.out, "testing  Write and run unit tests for Ascend code  [installed 1.0.0]"));
    CHECK(!contains(search.out, "strings"));

    Outcome removed = apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.uninstall({"testing"}); });
    CHECK_EQ(removed.code, 0);
    CHECK(contains(removed.out, "Uninstalled testing 1.0.0\n"));
    CHECK(contains(removed.out, "Removed testing from ascend.json\n"));
    CHECK(!fs::exists(sandbox.project / "ascend_packages" / "testing"));
    CHECK(!contains(readFile(sandbox.project / "ascend.json"), "testing"));

    Outcome notInstalled = apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.uninstall({"testing"}); });
    CHECK_EQ(notInstalled.code, 1);
    CHECK(contains(notInstalled.err, "testing isn't installed"));

    // `apkg install` with no names installs what ascend.json lists.
    removeTree(sandbox.project / "ascend_packages");
    writeFile(sandbox.project / "ascend.json",
              "{\"name\": \"app\", \"version\": \"1.0.0\", \"dependencies\": {\"strings\": \"1.0.0\", \"testing\": \"*\"}}");
    Outcome missing = apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.list(); });
    CHECK(contains(missing.out, "Not installed yet (run `apkg install`): strings testing"));
    Outcome restored = apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.install({}); });
    CHECK_EQ(restored.code, 0);
    CHECK(contains(restored.out, "Installed strings 1.0.0\n"));
    CHECK(contains(restored.out, "Installed colors 1.0.0 (needed by testing)\n"));
    CHECK(fs::is_directory(sandbox.project / "ascend_packages" / "testing"));
}

TEST(pkg_global_packages_work_outside_projects) {
    Sandbox sandbox;
    fs::path elsewhere = sandbox.root / "scripts";
    fs::create_directories(elsewhere);

    // Outside a project, packages go to ASCEND_HOME/packages.
    Outcome result = apkg(sandbox.options(elsewhere), [](PackageManager& m) { return m.install({"strings"}); });
    CHECK_EQ(result.code, 0);
    CHECK(fs::is_directory(sandbox.home / "packages" / "strings"));
    CHECK_EQ(runAt(elsewhere / "tool.asc", "print(import(\"strings\").title(\"global package\"))"), "Global Package\n");

    // --global skips the project even inside one.
    apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.init("app"); });
    Outcome global = apkg(sandbox.options(sandbox.project, true), [](PackageManager& m) { return m.install({"colors"}); });
    CHECK_EQ(global.code, 0);
    CHECK(fs::is_directory(sandbox.home / "packages" / "colors"));
    CHECK(!fs::exists(sandbox.project / "ascend_packages"));
    CHECK(!contains(readFile(sandbox.project / "ascend.json"), "colors"));

    // A project's own copy wins over the user's.
    fs::path local = sandbox.root / "my-strings";
    makePackage(local, "strings", "9.0.0", "string title(string s) { return \"project copy\" }\n");
    apkg(sandbox.options(sandbox.project), [&](PackageManager& m) { return m.install({local.string()}); });
    CHECK_EQ(runAt(sandbox.project / "main.asc", "print(import(\"strings\").title(\"x\"))"), "project copy\n");
    CHECK_EQ(runAt(elsewhere / "tool.asc", "print(import(\"strings\").title(\"x\"))"), "X\n");
}

TEST(pkg_installs_local_folders_with_dependencies) {
    Sandbox sandbox;
    apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.init("app"); });

    // shapes depends on a folder next to it and on a registry package.
    makePackage(sandbox.root / "libs" / "units", "units", "0.1.0", "f64 cm(f64 inches) { return inches * 2.5 }\n");
    makePackage(sandbox.root / "libs" / "shapes", "shapes", "0.3.0",
                "units := import(\"units\")\nstrings := import(\"strings\")\n"
                "string describe(f64 side) { return strings.capitalize(f\"square of {units.cm(side)} cm\") }\n",
                "{\"units\": \"file:../units\", \"strings\": \"*\"}");

    Outcome result = apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.install({"../libs/shapes"}); });
    CHECK_EQ(result.err, "");
    CHECK_EQ(result.code, 0);
    CHECK(contains(result.out, "Installed shapes 0.3.0\n"));
    CHECK(contains(result.out, "Installed units 0.1.0 (needed by shapes)\n"));
    CHECK(contains(result.out, "Installed strings 1.0.0 (needed by shapes)\n"));
    CHECK(contains(readFile(sandbox.project / "ascend.json"), "\"shapes\": \"file:../libs/shapes\""));
    CHECK_EQ(runAt(sandbox.project / "main.asc", "print(import(\"shapes\").describe(10))"), "Square of 25.0 cm\n");

    // Local folders are reinstalled every time, to pick up changes.
    makePackage(sandbox.root / "libs" / "units", "units", "0.1.0", "f64 cm(f64 inches) { return 0.0 }\n");
    Outcome again = apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.install({"../libs/units"}); });
    CHECK(contains(again.out, "Reinstalled units 0.1.0\n"));
    CHECK_EQ(runAt(sandbox.project / "main.asc", "print(import(\"shapes\").describe(10))"), "Square of 0.0 cm\n");
}

TEST(pkg_reports_problems_clearly) {
    Sandbox sandbox;
    apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.init("app"); });
    auto install = [&](const std::string& spec) {
        return apkg(sandbox.options(sandbox.project), [&](PackageManager& m) { return m.install({spec}); });
    };

    Outcome unknown = install("no-such-package");
    CHECK_EQ(unknown.code, 1);
    CHECK(contains(unknown.err, "there's no package named 'no-such-package' in the registry"));

    Outcome version = install("colors@99.0.0");
    CHECK_EQ(version.code, 1);
    CHECK(contains(version.err, "colors 99.0.0 isn't available (the registry has version 1.0.0)"));

    fs::path folder = sandbox.root / "not-a-package";
    fs::create_directories(folder);
    Outcome noManifest = install(folder.string());
    CHECK_EQ(noManifest.code, 1);
    CHECK(contains(noManifest.err, "there is no ascend.json in"));

    writeFile(folder / "ascend.json", "{\"name\": \"x\", ");
    CHECK(contains(install(folder.string()).err, "isn't valid JSON"));

    writeFile(folder / "ascend.json", "{\"name\": \"Bad Name\", \"version\": \"1.0\"}");
    CHECK(contains(install(folder.string()).err, "isn't a valid package name"));

    writeFile(folder / "ascend.json", "{\"name\": \"x\", \"version\": \"1.0\", \"main\": \"../../evil.asc\"}");
    CHECK(contains(install(folder.string()).err, "should be a file inside the package"));

    writeFile(folder / "ascend.json", "{\"name\": \"x\", \"version\": \"1.0\", \"main\": \"missing.asc\"}");
    CHECK(contains(install(folder.string()).err, "its main file 'missing.asc' is missing"));

    Outcome missingFolder = install("./nowhere");
    CHECK_EQ(missingFolder.code, 1);
    CHECK(contains(missingFolder.err, "doesn't exist"));
    CHECK(!fs::exists(sandbox.project / "ascend_packages" / "x"));

    // Nothing half-installed is left behind.
    std::error_code ignored;
    for (const auto& entry : fs::directory_iterator(sandbox.project / "ascend_packages", ignored)) {
        CHECK(entry.path().filename().string().rfind(".apkg-tmp", 0) != 0);
    }

    // Importing a package that isn't installed, or is broken, says so.
    CHECK(contains(runAt(sandbox.project / "main.asc", "import(\"colors\")"),
                   "no installed package named 'colors' (to install it, run: apkg install colors)"));
    writeFile(sandbox.project / "ascend_packages" / "broken" / "ascend.json", "{\"name\": \"broken\", \"version\": \"1\"}");
    CHECK(contains(runAt(sandbox.project / "main.asc", "import(\"broken\")"), "the package 'broken' has no file 'main.asc'"));
    CHECK(contains(runAt(sandbox.project / "main.asc", "import(\"broken/../../x\")"), "isn't a file inside the package"));
}

TEST(pkg_installs_from_git_repositories) {
    if (!gitAvailable()) return;  // nothing to test without git
    Sandbox sandbox;
    apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.init("app"); });

    // A repository with two tagged versions and a second package in a folder.
    fs::path repo = sandbox.root / "repo";
    makePackage(repo, "greet", "1.0.0", "string hello(string name) { return \"Hello, \" + name }\n");
    makePackage(repo / "extra", "extra", "0.1.0", "ANSWER := 42\n");
    CHECK_EQ(gitIn(repo, {"init", "-q"}), 0);
    CHECK_EQ(gitIn(repo, {"add", "-A"}), 0);
    CHECK_EQ(gitIn(repo, {"commit", "-q", "-m", "first"}), 0);
    CHECK_EQ(gitIn(repo, {"tag", "v1.0.0"}), 0);
    makePackage(repo, "greet", "2.0.0", "string hello(string name) { return \"Hi, \" + name }\n");
    CHECK_EQ(gitIn(repo, {"commit", "-q", "-a", "-m", "second"}), 0);
    CHECK_EQ(gitIn(repo, {"tag", "v2.0.0"}), 0);
    std::string url = fileUrl(repo);

    Outcome tagged = apkg(sandbox.options(sandbox.project), [&](PackageManager& m) { return m.install({"git+" + url + "@v1.0.0"}); });
    CHECK_EQ(tagged.code, 0);
    CHECK(contains(tagged.out, "Installed greet 1.0.0\n"));
    CHECK(!fs::exists(sandbox.project / "ascend_packages" / "greet" / ".git"));
    CHECK_EQ(runAt(sandbox.project / "main.asc", "print(import(\"greet\").hello(\"Ada\"))"), "Hello, Ada\n");
    CHECK(contains(readFile(sandbox.project / "ascend.json"), "\"greet\": \"git+" + url + "@v1.0.0\""));
    Outcome show = apkg(sandbox.options(sandbox.project), [](PackageManager& m) { return m.show("greet"); });
    CHECK(contains(show.out, "Commit:      "));

    Outcome latest = apkg(sandbox.options(sandbox.project), [&](PackageManager& m) { return m.install({"git+" + url}); });
    CHECK(contains(latest.out, "Upgraded greet 2.0.0 (was 1.0.0)\n"));
    CHECK_EQ(runAt(sandbox.project / "main.asc", "print(import(\"greet\").hello(\"Ada\"))"), "Hi, Ada\n");

    Outcome folder = apkg(sandbox.options(sandbox.project), [&](PackageManager& m) { return m.install({"git+" + url + "#extra"}); });
    CHECK_EQ(folder.code, 0);
    CHECK_EQ(runAt(sandbox.project / "main.asc", "print(import(\"extra\").ANSWER)"), "42\n");

    Outcome badTag = apkg(sandbox.options(sandbox.project), [&](PackageManager& m) { return m.install({"git+" + url + "@v9"}); });
    CHECK_EQ(badTag.code, 1);
    CHECK(contains(badTag.err, "check the address and that the tag or branch exists"));

    // A registry kept in git: packages in their own repositories are found by version tag.
    fs::path registry = sandbox.root / "registry";
    writeFile(registry / "registry" / "index.json",
              "{\"packages\": {\"greet\": {\"description\": \"Says hello\", \"source\": \"git+" + url + "\"}}}");
    CHECK_EQ(gitIn(registry, {"init", "-q"}), 0);
    CHECK_EQ(gitIn(registry, {"add", "-A"}), 0);
    CHECK_EQ(gitIn(registry, {"commit", "-q", "-m", "registry"}), 0);
    PackageOptions options = sandbox.options(sandbox.root / "elsewhere");
    fs::create_directories(options.workingDirectory);
    options.registry = fileUrl(registry);

    Outcome pinned = apkg(options, [](PackageManager& m) { return m.install({"greet@1.0.0"}); });
    CHECK_EQ(pinned.err, "");
    CHECK(contains(pinned.out, "Downloading the package registry...\n"));
    CHECK(contains(pinned.out, "Installed greet 1.0.0\n"));
    Outcome search = apkg(options, [](PackageManager& m) { return m.search("hello"); });
    CHECK(contains(search.out, "greet  Says hello  [installed 1.0.0]"));
    CHECK(!contains(search.out, "Downloading"));  // the cached copy is updated, not downloaded again
    options.upgrade = true;
    Outcome upgraded = apkg(options, [](PackageManager& m) { return m.install({"greet"}); });
    CHECK(contains(upgraded.out, "Upgraded greet 2.0.0 (was 1.0.0)\n"));
}
