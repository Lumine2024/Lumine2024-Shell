#pragma once

#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace lumine_sh {

bool is_builtin_function(const std::wstring &name);
void invoke_builtin(const std::wstring &name, const std::vector<std::wstring> &arg_tokens);
void invoke_builtin(
    const std::wstring &name,
    const std::vector<std::wstring> &arg_tokens,
    std::wistream &input,
    std::wostream &output
);

inline const std::vector<std::wstring> all_functions = {
    L"Set-Location",
    L"Write-Output",
    L"exit"
};

}
