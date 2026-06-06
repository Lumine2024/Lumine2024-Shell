#include "parser.hpp"
#include "types.hpp"
#include "utils.hpp"

#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace lumine_sh;
namespace fs = filesystem;

namespace lumine_sh {

unordered_map<wstring, shared_ptr<BasicShellType>> variables;

}

static bool is_control_header(const wstring &line) {
    return starts_with(line, L"if ") || starts_with(line, L"while ");
}

static bool is_if_header(const wstring &line) {
    return starts_with(line, L"if ");
}

static bool is_chain_header(const wstring &line) {
    return starts_with(line, L"elif ") || line == L"else {";
}

static int brace_delta(const wstring &line) {
    if(trim(line) == L"}") {
        return -1;
    }
    if(!line.empty() && line.back() == L'{') {
        return 1;
    }
    return 0;
}

static vector<wstring> read_script_lines(const fs::path &path) {
    ifstream input(path);
    if(!input.is_open()) {
        throw runtime_error("failed to open script file");
    }
    vector<wstring> lines;
    string line;
    while(getline(input, line)) {
        if(!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(widen_ascii(line));
    }
    return lines;
}

static void execute_program(const ExecutionList &program) {
    for(const auto &command : program) {
        if(command != nullptr) {
            command->execute();
        }
    }
}

static optional<wstring> read_line_or_eof(bool continuation) {
    wcout << widen_ascii(fs::current_path().string()) << (continuation ? L" ... " : L"> ") << flush;
    string line;
    if(!getline(cin, line)) {
        return nullopt;
    }
    return widen_ascii(line);
}

static vector<wstring> collect_interactive_command(optional<wstring> &pending_line) {
    vector<wstring> collected;
    wstring first_line;
    if(pending_line.has_value()) {
        first_line = *pending_line;
        pending_line.reset();
    } else {
        const auto line = read_line_or_eof(false);
        if(!line.has_value()) {
            return {};
        }
        first_line = *line;
    }

    first_line = trim(first_line);
    if(first_line.empty()) {
        return {};
    }
    collected.push_back(first_line);
    if(!is_control_header(first_line)) {
        return collected;
    }

    int depth = brace_delta(first_line);
    while(true) {
        if(depth > 0) {
            const auto next = read_line_or_eof(true);
            if(!next.has_value()) {
                throw runtime_error("unexpected eof inside block");
            }
            const wstring trimmed = trim(*next);
            if(trimmed.empty()) {
                continue;
            }
            collected.push_back(trimmed);
            depth += brace_delta(trimmed);
            continue;
        }

        if(!is_if_header(collected.front())) {
            break;
        }

        const auto next = read_line_or_eof(true);
        if(!next.has_value()) {
            break;
        }
        const wstring trimmed = trim(*next);
        if(trimmed.empty()) {
            break;
        }
        if(is_chain_header(trimmed)) {
            collected.push_back(trimmed);
            depth += brace_delta(trimmed);
            continue;
        }
        pending_line = trimmed;
        break;
    }

    return collected;
}

static int run_shell(int argc, char **argv) {
    if(argc > 1) {
        execute_program(parse_script(read_script_lines(argv[1])));
        return 0;
    }

    wcout << L"============ Lumine2024 Shell ============\n";
    wcout << L"Copyright (c) Lumine2024. Licensed under MIT License.\n\n";

    optional<wstring> pending_line;
    while(true) {
        try {
            const auto command_lines = collect_interactive_command(pending_line);
            if(command_lines.empty()) {
                if(!cin.good()) {
                    break;
                }
                continue;
            }
            execute_program(parse_script(command_lines));
        } catch(const exception &ex) {
            cerr << "[error] " << ex.what() << '\n';
            if(!cin.good()) {
                break;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    try {
        return run_shell(argc, argv);
    } catch(const exception &ex) {
        cerr << "[fatal] " << ex.what() << '\n';
        return 1;
    }
}
