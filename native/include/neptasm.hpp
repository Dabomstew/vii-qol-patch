#pragma once
#include "patch.hpp"

namespace vii {
// Opt-in compatibility layer for the features formerly supplied by neptasm.
// The installer is deliberately separate from the existing loading patches so
// it can share this proxy without changing their defaults.
bool InstallNeptasm(const Context& context);
}
