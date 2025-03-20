#ifndef SYMBOLTABLE_HPP
#define SYMBOLTABLE_HPP

#include <unordered_map>
#include <string>
#include <iostream>

class SymbolTable {
public:
    void addSymbol(const std::string &name);
    bool hasSymbol(const std::string &name);
    void printSymbols();

private:
    std::unordered_map<std::string, int> symbols;
};

void SymbolTable::addSymbol(const std::string &name) {
    symbols[name] = 0;  // For now, just store 0 as a placeholder for the symbol's value
}

bool SymbolTable::hasSymbol(const std::string &name) {
    return symbols.find(name) != symbols.end();
}

void SymbolTable::printSymbols() {
    for (const auto &entry : symbols) {
        std::cout << entry.first << std::endl;
    }
}

#endif // SYMBOLTABLE_HPP
