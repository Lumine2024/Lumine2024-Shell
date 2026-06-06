#include "execution.hpp"

#include "builtin_functions.hpp"
#include "types.hpp"
#include "utils.hpp"

#include <cwctype>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace std;
using namespace lumine_sh;

#ifdef _WIN32
static wstring quote_windows_argument(const wstring &arg) {
    if(arg.empty()) {
        return L"\"\"";
    }

    bool requires_quotes = false;
    for(const wchar_t ch : arg) {
        if(iswspace(ch) || ch == L'"') {
            requires_quotes = true;
            break;
        }
    }
    if(!requires_quotes) {
        return arg;
    }

    wstring quoted = L"\"";
    size_t backslash_count = 0;
    for(const wchar_t ch : arg) {
        if(ch == L'\\') {
            ++backslash_count;
            continue;
        }
        if(ch == L'"') {
            quoted.append(backslash_count * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslash_count = 0;
            continue;
        }
        quoted.append(backslash_count, L'\\');
        backslash_count = 0;
        quoted.push_back(ch);
    }
    quoted.append(backslash_count * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

static HANDLE open_redirected_file(
    const wstring &path,
    DWORD desired_access,
    DWORD creation_disposition,
    SECURITY_ATTRIBUTES *security_attributes
) {
    return CreateFileW(
        path.c_str(),
        desired_access,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        security_attributes,
        creation_disposition,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
}

static int invoke_external(const vector<wstring> &argv, const RedirectIo &redirect_io) {
    if(argv.empty()) {
        return 0;
    }

    wstring command_line;
    bool first = true;
    for(const auto &token : argv) {
        if(!first) {
            command_line.push_back(L' ');
        }
        first = false;
        command_line += quote_windows_argument(token);
    }

    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE handle_input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE handle_output = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE handle_error = GetStdHandle(STD_ERROR_HANDLE);
    bool close_input = false;
    bool close_output = false;
    bool close_error = false;

    if(redirect_io.input_file.has_value()) {
        handle_input = open_redirected_file(*redirect_io.input_file, GENERIC_READ, OPEN_EXISTING, &security_attributes);
        if(handle_input == INVALID_HANDLE_VALUE) {
            throw runtime_error("failed to open redirected stdin");
        }
        close_input = true;
    }

    if(redirect_io.output_file.has_value()) {
        handle_output = open_redirected_file(*redirect_io.output_file, GENERIC_WRITE, CREATE_ALWAYS, &security_attributes);
        if(handle_output == INVALID_HANDLE_VALUE) {
            if(close_input) {
                CloseHandle(handle_input);
            }
            throw runtime_error("failed to open redirected stdout");
        }
        close_output = true;
    }

    if(redirect_io.error_file.has_value()) {
        if(redirect_io.output_file.has_value() && *redirect_io.error_file == *redirect_io.output_file) {
            handle_error = handle_output;
        } else {
            handle_error = open_redirected_file(*redirect_io.error_file, GENERIC_WRITE, CREATE_ALWAYS, &security_attributes);
            if(handle_error == INVALID_HANDLE_VALUE) {
                if(close_input) {
                    CloseHandle(handle_input);
                }
                if(close_output) {
                    CloseHandle(handle_output);
                }
                throw runtime_error("failed to open redirected stderr");
            }
            close_error = true;
        }
    }

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = handle_input;
    startup_info.hStdOutput = handle_output;
    startup_info.hStdError = handle_error;

    PROCESS_INFORMATION process_info{};
    vector<wchar_t> buffer(command_line.begin(), command_line.end());
    buffer.push_back(L'\0');
    if(!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup_info, &process_info)) {
        if(close_input) {
            CloseHandle(handle_input);
        }
        if(close_output) {
            CloseHandle(handle_output);
        }
        if(close_error) {
            CloseHandle(handle_error);
        }
        throw runtime_error("failed to create external process");
    }

    CloseHandle(process_info.hThread);
    WaitForSingleObject(process_info.hProcess, INFINITE);

    DWORD exit_code = 0;
    if(!GetExitCodeProcess(process_info.hProcess, &exit_code)) {
        CloseHandle(process_info.hProcess);
        if(close_input) {
            CloseHandle(handle_input);
        }
        if(close_output) {
            CloseHandle(handle_output);
        }
        if(close_error) {
            CloseHandle(handle_error);
        }
        throw runtime_error("failed to query external process exit code");
    }

    CloseHandle(process_info.hProcess);
    if(close_input) {
        CloseHandle(handle_input);
    }
    if(close_output) {
        CloseHandle(handle_output);
    }
    if(close_error) {
        CloseHandle(handle_error);
    }
    return static_cast<int>(exit_code);
}
#elif defined(__linux__) || defined(__APPLE__)
static int invoke_external(const vector<wstring> &argv, const RedirectIo &redirect_io) {
    if(argv.empty()) {
        return 0;
    }

    vector<string> argv_storage;
    argv_storage.reserve(argv.size());
    for(const auto &token : argv) {
        argv_storage.emplace_back(token.begin(), token.end());
    }

    vector<char *> argv_ptrs;
    argv_ptrs.reserve(argv_storage.size() + 1);
    for(auto &token : argv_storage) {
        argv_ptrs.push_back(token.data());
    }
    argv_ptrs.push_back(nullptr);

    const pid_t child = fork();
    if(child < 0) {
        throw runtime_error("failed to fork external process");
    }
    if(child == 0) {
        if(redirect_io.input_file.has_value()) {
            const string path(redirect_io.input_file->begin(), redirect_io.input_file->end());
            const int fd = open(path.c_str(), O_RDONLY);
            if(fd < 0 || dup2(fd, STDIN_FILENO) < 0) {
                _exit(126);
            }
            close(fd);
        }
        if(redirect_io.output_file.has_value()) {
            const string path(redirect_io.output_file->begin(), redirect_io.output_file->end());
            const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(fd < 0 || dup2(fd, STDOUT_FILENO) < 0) {
                _exit(126);
            }
            close(fd);
        }
        if(redirect_io.error_file.has_value()) {
            if(redirect_io.output_file.has_value() && *redirect_io.error_file == *redirect_io.output_file) {
                if(dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
                    _exit(126);
                }
            } else {
                const string path(redirect_io.error_file->begin(), redirect_io.error_file->end());
                const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if(fd < 0 || dup2(fd, STDERR_FILENO) < 0) {
                    _exit(126);
                }
                close(fd);
            }
        }
        execvp(argv_ptrs.front(), argv_ptrs.data());
        _exit(127);
    }

    int status = 0;
    if(waitpid(child, &status, 0) < 0) {
        throw runtime_error("failed to wait for external process");
    }
    if(WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    throw runtime_error("external process terminated abnormally");
}
#else
static int invoke_external(const vector<wstring> &, const RedirectIo &) {
    throw runtime_error("external execution is unsupported on this platform");
}
#endif

static void execute_block(const ExecutionList &commands) {
    for(const auto &command : commands) {
        command->execute();
    }
}

#ifdef _WIN32
static wstring build_windows_command_line(const vector<wstring> &argv) {
    wstring command_line;
    bool first = true;
    for(const auto &token : argv) {
        if(!first) {
            command_line.push_back(L' ');
        }
        first = false;
        command_line += quote_windows_argument(token);
    }
    return command_line;
}

static wstring invoke_external_in_pipeline(const vector<wstring> &argv, const RedirectIo &redirect_io, const wstring &input) {
    if(redirect_io.input_file.has_value() || redirect_io.output_file.has_value()) {
        throw runtime_error("pipeline stage does not support explicit stdin/stdout redirection");
    }
    if(argv.empty()) {
        return L"";
    }

    const string input_bytes = narrow_ascii(input);
    const wstring command_line = build_windows_command_line(argv);

    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE child_stdin_read = nullptr;
    HANDLE child_stdin_write = nullptr;
    HANDLE child_stdout_read = nullptr;
    HANDLE child_stdout_write = nullptr;
    HANDLE handle_error = GetStdHandle(STD_ERROR_HANDLE);
    bool close_error = false;

    auto cleanup = [&]() {
        if(child_stdin_read != nullptr) {
            CloseHandle(child_stdin_read);
        }
        if(child_stdin_write != nullptr) {
            CloseHandle(child_stdin_write);
        }
        if(child_stdout_read != nullptr) {
            CloseHandle(child_stdout_read);
        }
        if(child_stdout_write != nullptr) {
            CloseHandle(child_stdout_write);
        }
        if(close_error && handle_error != nullptr && handle_error != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_error);
        }
    };

    if(!CreatePipe(&child_stdout_read, &child_stdout_write, &security_attributes, 0) ||
       !SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0) ||
       !CreatePipe(&child_stdin_read, &child_stdin_write, &security_attributes, 0) ||
       !SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0)) {
        cleanup();
        throw runtime_error("failed to create pipeline");
    }

    if(redirect_io.error_file.has_value()) {
        handle_error = open_redirected_file(*redirect_io.error_file, GENERIC_WRITE, CREATE_ALWAYS, &security_attributes);
        if(handle_error == INVALID_HANDLE_VALUE) {
            cleanup();
            throw runtime_error("failed to open redirected stderr");
        }
        close_error = true;
    }

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = child_stdin_read;
    startup_info.hStdOutput = child_stdout_write;
    startup_info.hStdError = handle_error;

    PROCESS_INFORMATION process_info{};
    vector<wchar_t> buffer(command_line.begin(), command_line.end());
    buffer.push_back(L'\0');
    if(!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup_info, &process_info)) {
        cleanup();
        throw runtime_error("failed to create external process");
    }

    CloseHandle(process_info.hThread);
    CloseHandle(child_stdin_read);
    child_stdin_read = nullptr;
    CloseHandle(child_stdout_write);
    child_stdout_write = nullptr;

    size_t offset = 0;
    while(offset < input_bytes.size()) {
        DWORD bytes_written = 0;
        const DWORD chunk = static_cast<DWORD>(min<size_t>(input_bytes.size() - offset, 1u << 15));
        if(!WriteFile(child_stdin_write, input_bytes.data() + offset, chunk, &bytes_written, nullptr)) {
            if(GetLastError() != ERROR_BROKEN_PIPE) {
                CloseHandle(process_info.hProcess);
                CloseHandle(child_stdin_write);
                child_stdin_write = nullptr;
                CloseHandle(child_stdout_read);
                child_stdout_read = nullptr;
                if(close_error) {
                    CloseHandle(handle_error);
                    close_error = false;
                }
                throw runtime_error("failed to write pipeline stdin");
            }
            break;
        }
        offset += bytes_written;
    }
    CloseHandle(child_stdin_write);
    child_stdin_write = nullptr;

    string output_bytes;
    char buffer_chunk[4096];
    while(true) {
        DWORD bytes_read = 0;
        if(!ReadFile(child_stdout_read, buffer_chunk, sizeof(buffer_chunk), &bytes_read, nullptr)) {
            if(GetLastError() == ERROR_BROKEN_PIPE) {
                break;
            }
            CloseHandle(process_info.hProcess);
            CloseHandle(child_stdout_read);
            child_stdout_read = nullptr;
            if(close_error) {
                CloseHandle(handle_error);
                close_error = false;
            }
            throw runtime_error("failed to read pipeline stdout");
        }
        if(bytes_read == 0) {
            break;
        }
        output_bytes.append(buffer_chunk, buffer_chunk + bytes_read);
    }

    CloseHandle(child_stdout_read);
    child_stdout_read = nullptr;

    WaitForSingleObject(process_info.hProcess, INFINITE);
    CloseHandle(process_info.hProcess);

    if(close_error) {
        CloseHandle(handle_error);
        close_error = false;
    }

    return widen_ascii(output_bytes);
}
#elif defined(__linux__) || defined(__APPLE__)
static wstring invoke_external_in_pipeline(const vector<wstring> &argv, const RedirectIo &redirect_io, const wstring &input) {
    if(redirect_io.input_file.has_value() || redirect_io.output_file.has_value()) {
        throw runtime_error("pipeline stage does not support explicit stdin/stdout redirection");
    }
    if(argv.empty()) {
        return L"";
    }

    int stdin_pipe[2];
    int stdout_pipe[2];
    if(pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        throw runtime_error("failed to create pipeline");
    }

    vector<string> argv_storage;
    argv_storage.reserve(argv.size());
    for(const auto &token : argv) {
        argv_storage.emplace_back(token.begin(), token.end());
    }

    vector<char *> argv_ptrs;
    argv_ptrs.reserve(argv_storage.size() + 1);
    for(auto &token : argv_storage) {
        argv_ptrs.push_back(token.data());
    }
    argv_ptrs.push_back(nullptr);

    const pid_t child = fork();
    if(child < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        throw runtime_error("failed to fork external process");
    }

    if(child == 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);

        if(dup2(stdin_pipe[0], STDIN_FILENO) < 0 || dup2(stdout_pipe[1], STDOUT_FILENO) < 0) {
            _exit(126);
        }

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        if(redirect_io.error_file.has_value()) {
            const string path(redirect_io.error_file->begin(), redirect_io.error_file->end());
            const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(fd < 0 || dup2(fd, STDERR_FILENO) < 0) {
                _exit(126);
            }
            close(fd);
        }

        execvp(argv_ptrs.front(), argv_ptrs.data());
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    const string input_bytes = narrow_ascii(input);
    size_t offset = 0;
    while(offset < input_bytes.size()) {
        const size_t remaining = input_bytes.size() - offset;
        const ssize_t written = write(stdin_pipe[1], input_bytes.data() + offset, remaining);
        if(written < 0) {
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            waitpid(child, nullptr, 0);
            throw runtime_error("failed to write pipeline stdin");
        }
        offset += static_cast<size_t>(written);
    }
    close(stdin_pipe[1]);

    string output_bytes;
    char buffer_chunk[4096];
    while(true) {
        const ssize_t bytes_read = read(stdout_pipe[0], buffer_chunk, sizeof(buffer_chunk));
        if(bytes_read < 0) {
            close(stdout_pipe[0]);
            waitpid(child, nullptr, 0);
            throw runtime_error("failed to read pipeline stdout");
        }
        if(bytes_read == 0) {
            break;
        }
        output_bytes.append(buffer_chunk, buffer_chunk + bytes_read);
    }

    close(stdout_pipe[0]);

    int status = 0;
    if(waitpid(child, &status, 0) < 0) {
        throw runtime_error("failed to wait for external process");
    }
    if(!WIFEXITED(status)) {
        throw runtime_error("external process terminated abnormally");
    }

    return widen_ascii(output_bytes);
}
#else
static wstring invoke_external_in_pipeline(const vector<wstring> &, const RedirectIo &, const wstring &) {
    throw runtime_error("pipeline execution is unsupported on this platform");
}
#endif

void InvokeExternalExecution::execute() {
    wcout.flush();
    cout.flush();
    cerr.flush();
    invoke_external(argv, redirect_io);
}

wstring InvokeExternalExecution::run_pipeline_stage(const wstring &input) {
    return invoke_external_in_pipeline(argv, redirect_io, input);
}

void FunctionCallExecution::execute() {
    invoke_builtin(name, arg_tokens);
}

wstring FunctionCallExecution::run_pipeline_stage(const wstring &input) {
    wistringstream input_stream(input);
    wostringstream output_stream;
    invoke_builtin(name, arg_tokens, input_stream, output_stream);
    return output_stream.str();
}

void VariableRelatedExecution::execute() {
    const auto current = variables.find(variable_name);
    if(current == variables.end()) {
        if(!declared_type.has_value()) {
            throw runtime_error("first assignment must declare variable type");
        }
        if(!is_supported_type(*declared_type)) {
            throw runtime_error("unsupported variable type");
        }
        variables[variable_name] = convert_to_type(evaluate_expression(expression_tokens), *declared_type);
        return;
    }

    if(declared_type.has_value() && current->second->type_name() != *declared_type) {
        throw runtime_error("redeclared variable with different type");
    }
    variables[variable_name] = convert_to_type(
        evaluate_expression(expression_tokens),
        current->second->type_name()
    );
}

void IfStatementExecution::execute() {
    for(const auto &branch : branches) {
        if(is_truthy(evaluate_expression(branch.condition_tokens))) {
            execute_block(branch.subcommands);
            return;
        }
    }
    execute_block(else_commands);
}

void WhileStatementExecution::execute() {
    while(is_truthy(evaluate_expression(condition_tokens))) {
        execute_block(subcommands);
    }
}

void PipelineExecution::execute() {
    wstring current_output;
    for(const auto &stage : pipeline) {
        current_output = stage->run_pipeline_stage(current_output);
    }
    wcout << current_output;
}
