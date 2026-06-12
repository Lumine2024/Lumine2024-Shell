#include "execution.hpp"

#include "builtin_functions.hpp"
#include "types.hpp"
#include "utils.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <cerrno>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace std;
using namespace lumine_sh;

namespace {

#ifdef _WIN32
bool is_valid_pipe_end(const PipeEnd &pipe_end) {
    return pipe_end.handle != nullptr && pipe_end.handle != INVALID_HANDLE_VALUE;
}

void invalidate_pipe_end(PipeEnd &pipe_end) {
    pipe_end.handle = nullptr;
}

void close_pipe_end(PipeEnd &pipe_end) {
    if(is_valid_pipe_end(pipe_end)) {
        CloseHandle(pipe_end.handle);
        invalidate_pipe_end(pipe_end);
    }
}

pair<PipeEnd, PipeEnd> create_pipe_pair() {
    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE read_handle = nullptr;
    HANDLE write_handle = nullptr;
    if(!CreatePipe(&read_handle, &write_handle, &security_attributes, 0)) {
        throw runtime_error("failed to create pipe");
    }

    return {PipeEnd{read_handle}, PipeEnd{write_handle}};
}
#else
bool is_valid_pipe_end(const PipeEnd &pipe_end) {
    return pipe_end.fd >= 0;
}

void invalidate_pipe_end(PipeEnd &pipe_end) {
    pipe_end.fd = -1;
}

void close_pipe_end(PipeEnd &pipe_end) {
    if(is_valid_pipe_end(pipe_end)) {
        close(pipe_end.fd);
        invalidate_pipe_end(pipe_end);
    }
}

pair<PipeEnd, PipeEnd> create_pipe_pair() {
    int fds[2];
    if(pipe(fds) < 0) {
        throw runtime_error("failed to create pipe");
    }
    return {PipeEnd{fds[0]}, PipeEnd{fds[1]}};
}
#endif

void close_pipe_pairs(vector<pair<PipeEnd, PipeEnd>> &pipes) {
    for(auto &pipe_pair : pipes) {
        close_pipe_end(pipe_pair.first);
        close_pipe_end(pipe_pair.second);
    }
}

void execute_block(const ExecutionList &commands) {
    for(const auto &command : commands) {
        command->execute();
    }
}

#ifdef _WIN32
struct RunningProcess {
    HANDLE handle = nullptr;
};

bool is_valid_running_process(const RunningProcess &process) {
    return process.handle != nullptr && process.handle != INVALID_HANDLE_VALUE;
}

void close_running_process(RunningProcess &process) {
    if(is_valid_running_process(process)) {
        CloseHandle(process.handle);
        process.handle = nullptr;
    }
}

wstring quote_windows_argument(const wstring &arg) {
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

wstring build_windows_command_line(const vector<wstring> &argv) {
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

HANDLE open_redirected_file(
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

RunningProcess start_external_process(
    const vector<wstring> &argv,
    const RedirectIo &redirect_io,
    const PipeEnd *pipeline_input = nullptr,
    const PipeEnd *pipeline_output = nullptr
) {
    if(argv.empty()) {
        return {};
    }
    if(pipeline_input != nullptr && redirect_io.input_file.has_value()) {
        throw runtime_error("pipeline stage has conflicting stdin sources");
    }
    if(pipeline_output != nullptr && redirect_io.output_file.has_value()) {
        throw runtime_error("pipeline stage has conflicting stdout targets");
    }

    const wstring command_line = build_windows_command_line(argv);

    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE handle_input = pipeline_input != nullptr ? pipeline_input->handle : GetStdHandle(STD_INPUT_HANDLE);
    HANDLE handle_output = pipeline_output != nullptr ? pipeline_output->handle : GetStdHandle(STD_OUTPUT_HANDLE);
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
    if(close_input) {
        CloseHandle(handle_input);
    }
    if(close_output) {
        CloseHandle(handle_output);
    }
    if(close_error) {
        CloseHandle(handle_error);
    }
    return RunningProcess{process_info.hProcess};
}

int wait_running_process(RunningProcess &process) {
    if(!is_valid_running_process(process)) {
        return 0;
    }
    WaitForSingleObject(process.handle, INFINITE);

    DWORD exit_code = 0;
    if(!GetExitCodeProcess(process.handle, &exit_code)) {
        close_running_process(process);
        throw runtime_error("failed to query external process exit code");
    }
    close_running_process(process);
    return static_cast<int>(exit_code);
}
#elif defined(__linux__) || defined(__APPLE__)
struct RunningProcess {
    pid_t pid = -1;
};

bool is_valid_running_process(const RunningProcess &process) {
    return process.pid >= 0;
}

void close_running_process(RunningProcess &process) {
    process.pid = -1;
}

RunningProcess start_external_process(
    const vector<wstring> &argv,
    const RedirectIo &redirect_io,
    const PipeEnd *pipeline_input = nullptr,
    const PipeEnd *pipeline_output = nullptr
) {
    if(argv.empty()) {
        return {};
    }
    if(pipeline_input != nullptr && redirect_io.input_file.has_value()) {
        throw runtime_error("pipeline stage has conflicting stdin sources");
    }
    if(pipeline_output != nullptr && redirect_io.output_file.has_value()) {
        throw runtime_error("pipeline stage has conflicting stdout targets");
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
        if(pipeline_input != nullptr) {
            if(dup2(pipeline_input->fd, STDIN_FILENO) < 0) {
                _exit(126);
            }
        } else if(redirect_io.input_file.has_value()) {
            const string path(redirect_io.input_file->begin(), redirect_io.input_file->end());
            const int fd = open(path.c_str(), O_RDONLY);
            if(fd < 0 || dup2(fd, STDIN_FILENO) < 0) {
                _exit(126);
            }
            close(fd);
        }

        if(pipeline_output != nullptr) {
            if(dup2(pipeline_output->fd, STDOUT_FILENO) < 0) {
                _exit(126);
            }
        } else if(redirect_io.output_file.has_value()) {
            const string path(redirect_io.output_file->begin(), redirect_io.output_file->end());
            const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(fd < 0 || dup2(fd, STDOUT_FILENO) < 0) {
                _exit(126);
            }
            close(fd);
        }

        if(pipeline_input != nullptr && pipeline_input->fd != STDIN_FILENO) {
            close(pipeline_input->fd);
        }
        if(pipeline_output != nullptr && pipeline_output->fd != STDOUT_FILENO) {
            close(pipeline_output->fd);
        }

        if(redirect_io.error_file.has_value()) {
            if(redirect_io.output_file.has_value() &&
               *redirect_io.error_file == *redirect_io.output_file &&
               pipeline_output == nullptr) {
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
    return RunningProcess{child};
}

int wait_running_process(RunningProcess &process) {
    if(!is_valid_running_process(process)) {
        return 0;
    }

    int status = 0;
    if(waitpid(process.pid, &status, 0) < 0) {
        close_running_process(process);
        throw runtime_error("failed to wait for external process");
    }
    close_running_process(process);
    if(WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    throw runtime_error("external process terminated abnormally");
}
#else
struct RunningProcess {};

RunningProcess start_external_process(const vector<wstring> &, const RedirectIo &, const PipeEnd * = nullptr, const PipeEnd * = nullptr) {
    throw runtime_error("external execution is unsupported on this platform");
}

int wait_running_process(RunningProcess &) {
    throw runtime_error("external execution is unsupported on this platform");
}
#endif

int invoke_external(
    const vector<wstring> &argv,
    const RedirectIo &redirect_io,
    const PipeEnd *pipeline_input = nullptr,
    const PipeEnd *pipeline_output = nullptr
) {
    auto process = start_external_process(argv, redirect_io, pipeline_input, pipeline_output);
    return wait_running_process(process);
}

#ifdef _WIN32
void write_all_bytes(const PipeEnd &pipe_end, const string &bytes) {
    size_t offset = 0;
    while(offset < bytes.size()) {
        DWORD bytes_written = 0;
        const DWORD chunk = static_cast<DWORD>(bytes.size() - offset);
        if(!WriteFile(pipe_end.handle, bytes.data() + offset, chunk, &bytes_written, nullptr)) {
            if(GetLastError() == ERROR_BROKEN_PIPE) {
                return;
            }
            throw runtime_error("failed to write pipeline output");
        }
        offset += bytes_written;
    }
}
#elif defined(__linux__) || defined(__APPLE__)
void write_all_bytes(const PipeEnd &pipe_end, const string &bytes) {
    size_t offset = 0;
    while(offset < bytes.size()) {
        const ssize_t written = write(pipe_end.fd, bytes.data() + offset, bytes.size() - offset);
        if(written < 0) {
            if(errno == EPIPE) {
                return;
            }
            throw runtime_error("failed to write pipeline output");
        }
        offset += static_cast<size_t>(written);
    }
}
#else
void write_all_bytes(const PipeEnd &, const string &) {
    throw runtime_error("pipeline output is unsupported on this platform");
}
#endif

void write_pipeline_text(const PipeEnd *output, const wstring &text) {
    if(output == nullptr) {
        wcout << text << flush;
        return;
    }
    write_all_bytes(*output, narrow_ascii(text));
}

}

void InvokeExternalExecution::execute() {
    wcout.flush();
    cout.flush();
    cerr.flush();
    invoke_external(argv, redirect_io);
}

void InvokeExternalExecution::run_pipeline_stage(const PipeEnd *input, const PipeEnd *output) {
    invoke_external(argv, redirect_io, input, output);
}

void FunctionCallExecution::execute() {
    invoke_builtin(name, arg_tokens);
}

void FunctionCallExecution::run_pipeline_stage(const PipeEnd *input, const PipeEnd *output) {
    (void)input;
    if(name == L"Write-Output") {
        auto value = evaluate_expression(arg_tokens);
        write_pipeline_text(output, value->stringify() + L"\n");
        return;
    }
    invoke_builtin(name, arg_tokens);
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
    if(pipeline.empty()) {
        return;
    }

    wcout.flush();
    cout.flush();
    cerr.flush();

    vector<pair<PipeEnd, PipeEnd>> pipes;
    pipes.reserve(pipeline.size() > 1 ? pipeline.size() - 1 : 0);
    for(size_t i = 1; i < pipeline.size(); ++i) {
        pipes.push_back(create_pipe_pair());
    }

    vector<RunningProcess> running_processes;
    running_processes.reserve(pipeline.size());

    try {
        for(size_t reverse_index = pipeline.size(); reverse_index > 0; --reverse_index) {
            const size_t i = reverse_index - 1;
            const PipeEnd *input = i == 0 ? nullptr : &pipes[i - 1].first;
            const PipeEnd *output = i + 1 == pipeline.size() ? nullptr : &pipes[i].second;

            if(auto *external_stage = dynamic_cast<InvokeExternalExecution *>(pipeline[i].get()); external_stage != nullptr) {
                running_processes.push_back(start_external_process(
                    external_stage->argv,
                    external_stage->redirect_io,
                    input,
                    output
                ));
            } else {
                pipeline[i]->run_pipeline_stage(input, output);
            }

            if(i > 0) {
                close_pipe_end(pipes[i - 1].first);
            }
            if(i + 1 < pipeline.size()) {
                close_pipe_end(pipes[i].second);
            }
        }
    } catch(...) {
        close_pipe_pairs(pipes);
        for(auto &process : running_processes) {
            wait_running_process(process);
        }
        throw;
    }

    close_pipe_pairs(pipes);

    for(auto &process : running_processes) {
        wait_running_process(process);
    }
}
