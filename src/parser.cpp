#include "parser.hpp"

#include "builtin_functions.hpp"
#include "types.hpp"
#include "utils.hpp"

#include <cwctype>
#include <optional>
#include <stdexcept>

using namespace std;
using namespace lumine_sh;

static bool is_identifier_char(wchar_t ch) {
    return iswalnum(ch) || ch == L'_';
}

static bool is_assignment_line(const wstring &line) {
    return !line.empty() && line[0] == L'$';
}

static bool is_redirect_operator(const wstring &token) {
    return token == L"<" || token == L">" || token == L"2>" || token == L"&>";
}

static bool is_quoted_string_token(const wstring &token) {
    return token.size() >= 2 && token.front() == L'"' && token.back() == L'"';
}

static bool is_if_header(const wstring &line) {
    return starts_with(line, L"if ") && ends_with(line, L"{");
}

static bool is_elif_header(const wstring &line) {
    return starts_with(line, L"elif ") && ends_with(line, L"{");
}

static bool is_else_header(const wstring &line) {
    return line == L"else {";
}

static bool is_while_header(const wstring &line) {
    return starts_with(line, L"while ") && ends_with(line, L"{");
}

static wstring extract_condition(const wstring &line, const wstring &keyword) {
    if(!ends_with(line, L"{")) {
        throw runtime_error("control header must end with {");
    }
    const wstring middle = trim(line.substr(keyword.size(), line.size() - keyword.size() - 1));
    if(middle.empty()) {
        throw runtime_error("missing condition");
    }
    return middle;
}

static pair<wstring, optional<wstring>> parse_assignment_lhs(const wstring &lhs) {
    if(lhs.empty() || lhs[0] != L'$') {
        throw runtime_error("invalid assignment lhs");
    }
    const size_t type_begin = lhs.find(L'[');
    const size_t type_end = lhs.find(L']');
    if(type_begin == wstring::npos && type_end == wstring::npos) {
        const wstring name = lhs.substr(1);
        if(name.empty()) {
            throw runtime_error("missing variable name");
        }
        for(wchar_t ch : name) {
            if(!is_identifier_char(ch)) {
                throw runtime_error("invalid variable name");
            }
        }
        return {name, nullopt};
    }
    if(type_begin == wstring::npos || type_end == wstring::npos || type_end <= type_begin + 1 || type_end != lhs.size() - 1) {
        throw runtime_error("invalid typed assignment lhs");
    }
    const wstring name = lhs.substr(1, type_begin - 1);
    if(name.empty()) {
        throw runtime_error("missing variable name");
    }
    for(wchar_t ch : name) {
        if(!is_identifier_char(ch)) {
            throw runtime_error("invalid variable name");
        }
    }
    const wstring type_name = lhs.substr(type_begin + 1, type_end - type_begin - 1);
    if(!is_supported_type(type_name)) {
        throw runtime_error("unsupported variable type");
    }
    return {name, type_name};
}

static wstring strip_outer_quotes(const wstring &token) {
    if(token.size() >= 2 && token.front() == L'"' && token.back() == L'"') {
        return token.substr(1, token.size() - 2);
    }
    return token;
}

static shared_ptr<BasicExecution> parse_external_command(const vector<wstring> &tokens) {
    RedirectIo redirect_io;
    vector<wstring> argv;

    auto parse_redirect_target = [&](size_t &index) -> wstring {
        if(index + 1 >= tokens.size() || is_redirect_operator(tokens[index + 1])) {
            throw runtime_error("invalid command: invalid redirect IO");
        }
        ++index;
        return strip_outer_quotes(tokens[index]);
    };

    for(size_t i = 0; i < tokens.size(); ++i) {
        if(tokens[i] == L"<") {
            redirect_io.input_file = parse_redirect_target(i);
            continue;
        }
        if(tokens[i] == L">") {
            redirect_io.output_file = parse_redirect_target(i);
            continue;
        }
        if(tokens[i] == L"2>") {
            redirect_io.error_file = parse_redirect_target(i);
            continue;
        }
        if(tokens[i] == L"&>") {
            const wstring target = parse_redirect_target(i);
            redirect_io.output_file = target;
            redirect_io.error_file = target;
            continue;
        }
        argv.push_back(strip_outer_quotes(tokens[i]));
    }

    if(argv.empty()) {
        throw runtime_error("invalid command: missing program name");
    }

    return make_shared<InvokeExternalExecution>(move(argv), move(redirect_io));
}

static vector<wstring> split_pipeline_segments(const wstring &command) {
    vector<wstring> segments;
    wstring current;
    bool in_quotes = false;

    for(wchar_t ch : command) {
        if(ch == L'"') {
            current.push_back(ch);
            in_quotes = !in_quotes;
            continue;
        }
        if(!in_quotes && ch == L'|') {
            const wstring segment = trim(current);
            if(segment.empty()) {
                throw runtime_error("invalid pipeline: empty stage");
            }
            segments.push_back(segment);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }

    if(in_quotes) {
        throw runtime_error("unterminated string literal");
    }

    const wstring tail = trim(current);
    if(tail.empty()) {
        if(!segments.empty()) {
            throw runtime_error("invalid pipeline: empty stage");
        }
        return {};
    }
    if(!segments.empty()) {
        segments.push_back(tail);
    }
    return segments;
}

static shared_ptr<PipeableExecution> parse_pipeline_stage(const wstring &segment) {
    const wstring line = trim(segment);
    if(line.empty()) {
        throw runtime_error("invalid pipeline: empty stage");
    }
    if(is_assignment_line(line) || is_if_header(line) || is_while_header(line) ||
       line == L"}" || is_elif_header(line) || is_else_header(line)) {
        throw runtime_error("only builtin and external commands can appear in pipeline");
    }

    const auto tokens = tokenize_command(line);
    if(tokens.empty()) {
        throw runtime_error("invalid pipeline: empty stage");
    }

    if(tokens.size() == 1 && is_quoted_string_token(tokens.front())) {
        return make_shared<FunctionCallExecution>(L"Write-Output", vector<wstring>{tokens.front()});
    }

    if(is_builtin_function(tokens.front())) {
        for(const auto &token : tokens) {
            if(is_redirect_operator(token)) {
                throw runtime_error("builtin redirection is unsupported");
            }
        }
        vector<wstring> arg_tokens(tokens.begin() + 1, tokens.end());
        return make_shared<FunctionCallExecution>(tokens.front(), move(arg_tokens));
    }

    return static_pointer_cast<PipeableExecution>(parse_external_command(tokens));
}

static ExecutionList parse_block(
    const vector<wstring> &commands,
    size_t &index
);

static shared_ptr<BasicExecution> parse_if_chain(
    const vector<wstring> &commands,
    size_t &index
) {
    vector<ConditionalBranch> branches;
    ExecutionList else_commands;

    wstring header = trim(commands[index++]);
    branches.push_back({
        tokenize_command(extract_condition(header, L"if")),
        parse_block(commands, index)
    });

    while(index < commands.size()) {
        const wstring next = trim(commands[index]);
        if(is_elif_header(next)) {
            ++index;
            branches.push_back({
                tokenize_command(extract_condition(next, L"elif")),
                parse_block(commands, index)
            });
            continue;
        }
        if(is_else_header(next)) {
            ++index;
            else_commands = parse_block(commands, index);
        }
        break;
    }

    return make_shared<IfStatementExecution>(branches, else_commands);
}

static shared_ptr<BasicExecution> parse_while(
    const vector<wstring> &commands,
    size_t &index
) {
    const wstring header = trim(commands[index++]);
    const auto condition_tokens = tokenize_command(extract_condition(header, L"while"));
    auto body = parse_block(commands, index);
    return make_shared<WhileStatementExecution>(condition_tokens, body);
}

static ExecutionList parse_block(
    const vector<wstring> &commands,
    size_t &index
) {
    ExecutionList body;
    while(index < commands.size()) {
        const wstring line = trim(commands[index]);
        if(line.empty()) {
            ++index;
            continue;
        }
        if(line == L"}") {
            ++index;
            return body;
        }
        if(is_elif_header(line) || is_else_header(line)) {
            return body;
        }
        if(is_if_header(line)) {
            body.push_back(parse_if_chain(commands, index));
            continue;
        }
        if(is_while_header(line)) {
            body.push_back(parse_while(commands, index));
            continue;
        }
        body.push_back(parse_command(line));
        ++index;
    }
    throw runtime_error("missing closing brace");
}

vector<wstring> lumine_sh::tokenize_command(const wstring &command) {
    vector<wstring> tokens;
    wstring current;
    bool in_quotes = false;
    for(wchar_t ch : command) {
        if(ch == L'"') {
            current.push_back(ch);
            in_quotes = !in_quotes;
            continue;
        }
        if(!in_quotes && iswspace(ch)) {
            if(!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if(in_quotes) {
        throw runtime_error("unterminated string literal");
    }
    if(!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

shared_ptr<BasicExecution> lumine_sh::parse_command(const wstring &command) {
    const wstring line = trim(command);
    if(line.empty()) {
        return nullptr;
    }
    if(line == L"}" || is_elif_header(line) || is_else_header(line)) {
        throw runtime_error("unexpected block control line");
    }
    if(is_if_header(line) || is_while_header(line)) {
        throw runtime_error("single-line parse does not accept incomplete blocks");
    }

    const auto pipeline_segments = split_pipeline_segments(line);
    if(pipeline_segments.size() > 1) {
        vector<shared_ptr<PipeableExecution>> pipeline;
        pipeline.reserve(pipeline_segments.size());
        for(const auto &segment : pipeline_segments) {
            pipeline.push_back(parse_pipeline_stage(segment));
        }
        return make_shared<PipelineExecution>(move(pipeline));
    }

    if(is_assignment_line(line)) {
        const size_t equal_pos = line.find(L'=');
        if(equal_pos == wstring::npos || line.find(L'=', equal_pos + 1) != wstring::npos) {
            throw runtime_error("assignment must contain exactly one =");
        }
        const auto lhs = trim(line.substr(0, equal_pos));
        const auto rhs = trim(line.substr(equal_pos + 1));
        if(rhs.empty()) {
            throw runtime_error("missing assignment rhs");
        }
        auto [name, declared_type] = parse_assignment_lhs(lhs);
        return make_shared<VariableRelatedExecution>(name, declared_type, tokenize_command(rhs));
    }

    const auto tokens = tokenize_command(line);
    if(tokens.empty()) {
        return nullptr;
    }
    if(is_builtin_function(tokens.front())) {
        for(const auto &token : tokens) {
            if(is_redirect_operator(token)) {
                throw runtime_error("builtin redirection is unsupported");
            }
        }
        vector<wstring> arg_tokens(tokens.begin() + 1, tokens.end());
        return make_shared<FunctionCallExecution>(tokens.front(), arg_tokens);
    }
    return parse_external_command(tokens);
}

vector<shared_ptr<BasicExecution>> lumine_sh::parse_script(const vector<wstring> &commands) {
    size_t index = 0;
    ExecutionList program;
    while(index < commands.size()) {
        const wstring line = trim(commands[index]);
        if(line.empty()) {
            ++index;
            continue;
        }
        if(line == L"}" || is_elif_header(line) || is_else_header(line)) {
            throw runtime_error("unexpected block control line");
        }
        if(is_if_header(line)) {
            program.push_back(parse_if_chain(commands, index));
            continue;
        }
        if(is_while_header(line)) {
            program.push_back(parse_while(commands, index));
            continue;
        }
        program.push_back(parse_command(line));
        ++index;
    }
    return program;
}
