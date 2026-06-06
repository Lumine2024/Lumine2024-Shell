#pragma once

#include "execution.hpp"

#include <memory>
#include <string>
#include <vector>

namespace lumine_sh {

std::shared_ptr<BasicExecution> parse_command(const std::wstring &command);
std::vector<std::shared_ptr<BasicExecution>> parse_script(const std::vector<std::wstring> &commands);
std::vector<std::wstring> tokenize_command(const std::wstring &command);

}
