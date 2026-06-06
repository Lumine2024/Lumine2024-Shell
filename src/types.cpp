#include "types.hpp"
#include "utils.hpp"

#include <cwctype>
#include <stdexcept>

using namespace std;
using namespace lumine_sh;

static wstring lower_ascii(const wstring &value) {
    wstring lowered = value;
    for(wchar_t &ch : lowered) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return lowered;
}

static bool is_int_literal(const wstring &token) {
    if(token.empty()) {
        return false;
    }
    size_t index = (token[0] == L'-') ? 1 : 0;
    if(index == token.size()) {
        return false;
    }
    for(; index < token.size(); ++index) {
        if(!iswdigit(token[index])) {
            return false;
        }
    }
    return true;
}

static int parse_int_literal(const wstring &token) {
    try {
        return stoi(token);
    } catch(const exception &) {
        throw runtime_error("invalid integer literal");
    }
}

static bool is_bool_literal(const wstring &token) {
    const wstring lowered = lower_ascii(token);
    return lowered == L"true" || lowered == L"false";
}

static bool parse_bool_literal(const wstring &token) {
    return lower_ascii(token) == L"true";
}

static bool is_quoted_string(const wstring &token) {
    return token.size() >= 2 && token.front() == L'"' && token.back() == L'"';
}

static void require_same_type(const shared_ptr<BasicShellType> &lhs, const shared_ptr<BasicShellType> &rhs) {
    if(lhs->type_name() != rhs->type_name()) {
        throw runtime_error("type mismatch");
    }
}

static shared_ptr<BasicShellType> evaluate_operand(const vector<wstring> &tokens, size_t &index) {
    if(index >= tokens.size()) {
        throw runtime_error("incomplete expression");
    }
    const wstring &token = tokens[index];
    if(token.size() > 1 && token[0] == L'-' && !is_int_literal(token)) {
        ++index;
        auto inner = evaluate_operand(tokens, index);
        auto result = inner->unary_op(token);
        if(result == nullptr) {
            throw runtime_error("unsupported unary operator");
        }
        return result;
    }
    ++index;
    return resolve_token(token);
}

shared_ptr<BasicShellType> ShellInt::clone() const {
    return make_shared<ShellInt>(value);
}

wstring ShellInt::type_name() const {
    return L"int";
}

shared_ptr<BasicShellType> ShellInt::unary_op(const wstring &command) const {
    if(command == L"-Type") {
        return make_shared<ShellString>(L"int");
    }
    if(command == L"-Neg") {
        return make_shared<ShellInt>(-value);
    }
    return nullptr;
}

shared_ptr<BasicShellType> ShellInt::binary_op(
    const wstring &command,
    const shared_ptr<BasicShellType> &other
) const {
    if(other == nullptr || other->type_name() != L"int") {
        throw runtime_error("integer operator requires integer rhs");
    }
    const auto rhs = dynamic_pointer_cast<ShellInt>(other);
    if(command == L"-Add") {
        return make_shared<ShellInt>(value + rhs->value);
    }
    if(command == L"-Sub") {
        return make_shared<ShellInt>(value - rhs->value);
    }
    if(command == L"-Mul") {
        return make_shared<ShellInt>(value * rhs->value);
    }
    if(command == L"-Div") {
        if(rhs->value == 0) {
            throw runtime_error("division by zero");
        }
        return make_shared<ShellInt>(value / rhs->value);
    }
    if(command == L"-Rem") {
        if(rhs->value == 0) {
            throw runtime_error("division by zero");
        }
        return make_shared<ShellInt>(value % rhs->value);
    }
    if(command == L"-Eq") {
        return make_shared<ShellBool>(value == rhs->value);
    }
    if(command == L"-Ne") {
        return make_shared<ShellBool>(value != rhs->value);
    }
    if(command == L"-Gt") {
        return make_shared<ShellBool>(value > rhs->value);
    }
    if(command == L"-Ge") {
        return make_shared<ShellBool>(value >= rhs->value);
    }
    if(command == L"-Lt") {
        return make_shared<ShellBool>(value < rhs->value);
    }
    if(command == L"-Le") {
        return make_shared<ShellBool>(value <= rhs->value);
    }
    throw runtime_error("unsupported integer operator");
}

wstring ShellInt::stringify() const {
    return to_wstring(value);
}

bool ShellInt::truthy() const {
    return value != 0;
}

shared_ptr<BasicShellType> ShellBool::clone() const {
    return make_shared<ShellBool>(value);
}

wstring ShellBool::type_name() const {
    return L"bool";
}

shared_ptr<BasicShellType> ShellBool::unary_op(const wstring &command) const {
    if(command == L"-Type") {
        return make_shared<ShellString>(L"bool");
    }
    if(command == L"-Not") {
        return make_shared<ShellBool>(!value);
    }
    return nullptr;
}

shared_ptr<BasicShellType> ShellBool::binary_op(
    const wstring &command,
    const shared_ptr<BasicShellType> &other
) const {
    if(other == nullptr || other->type_name() != L"bool") {
        throw runtime_error("boolean operator requires boolean rhs");
    }
    const auto rhs = dynamic_pointer_cast<ShellBool>(other);
    if(command == L"-And") {
        return make_shared<ShellBool>(value && rhs->value);
    }
    if(command == L"-Or") {
        return make_shared<ShellBool>(value || rhs->value);
    }
    if(command == L"-Eq") {
        return make_shared<ShellBool>(value == rhs->value);
    }
    if(command == L"-Ne") {
        return make_shared<ShellBool>(value != rhs->value);
    }
    throw runtime_error("unsupported boolean operator");
}

wstring ShellBool::stringify() const {
    return value ? L"true" : L"false";
}

bool ShellBool::truthy() const {
    return value;
}

shared_ptr<BasicShellType> ShellString::clone() const {
    return make_shared<ShellString>(value);
}

wstring ShellString::type_name() const {
    return L"string";
}

shared_ptr<BasicShellType> ShellString::unary_op(const wstring &command) const {
    if(command == L"-Type") {
        return make_shared<ShellString>(L"string");
    }
    return nullptr;
}

shared_ptr<BasicShellType> ShellString::binary_op(
    const wstring &command,
    const shared_ptr<BasicShellType> &other
) const {
    if(other == nullptr) {
        throw runtime_error("string operator requires rhs");
    }
    const wstring rhs = other->stringify();
    if(command == L"-Add") {
        return make_shared<ShellString>(value + rhs);
    }
    if(command == L"-Eq") {
        return make_shared<ShellBool>(value == rhs);
    }
    if(command == L"-Ne") {
        return make_shared<ShellBool>(value != rhs);
    }
    if(command == L"-Gt") {
        return make_shared<ShellBool>(value > rhs);
    }
    if(command == L"-Ge") {
        return make_shared<ShellBool>(value >= rhs);
    }
    if(command == L"-Lt") {
        return make_shared<ShellBool>(value < rhs);
    }
    if(command == L"-Le") {
        return make_shared<ShellBool>(value <= rhs);
    }
    throw runtime_error("unsupported string operator");
}

wstring ShellString::stringify() const {
    return value;
}

bool ShellString::truthy() const {
    return !value.empty();
}

bool lumine_sh::is_supported_type(const wstring &type_name) {
    return type_name == L"int" || type_name == L"bool" || type_name == L"string";
}

shared_ptr<BasicShellType> lumine_sh::make_default_value(const wstring &type_name) {
    if(type_name == L"int") {
        return make_shared<ShellInt>(0);
    }
    if(type_name == L"bool") {
        return make_shared<ShellBool>(false);
    }
    if(type_name == L"string") {
        return make_shared<ShellString>(L"");
    }
    throw runtime_error("unsupported type");
}

shared_ptr<BasicShellType> lumine_sh::convert_to_type(
    const shared_ptr<BasicShellType> &value,
    const wstring &type_name
) {
    if(value == nullptr) {
        return make_default_value(type_name);
    }
    if(type_name == L"int") {
        if(value->type_name() != L"int") {
            throw runtime_error("cannot assign non-int to int");
        }
        return value->clone();
    }
    if(type_name == L"bool") {
        if(value->type_name() == L"bool") {
            return value->clone();
        }
        return make_shared<ShellBool>(value->truthy());
    }
    if(type_name == L"string") {
        return make_shared<ShellString>(value->stringify());
    }
    throw runtime_error("unsupported type");
}

shared_ptr<BasicShellType> lumine_sh::resolve_token(const wstring &token) {
    if(token.empty()) {
        throw runtime_error("empty token");
    }
    if(token[0] == L'$') {
        const auto it = variables.find(token.substr(1));
        if(it == variables.end()) {
            throw runtime_error("undefined variable: " + narrow_ascii(token));
        }
        return it->second->clone();
    }
    if(is_int_literal(token)) {
        return make_shared<ShellInt>(parse_int_literal(token));
    }
    if(is_bool_literal(token)) {
        return make_shared<ShellBool>(parse_bool_literal(token));
    }
    if(is_quoted_string(token)) {
        return make_shared<ShellString>(token.substr(1, token.size() - 2));
    }
    return make_shared<ShellString>(token);
}

shared_ptr<BasicShellType> lumine_sh::evaluate_expression(const vector<wstring> &tokens) {
    if(tokens.empty()) {
        return make_shared<ShellString>(L"");
    }
    size_t index = 0;
    auto current = evaluate_operand(tokens, index);
    while(index < tokens.size()) {
        const wstring op = tokens[index++];
        auto rhs = evaluate_operand(tokens, index);
        current = current->binary_op(op, rhs);
    }
    return current;
}

bool lumine_sh::is_truthy(const shared_ptr<BasicShellType> &value) {
    return value != nullptr && value->truthy();
}
