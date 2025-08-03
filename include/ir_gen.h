#pragma once

#include <cstdio>
#include <memory>

namespace midend {
class Module;
}

// Integrate generator
std::unique_ptr<midend::Module> generate_IR(
    FILE* file_in, bool enable_mangle_c_std_symbol = true);
