#ifndef SYMBOLTABLE_HPP
#define SYMBOLTABLE_HPP

#include <unordered_map>
#include <string>
#include <iostream>

class SymbolTable {
public:
    inline void addSymbol(const std::string &name, int value);
    inline bool hasSymbol(const std::string &name) const;
    inline int getValue(const std::string &name) const;
    inline void printSymbols() const;

private:
    std::unordered_map<std::string, int> symbols;
};

inline void SymbolTable::addSymbol(const std::string &name, int value) {
    symbols[name] = value;
}

inline bool SymbolTable::hasSymbol(const std::string &name) const {
    return symbols.find(name) != symbols.end();
}

inline int SymbolTable::getValue(const std::string &name) const {
    auto it = symbols.find(name);
    if (it != symbols.end()) {
        return it->second;
    }
    throw std::runtime_error("Symbol not found: " + name);
}

inline void SymbolTable::printSymbols() const {
    for (const auto &entry : symbols) {
        std::cout << entry.first << " = " << entry.second << std::endl;
    }
}

#endif // SYMBOLTABLE_HPP
