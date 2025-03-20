#include <iostream>
#include <string>
#include <unordered_map>
#include <cctype>
#include <fstream>
#include "include/lexer.hpp"
#include "include/symboltable.hpp"
#include "include/parser.hpp"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <filename>" << std::endl;
        return 1;
    }

    std::ifstream file(argv[1]);
    if (!file.is_open()) {
        std::cerr << "Error opening file: " << argv[1] << std::endl;
        return 1;
    }

    std::string code((std::istreambuf_iterator<char>(file)), (std::istreambuf_iterator<char>()));
    file.close();
    
    Lexer lexer(code);
    SymbolTable symbolTable;
    Parser parser(lexer, symbolTable);
    parser.parse();

    return 0;
}
