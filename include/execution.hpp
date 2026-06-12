#pragma once

#include "pipe.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lumine_sh {

class BasicExecution {
public:
    virtual ~BasicExecution() = default;
    virtual void execute() = 0;
};

class PipeableExecution : public BasicExecution {
public:
    virtual void run_pipeline_stage(const PipeEnd *input, const PipeEnd *output) = 0;
};

using ExecutionList = std::vector<std::shared_ptr<BasicExecution>>;

struct ConditionalBranch {
    std::vector<std::wstring> condition_tokens;
    ExecutionList subcommands;
};

struct RedirectIo {
    std::optional<std::wstring> input_file;
    std::optional<std::wstring> output_file;
    std::optional<std::wstring> error_file;
};

class InvokeExternalExecution : public PipeableExecution {
public:
    InvokeExternalExecution(const std::vector<std::wstring> &_argv, const RedirectIo &_redirect_io)
        : argv(_argv), redirect_io(_redirect_io) {}
    InvokeExternalExecution(std::vector<std::wstring> &&_argv, RedirectIo &&_redirect_io)
        : argv(std::move(_argv)), redirect_io(std::move(_redirect_io)) {}

    void execute() override;
    void run_pipeline_stage(const PipeEnd *input, const PipeEnd *output) override;

private:
    friend class PipelineExecution;

    std::vector<std::wstring> argv;
    RedirectIo redirect_io;
};

class FunctionCallExecution : public PipeableExecution {
public:
    FunctionCallExecution(const std::wstring &_name, const std::vector<std::wstring> &_args)
        : name(_name), arg_tokens(_args) {}
    FunctionCallExecution(std::wstring &&_name, std::vector<std::wstring> &&_args)
        : name(std::move(_name)), arg_tokens(std::move(_args)) {}

    void execute() override;
    void run_pipeline_stage(const PipeEnd *input, const PipeEnd *output) override;

private:
    std::wstring name;
    std::vector<std::wstring> arg_tokens;
};

class VariableRelatedExecution : public BasicExecution {
public:
    VariableRelatedExecution(
        const std::wstring &_variable_name,
        const std::optional<std::wstring> &_declared_type,
        const std::vector<std::wstring> &_expression_tokens
    )
        : variable_name(_variable_name),
          declared_type(_declared_type),
          expression_tokens(_expression_tokens) {}

    void execute() override;

private:
    std::wstring variable_name;
    std::optional<std::wstring> declared_type;
    std::vector<std::wstring> expression_tokens;
};

class IfStatementExecution : public BasicExecution {
public:
    IfStatementExecution(const std::vector<ConditionalBranch> &_branches, const ExecutionList &_else_commands)
        : branches(_branches), else_commands(_else_commands) {}

    void execute() override;

private:
    std::vector<ConditionalBranch> branches;
    ExecutionList else_commands;
};

class WhileStatementExecution : public BasicExecution {
public:
    WhileStatementExecution(
        const std::vector<std::wstring> &_condition_tokens,
        const ExecutionList &_subcommands
    )
        : condition_tokens(_condition_tokens), subcommands(_subcommands) {}

    void execute() override;

private:
    std::vector<std::wstring> condition_tokens;
    ExecutionList subcommands;
};

class PipelineExecution : public BasicExecution {
public:
    PipelineExecution(const std::vector<std::shared_ptr<PipeableExecution>> &_pipeline)
        : pipeline(_pipeline) {}
    PipelineExecution(std::vector<std::shared_ptr<PipeableExecution>> &&_pipeline)
        : pipeline(std::move(_pipeline)) {}

    void execute() override;

private:
    std::vector<std::shared_ptr<PipeableExecution>> pipeline;
};

}
