#ifndef ASCEND_TYPECHECK_HPP
#define ASCEND_TYPECHECK_HPP

#include "types.hpp"
#include "value.hpp"

#include <string>

namespace ascend {

// Why a value can't be stored in a typed variable, parameter or return value.
struct CoerceFailure {
    std::string what;  // the offending value, e.g. "a string" or "300"
    std::string hint;  // extra advice, e.g. "u8 is 0 to 255" (may be empty)
};

// Converts `value` so it can be stored with type `type`:
//   - any number converts to another numeric type if no information is lost
//     (300 doesn't fit in a u8, and 2.5 can't become an i32 implicitly)
//   - a char converts to a string
//   - null is only allowed for nullable types like `string?`
// Lists, maps and functions are not converted. Returns false on failure.
bool coerceToType(const Value& value, const TypeSpec& type, Value& out, CoerceFailure& failure);

// Checks (and converts) a value being added to a list<T> or stored in a
// map<K, V>. Throws RuntimeError if it doesn't fit; does nothing for
// untyped lists and maps.
Value checkListItem(const ListObj& list, Value value);
void checkMapEntry(const MapObj& map, const Value& key, Value& value);

// "a string", "300", "null", "'a'"... for error messages.
std::string describeValue(const Value& value);

// Appends " (hint)" if there is a hint.
std::string withHint(const std::string& message, const CoerceFailure& failure);

}  // namespace ascend

#endif  // ASCEND_TYPECHECK_HPP
