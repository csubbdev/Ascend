#ifndef ASCEND_REPL_HPP
#define ASCEND_REPL_HPP

#include "interpreter.hpp"

#include <istream>
#include <ostream>

namespace ascend {

// Interactive read-eval-print loop. Statements that span several lines (an
// open brace, an unfinished expression, ...) are continued on a "..." prompt.
// Returns the process exit code.
int runRepl(Interpreter& interpreter, std::istream& in, std::ostream& out, std::ostream& err);

}  // namespace ascend

#endif  // ASCEND_REPL_HPP
