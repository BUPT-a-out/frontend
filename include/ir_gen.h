#pragma once

#include <memory>

namespace midend {
class Module;
}

// Integrate generator
std::unique_ptr<midend::Module> generate_IR(FILE* file_in);
