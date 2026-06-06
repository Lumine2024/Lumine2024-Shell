#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace lumine_sh {

class BasicShellType {
public:
    virtual ~BasicShellType() = default;

    virtual std::shared_ptr<BasicShellType> clone() const = 0;
    virtual std::wstring type_name() const = 0;
    virtual std::shared_ptr<BasicShellType> unary_op(const std::wstring &command) const = 0;
    virtual std::shared_ptr<BasicShellType> binary_op(
        const std::wstring &command,
        const std::shared_ptr<BasicShellType> &other
    ) const = 0;
    virtual std::wstring stringify() const = 0;
    virtual bool truthy() const = 0;
};

extern std::unordered_map<std::wstring, std::shared_ptr<BasicShellType>> variables;

class ShellInt : public BasicShellType {
public:
    ShellInt() : value(0) {}
    explicit ShellInt(int val) : value(val) {}

    std::shared_ptr<BasicShellType> clone() const override;
    std::wstring type_name() const override;
    std::shared_ptr<BasicShellType> unary_op(const std::wstring &command) const override;
    std::shared_ptr<BasicShellType> binary_op(
        const std::wstring &command,
        const std::shared_ptr<BasicShellType> &other
    ) const override;
    std::wstring stringify() const override;
    bool truthy() const override;

private:
    int value;
};

class ShellBool : public BasicShellType {
public:
    ShellBool() : value(false) {}
    explicit ShellBool(bool val) : value(val) {}

    std::shared_ptr<BasicShellType> clone() const override;
    std::wstring type_name() const override;
    std::shared_ptr<BasicShellType> unary_op(const std::wstring &command) const override;
    std::shared_ptr<BasicShellType> binary_op(
        const std::wstring &command,
        const std::shared_ptr<BasicShellType> &other
    ) const override;
    std::wstring stringify() const override;
    bool truthy() const override;

private:
    bool value;
};

class ShellString : public BasicShellType {
public:
    ShellString() = default;
    explicit ShellString(const std::wstring &str) : value(str) {}
    explicit ShellString(std::wstring &&str) : value(std::move(str)) {}

    std::shared_ptr<BasicShellType> clone() const override;
    std::wstring type_name() const override;
    std::shared_ptr<BasicShellType> unary_op(const std::wstring &command) const override;
    std::shared_ptr<BasicShellType> binary_op(
        const std::wstring &command,
        const std::shared_ptr<BasicShellType> &other
    ) const override;
    std::wstring stringify() const override;
    bool truthy() const override;

private:
    std::wstring value;
};

bool is_supported_type(const std::wstring &type_name);
std::shared_ptr<BasicShellType> make_default_value(const std::wstring &type_name);
std::shared_ptr<BasicShellType> convert_to_type(
    const std::shared_ptr<BasicShellType> &value,
    const std::wstring &type_name
);
std::shared_ptr<BasicShellType> resolve_token(const std::wstring &token);
std::shared_ptr<BasicShellType> evaluate_expression(const std::vector<std::wstring> &tokens);
bool is_truthy(const std::shared_ptr<BasicShellType> &value);

}
