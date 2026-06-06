#include "builtin_functions.hpp"

#include "types.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace std;
using namespace lumine_sh;
namespace fs = filesystem;

bool lumine_sh::is_builtin_function(const wstring &name) {
    for(const auto &fn : all_functions) {
        if(fn == name) {
            return true;
        }
    }
    return false;
}

void lumine_sh::invoke_builtin(const wstring &name, const vector<wstring> &arg_tokens) {
    invoke_builtin(name, arg_tokens, wcin, wcout);
}

void lumine_sh::invoke_builtin(
    const wstring &name,
    const vector<wstring> &arg_tokens,
    wistream &input,
    wostream &output
) {
    (void)input;
    auto value = evaluate_expression(arg_tokens);
    if(name == L"Write-Output") {
        output << value->stringify() << L'\n' << flush;
        return;
    }
    if(name == L"Set-Location") {
        fs::current_path(fs::path(value->stringify()));
        return;
    }
    if(name == L"exit") {
        exit(0);
    }
    throw runtime_error("unknown builtin");
}
