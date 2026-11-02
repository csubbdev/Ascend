#ifndef ASCEND_ENVIRONMENT_HPP
#define ASCEND_ENVIRONMENT_HPP

#include "value.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ascend {

// One scope of variables. Scopes form a chain through their parent: lookups
// walk outward until the name is found.
class Environment final : public GcObject {
public:
    explicit Environment(std::shared_ptr<Environment> parent = nullptr) : parent_(std::move(parent)) {}

    // Creates (or replaces) a variable in this scope.
    void define(const std::string& name, Value value) { values_[name] = std::move(value); }

    // Finds a variable in this scope or any enclosing one. The pointer is only
    // valid until the next variable is defined.
    Value* find(const std::string& name) {
        for (Environment* env = this; env != nullptr; env = env->parent_.get()) {
            auto it = env->values_.find(name);
            if (it != env->values_.end()) return &it->second;
        }
        return nullptr;
    }

    // All names visible from this scope (used for "did you mean ...?" hints).
    void collectNames(std::vector<std::string>& out) const {
        for (const Environment* env = this; env != nullptr; env = env->parent_.get()) {
            for (const auto& entry : env->values_) out.push_back(entry.first);
        }
    }

    void forEachChild(const std::function<void(GcObject*)>& visit) const override {
        if (parent_) visit(parent_.get());
        for (const auto& entry : values_) {
            if (GcObject* child = entry.second.gcObject()) visit(child);
        }
    }

    void clearReferences() override {
        values_.clear();
        parent_.reset();
    }

private:
    std::unordered_map<std::string, Value> values_;
    std::shared_ptr<Environment> parent_;
};

}  // namespace ascend

#endif  // ASCEND_ENVIRONMENT_HPP
