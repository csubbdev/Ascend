// apkg: the Ascend package manager.

#include "package_manager.hpp"
#include "runner.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

void printUsage(std::ostream& out) {
    out << "apkg - the Ascend package manager\n"
           "\n"
           "Usage:\n"
           "  apkg install <package>...    install packages (and what they depend on)\n"
           "  apkg install                 install everything the project's ascend.json lists\n"
           "  apkg uninstall <package>...  remove packages\n"
           "  apkg list                    show the installed packages\n"
           "  apkg show <package>          show details about an installed package\n"
           "  apkg search [text]           find packages in the registry\n"
           "  apkg init [name]             make the current folder a project (writes ascend.json)\n"
           "\n"
           "Options:\n"
           "  -g, --global    use your own packages folder, even inside a project\n"
           "  -U, --upgrade   get the newest version of packages that are already installed\n"
           "  -h, --help      show this help\n"
           "  -v, --version   show the version\n"
           "\n"
           "Packages can come from:\n"
           "  colors                        the registry (the newest version)\n"
           "  colors@1.0.0                  the registry (a particular version)\n"
           "  github:user/repo              a GitHub repository (add @tag, or /folder for a folder in it)\n"
           "  git+https://host/repo.git     any git repository (add @tag, or #folder for a folder in it)\n"
           "  ./path/to/folder              a folder on this computer\n"
           "\n"
           "Inside a project (a folder with an ascend.json) packages are installed into its\n"
           "ascend_packages folder and recorded in ascend.json. Anywhere else they're\n"
           "installed for you, in ~/.ascend/packages. Programs use them with: import colors\n"
           "\n"
           "Environment:\n"
           "  ASCEND_HOME      where your packages and downloads are kept (default ~/.ascend)\n"
           "  ASCEND_REGISTRY  the registry to use: a git address or a folder\n"
           "                   (default " << ascend::kDefaultRegistry << ")\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    ascend::PackageOptions options;
    try {
        options = ascend::defaultPackageOptions();
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }

    std::string command;
    std::vector<std::string> operands;
    bool flagsDone = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (!flagsDone && arg.size() > 1 && arg[0] == '-') {
            if (arg == "--") {
                flagsDone = true;
            } else if (arg == "-g" || arg == "--global") {
                options.global = true;
            } else if (arg == "-U" || arg == "--upgrade") {
                options.upgrade = true;
            } else if (arg == "-h" || arg == "--help") {
                printUsage(std::cout);
                return 0;
            } else if (arg == "-v" || arg == "--version") {
                std::cout << "apkg " << ascend::kVersion << " (Ascend " << ascend::kVersion << ")\n";
                return 0;
            } else {
                std::cerr << "error: unknown option '" << arg << "' (run `apkg --help` to see the options)\n";
                return 1;
            }
        } else if (command.empty()) {
            command = arg;
        } else {
            operands.push_back(arg);
        }
    }

    if (command.empty() || command == "help") {
        printUsage(command.empty() ? std::cerr : std::cout);
        return command.empty() ? 1 : 0;
    }

    auto expectAtMost = [&](size_t count) {
        if (operands.size() <= count) return true;
        std::cerr << "error: too many arguments for `apkg " << command << "`\n";
        return false;
    };

    ascend::PackageManager manager(options, std::cout, std::cerr);
    if (command == "install" || command == "add" || command == "i") return manager.install(operands);
    if (command == "uninstall" || command == "remove" || command == "rm") return manager.uninstall(operands);
    if (command == "list" || command == "ls") return expectAtMost(0) ? manager.list() : 1;
    if (command == "search") return expectAtMost(1) ? manager.search(operands.empty() ? "" : operands[0]) : 1;
    if (command == "init") return expectAtMost(1) ? manager.init(operands.empty() ? "" : operands[0]) : 1;
    if (command == "show" || command == "info") {
        if (operands.size() != 1) {
            std::cerr << "error: say which package to show, like: apkg show colors\n";
            return 1;
        }
        return manager.show(operands[0]);
    }

    std::cerr << "error: unknown command '" << command << "' (run `apkg --help` to see the commands)\n";
    return 1;
}
