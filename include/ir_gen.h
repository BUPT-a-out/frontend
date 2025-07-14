#pragma once

#include <memory>

namespace midend {
class Module;
}

std::unique_ptr<midend::Module> generate_test_module();
