#ifndef SYMBOLTABLE_HPP
#define SYMBOLTABLE_HPP

#include <unordered_map>
#include <string>
#include <iostream>
#include <vector>

class SymbolTable {
public:
    SymbolTable() { scopes.emplace_back(); }
    
    void enterScope() { scopes.emplace_back(); }
    void exitScope() { if (scopes.size() > 1) scopes.pop_back(); }
    
    void addSymbol(const std::string &name, int value) { 
        scopes.back()[name] = value; // enough to add/update the symbol
    }
    
    bool hasSymbol(const std::string &name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            if (it->find(name) != it->end()) return true;
        return false;
    }

    int getValue(const std::string &name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto entry = it->find(name);
            if (entry != it->end()) return entry->second;
        }
        throw std::runtime_error("Symbol not found: " + name);
    }

    void updateSymbol(const std::string &name, int value) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto entry = it->find(name);
            if (entry != it->end()) { 
                entry->second = value; 
                return; 
            }
        }
        throw std::runtime_error("Symbol not found: " + name);
    }

    void printSymbols() const {
        for (const auto &scope : scopes) {
            for (const auto &entry : scope) {
                std::cout << entry.first << " = " << entry.second << std::endl;
            }
        }
    }

private:
    std::vector<std::unordered_map<std::string, int>> scopes;
};

#endif // SYMBOLTABLE_HPP