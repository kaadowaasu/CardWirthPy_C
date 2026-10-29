#pragma once

// cw/calculator.pyのC++20移植。 Python
// 版の計算APIと、コールバック・値の遅延評価モデルを維持する。
#include "cw/lib/compat.hpp"
#include "cw/lib/decimal.hpp"
#include "cw/fwd.hpp"

namespace cw::calculator {
class CalcOption : public ::cw::lib::Object {
public:
    CalcOption() = default;
    virtual ~CalcOption() = default;
    ::cw::lib::Value m_evaltype{};
    ::cw::lib::Value m_is_differentscenario{};
    void initialize(::cw::lib::String arg_evaltype, bool arg_is_differentscenario);
};

// Pythonの_functionsは、引数の遅延評価器と元のオプション
// ・ソース位置を受け取る。 Valueは呼び出し可能オブジェクトを持て
// ないため、型付きの入口でこの言語差を吸収し、 引数を先に評価しない。
struct FunctionEntry : ::cw::lib::Object {
    using Callback = std::function<std::shared_ptr<::cw::calculator::ValueType>(
        const std::vector<::cw::lib::Callback>&,
        std::shared_ptr<::cw::calculator::CalcOption>,
        ::cw::lib::Integer, ::cw::lib::Integer)>;
    Callback callback;
};

class ComputeException : public ::cw::lib::Object {
public:
    ComputeException() = default;
    virtual ~ComputeException() = default;
    ::cw::lib::Value m_line{};
    ::cw::lib::Value m_pos{};
    // PythonのExceptionは行・桁とともに元のメッ
    // セージも保持する。 C++でObject階層を使う間も呼
    // び出し元から参照できるよう、互換オブジェクトへ保持する。
    ::cw::lib::String m_msg{};
    void initialize(::cw::lib::String arg_msg, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class Function : public ::cw::lib::Object {
public:
    Function() = default;
    virtual ~Function() = default;
    ::cw::lib::Value m_arg{};
    ::cw::lib::Value m_args{};
    ::cw::lib::Value m_line{};
    ::cw::lib::Value m_name{};
    ::cw::lib::Value m_option{};
    ::cw::lib::Value m_pos{};
    void initialize(::cw::lib::String arg_name, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos, std::vector<std::vector<::cw::lib::Value>> arg_args);
    std::shared_ptr<::cw::calculator::ValueType> call(std::shared_ptr<::cw::calculator::CalcOption> arg_option);
    ::cw::lib::String representation();
};

class Function_F : public ::cw::lib::Object {
public:
    Function_F() = default;
    virtual ~Function_F() = default;
};

class Operator : public ::cw::lib::Object {
public:
    Operator() = default;
    virtual ~Operator() = default;
    ::cw::lib::Value m_line{};
    ::cw::lib::Value m_py_operator{};
    ::cw::lib::Value m_pos{};
    void initialize(::cw::lib::String arg_py_operator, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
    static std::tuple<::cw::lib::Decimal, ::cw::lib::Decimal> chk_num(std::shared_ptr<::cw::calculator::ValueType> arg_lhs, std::shared_ptr<::cw::calculator::ValueType> arg_rhs);
    static std::tuple<::cw::lib::String, ::cw::lib::String> chk_str(std::shared_ptr<::cw::calculator::ValueType> arg_lhs, std::shared_ptr<::cw::calculator::ValueType> arg_rhs);
    static std::tuple<bool, bool> chk_bool(std::shared_ptr<::cw::calculator::ValueType> arg_lhs, std::shared_ptr<::cw::calculator::ValueType> arg_rhs);
    static std::shared_ptr<::cw::calculator::ListValue> chk_list(std::shared_ptr<::cw::calculator::ValueType> arg_val);
    static std::shared_ptr<::cw::calculator::StructureValue> chk_struct(std::shared_ptr<::cw::calculator::ValueType> arg_val);
    static std::shared_ptr<::cw::calculator::Symbol> chk_structmembername(::cw::lib::Value arg_val);
    static bool equals(std::shared_ptr<::cw::calculator::ValueType> arg_lhs, std::shared_ptr<::cw::calculator::ValueType> arg_rhs, ::cw::lib::String arg_o, bool arg_in_list);
    std::shared_ptr<::cw::calculator::ValueType> call(::cw::lib::Value arg_lhs, ::cw::lib::Value arg_rhs, std::shared_ptr<::cw::calculator::CalcOption> arg_option, std::optional<::cw::lib::String> arg_o);
    ::cw::lib::String representation();
};

class StructureInfo : public ::cw::lib::Object {
public:
    StructureInfo() = default;
    virtual ~StructureInfo() = default;
    ::cw::lib::ValueMap m__index_table{};
    ::cw::lib::Value m_is_public{};
    ::cw::lib::Value m_members{};
    ::cw::lib::Value m_name{};
    ::cw::lib::Value m_required_member_num{};
    void initialize(::cw::lib::String arg_name, bool arg_is_public, std::vector<std::shared_ptr<::cw::calculator::StructureMember>> arg_members, ::cw::lib::Integer arg_required_member_num);
    ::cw::lib::Integer index_of(::cw::lib::String arg_name);
};

class StructureMember : public ::cw::lib::Object {
public:
    StructureMember() = default;
    virtual ~StructureMember() = default;
    ::cw::lib::Value m_defvalue{};
    ::cw::lib::Value m_is_public{};
    ::cw::lib::Value m_min_value{};
    ::cw::lib::Value m_name{};
    ::cw::lib::Value m_struct_name{};
    ::cw::lib::Value m_type_check{};
    ::cw::lib::Value m_vtype{};
    void initialize(::cw::lib::String arg_name, bool arg_is_public, ::cw::lib::Value arg_defvalue, bool arg_type_check, ::cw::lib::String arg_vtype, std::optional<::cw::lib::Value> arg_min_value, ::cw::lib::String arg_struct_name);
};

class Symbol : public ::cw::lib::Object {
public:
    Symbol() = default;
    virtual ~Symbol() = default;
    ::cw::lib::Value m_line{};
    ::cw::lib::Value m_pos{};
    ::cw::lib::Value m_symbol{};
    void initialize(::cw::lib::String arg_symbol, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
    std::shared_ptr<::cw::calculator::ValueType> to_value_obj();
};

class Token : public ::cw::lib::Object {
public:
    Token() = default;
    virtual ~Token() = default;
    ::cw::lib::Value m_line{};
    ::cw::lib::Value m_pos{};
    ::cw::lib::Value m_token{};
    void initialize(::cw::lib::String arg_token, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
    ::cw::lib::String representation();
};

class UnaryOperator : public ::cw::lib::Object {
public:
    UnaryOperator() = default;
    virtual ~UnaryOperator() = default;
    ::cw::lib::Value m_line{};
    ::cw::lib::Value m_py_operator{};
    ::cw::lib::Value m_pos{};
    void initialize(::cw::lib::String arg_py_operator, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
    std::shared_ptr<::cw::calculator::ValueType> call(::cw::lib::Value arg_rhs);
    ::cw::lib::String representation();
};

class ValueType : public ::cw::lib::Object,
                  public std::enable_shared_from_this<::cw::calculator::ValueType> {
public:
    ValueType() = default;
    virtual ~ValueType() = default;
    inline static ::cw::lib::Integer s_line{};
    inline static ::cw::lib::Integer s_pos{};
    std::shared_ptr<::cw::calculator::ValueType> to_value_obj();
    virtual ::cw::lib::String to_str();
};

class ArgumentIsNotBooleanException : public ::cw::calculator::ComputeException {
public:
    ArgumentIsNotBooleanException() = default;
    virtual ~ArgumentIsNotBooleanException() = default;
    ::cw::lib::Value m_arg_index{};
    ::cw::lib::Value m_arg_value{};
    ::cw::lib::Value m_func_name{};
    void initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_arg_value, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class ArgumentIsNotDecimalException : public ::cw::calculator::ComputeException {
public:
    ArgumentIsNotDecimalException() = default;
    virtual ~ArgumentIsNotDecimalException() = default;
    ::cw::lib::Value m_arg_index{};
    ::cw::lib::Value m_arg_value{};
    ::cw::lib::Value m_func_name{};
    void initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_arg_value, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class ArgumentIsNotListException : public ::cw::calculator::ComputeException {
public:
    ArgumentIsNotListException() = default;
    virtual ~ArgumentIsNotListException() = default;
    ::cw::lib::Value m_arg_index{};
    ::cw::lib::Value m_arg_value{};
    ::cw::lib::Value m_func_name{};
    void initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_arg_value, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class ArgumentIsNotStringException : public ::cw::calculator::ComputeException {
public:
    ArgumentIsNotStringException() = default;
    virtual ~ArgumentIsNotStringException() = default;
    ::cw::lib::Value m_arg_index{};
    ::cw::lib::Value m_arg_value{};
    ::cw::lib::Value m_func_name{};
    void initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_arg_value, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class ArgumentIsNotStructureException : public ::cw::calculator::ComputeException {
public:
    ArgumentIsNotStructureException() = default;
    virtual ~ArgumentIsNotStructureException() = default;
    ::cw::lib::Value m_arg_index{};
    ::cw::lib::Value m_arg_value{};
    ::cw::lib::Value m_func_name{};
    ::cw::lib::Value m_struct_name{};
    void initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_arg_value, ::cw::lib::String arg_struct_name, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class ArgumentsCountException : public ::cw::calculator::ComputeException {
public:
    ArgumentsCountException() = default;
    virtual ~ArgumentsCountException() = default;
    ::cw::lib::Value m_func_name{};
    void initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class BooleanValue : public ::cw::calculator::ValueType {
public:
    BooleanValue() = default;
    virtual ~BooleanValue() = default;
    inline static bool s_value{};
    ::cw::lib::Value m_line{};
    ::cw::lib::Value m_pos{};
    ::cw::lib::Value m_value{};
    void initialize(bool arg_s, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
    ::cw::lib::String to_str();
    ::cw::lib::String representation();
};

class DecimalValue : public ::cw::calculator::ValueType {
public:
    DecimalValue() = default;
    virtual ~DecimalValue() = default;
    inline static ::cw::lib::Value s_value{};
    ::cw::lib::Value m_line{};
    ::cw::lib::Value m_pos{};
    // 正確な十進数はDecimalValueが所有する。 m_valueは、既存の
    // ゲームデータ連携で使うcompat::Valueの互換窓口としてのみ残す。
    ::cw::lib::Decimal m_decimal{};
    ::cw::lib::Value m_value{};
    void initialize(::cw::lib::Value arg_s, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
    void initialize(::cw::lib::Decimal arg_s, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
    ::cw::lib::String to_str();
    ::cw::lib::String representation();
};

class DifferentScenarioException : public ::cw::calculator::ComputeException {
public:
    DifferentScenarioException() = default;
    virtual ~DifferentScenarioException() = default;
    void initialize(::cw::lib::String arg_msg, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class DifferentStructureException : public ::cw::calculator::ComputeException {
public:
    DifferentStructureException() = default;
    virtual ~DifferentStructureException() = default;
    ::cw::lib::Value m_lhs_name{};
    ::cw::lib::Value m_rhs_name{};
    void initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_lhs_name, ::cw::lib::String arg_rhs_name, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class ExprPermissionError : public ::cw::calculator::ComputeException {
public:
    ExprPermissionError() = default;
    virtual ~ExprPermissionError() = default;
    ::cw::lib::Value m_info{};
    ::cw::lib::Value m_m{};
    void initialize(::cw::lib::String arg_msg, std::shared_ptr<::cw::calculator::StructureInfo> arg_info, std::optional<std::shared_ptr<::cw::calculator::StructureMember>> arg_m, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class FlagNotFoundException : public ::cw::calculator::ComputeException {
public:
    FlagNotFoundException() = default;
    virtual ~FlagNotFoundException() = default;
    ::cw::lib::Value m_path{};
    void initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_path, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class FunctionIsNotDefinedException : public ::cw::calculator::ComputeException {
public:
    FunctionIsNotDefinedException() = default;
    virtual ~FunctionIsNotDefinedException() = default;
    ::cw::lib::Value m_func_name{};
    void initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class InvalidArgumentException : public ::cw::calculator::ComputeException {
public:
    InvalidArgumentException() = default;
    virtual ~InvalidArgumentException() = default;
    ::cw::lib::Value m_arg_index{};
    ::cw::lib::Value m_arg_value{};
    ::cw::lib::Value m_func_name{};
    void initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_arg_value, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class InvalidStepValueException : public ::cw::calculator::ComputeException {
public:
    InvalidStepValueException() = default;
    virtual ~InvalidStepValueException() = default;
    void initialize(::cw::lib::String arg_msg, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class ListIndexOutOfRangeException : public ::cw::calculator::ComputeException {
public:
    ListIndexOutOfRangeException() = default;
    virtual ~ListIndexOutOfRangeException() = default;
    ::cw::lib::Value m_arg_index{};
    ::cw::lib::Value m_arg_value{};
    ::cw::lib::Value m_func_name{};
    ::cw::lib::Value m_list_len{};
    ::cw::lib::Value m_n{};
    void initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_arg_value, ::cw::lib::Integer arg_n, ::cw::lib::Integer arg_list_len, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class ListValue : public ::cw::calculator::ValueType {
public:
    ListValue() = default;
    virtual ~ListValue() = default;
    inline static std::vector<::cw::lib::Value> s_value{};
    ::cw::lib::Value m_line{};
    ::cw::lib::Value m_pos{};
    ::cw::lib::Value m_value{};
    void initialize(std::vector<::cw::lib::Value> arg_s, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
    std::shared_ptr<::cw::calculator::ValueType> eval(::cw::lib::Integer arg_i);
    ::cw::lib::String to_str();
    ::cw::lib::String representation();
};

class SemanticsException : public ::cw::calculator::ComputeException {
public:
    SemanticsException() = default;
    virtual ~SemanticsException() = default;
    void initialize(::cw::lib::String arg_msg, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class StepNotFoundException : public ::cw::calculator::ComputeException {
public:
    StepNotFoundException() = default;
    virtual ~StepNotFoundException() = default;
    ::cw::lib::Value m_path{};
    void initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_path, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class StringValue : public ::cw::calculator::ValueType {
public:
    StringValue() = default;
    virtual ~StringValue() = default;
    inline static ::cw::lib::String s_value{};
    ::cw::lib::Value m_line{};
    ::cw::lib::Value m_pos{};
    ::cw::lib::Value m_value{};
    void initialize(::cw::lib::String arg_s, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
    ::cw::lib::String to_str();
    ::cw::lib::String representation();
};

class StructureValue : public ::cw::calculator::ValueType {
public:
    StructureValue() = default;
    virtual ~StructureValue() = default;
    inline static ::cw::lib::String s_name{};
    inline static std::vector<::cw::lib::Value> s_value{};
    ::cw::lib::Value m_line{};
    ::cw::lib::Value m_name{};
    ::cw::lib::Value m_pos{};
    ::cw::lib::Value m_value{};
    void initialize(::cw::lib::String arg_name, std::vector<::cw::lib::Value> arg_s, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
    std::shared_ptr<::cw::calculator::ValueType> eval(::cw::lib::Integer arg_i);
    ::cw::lib::String to_str();
    ::cw::lib::String representation();
};

class TokanizeException : public ::cw::calculator::ComputeException {
public:
    TokanizeException() = default;
    virtual ~TokanizeException() = default;
    void initialize(::cw::lib::String arg_msg, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class VariantNotFoundException : public ::cw::calculator::ComputeException {
public:
    VariantNotFoundException() = default;
    virtual ~VariantNotFoundException() = default;
    ::cw::lib::Value m_path{};
    void initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_path, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

class ZeroDivisionException : public ::cw::calculator::ComputeException {
public:
    ZeroDivisionException() = default;
    virtual ~ZeroDivisionException() = default;
    ::cw::lib::Value m_line{};
    ::cw::lib::Value m_pos{};
    void initialize(::cw::lib::String arg_msg, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
};

extern ::cw::lib::Value g__NUM_REG;
extern std::map<::cw::lib::String, ::cw::lib::Value> g__functions;
extern std::map<::cw::lib::String, std::shared_ptr<::cw::calculator::StructureInfo>> g__structures;
extern std::map<::cw::lib::String, ::cw::lib::Value> g__symbols;
extern ::cw::lib::Value g__test_list1;
extern ::cw::lib::Value g__test_list2;
extern ::cw::lib::Value g__test_list2_0;
extern ::cw::lib::Value g__test_list2_1;

void Function_F_call___init__(::cw::lib::Value arg_self, std::vector<::cw::lib::Value> arg_arg, std::shared_ptr<::cw::calculator::CalcOption> arg_option);
std::shared_ptr<::cw::calculator::ValueType> Function_F_call_eval_arg(::cw::lib::Value arg_self);
::cw::lib::String ListValue_to_str_to_str(::cw::lib::Integer arg_i);
std::shared_ptr<::cw::calculator::StructureInfo> struct_info(::cw::lib::String arg_name);
std::vector<::cw::lib::Value> cut_optionalmembers(::cw::lib::String arg_name, std::vector<::cw::lib::Value> arg_members);
std::vector<::cw::lib::Value> complete_optionalmembers(::cw::lib::String arg_name, std::vector<::cw::lib::Value> arg_members);
std::vector<::cw::lib::String> structure_member_names(::cw::lib::String arg_name);
bool is_valid_structure(::cw::lib::String arg_name, std::vector<std::tuple<::cw::lib::String, ::cw::lib::Value>> arg_members);
std::vector<::cw::lib::Value> parse(::cw::lib::String arg_s);
std::tuple<::cw::lib::Integer, std::vector<std::vector<::cw::lib::Value>>> parse_parse_arguments(std::vector<std::shared_ptr<::cw::calculator::Token>> arg_tokens, ::cw::lib::Integer arg_i);
std::tuple<::cw::lib::Integer, std::vector<::cw::lib::Value>> parse_parse_semantics(std::vector<std::shared_ptr<::cw::calculator::Token>> arg_tokens, ::cw::lib::Integer arg_i);
std::shared_ptr<::cw::calculator::ValueType> calculate(std::vector<::cw::lib::Value> arg_st, std::shared_ptr<::cw::calculator::CalcOption> arg_option);
std::shared_ptr<::cw::data::Variant> eval_expr(std::vector<::cw::lib::Value> arg_st, std::shared_ptr<::cw::calculator::CalcOption> arg_option);
::cw::lib::Value to_variantvalue(std::shared_ptr<::cw::calculator::ValueType> arg_val);
void _chk_diffsc(std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
void _chk_argscount(std::vector<::cw::lib::Callback> arg_args, ::cw::lib::Integer arg_n, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
void _chk_argscount2(std::vector<::cw::lib::Callback> arg_args, ::cw::lib::Integer arg_n1, ::cw::lib::Integer arg_n2, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
::cw::lib::Decimal _chk_decimal(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index);
::cw::lib::Decimal _chk_minvalue(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::Value arg_minvalue);
::cw::lib::String _chk_string(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index);
bool _chk_boolean(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index);
std::shared_ptr<::cw::calculator::ListValue> _chk_list(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index);
std::shared_ptr<::cw::calculator::StructureValue> _chk_structure(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_struct_name);
bool _is_alldecimal(std::vector<std::shared_ptr<::cw::calculator::ValueType>> arg_args, ::cw::lib::String arg_func_name);
std::vector<std::shared_ptr<::cw::calculator::ValueType>> _all_eval(std::vector<::cw::lib::Callback> arg_args);
std::shared_ptr<::cw::calculator::DecimalValue> _func_max(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_min(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_len(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_find(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_left(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_right(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_mid(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_str(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_is_str(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_is_number(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_is_list(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_value(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_int(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ValueType> _func_if(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_and_d(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_or_d(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ValueType> _func_var(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ValueType> _variantvalue_to_valuetype(::cw::lib::Value arg_val, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_flagvalue(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_flagtext(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_stepvalue(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_steptext(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_stepmax(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_dice(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
::cw::lib::Integer _ccard_index(std::shared_ptr<::cw::character::Character> arg_ccard);
std::shared_ptr<::cw::calculator::DecimalValue> _func_selected(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_latestusecardowner(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::optional<std::shared_ptr<::cw::character::Character>> _ccard_from(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name);
std::shared_ptr<::cw::calculator::BooleanValue> _func_charexists(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_casttype(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_castname(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_findcoupon(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_coupontext(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_findgossip(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_gossiptext(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_partyname(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _func_list(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _func_list_e(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ValueType> _func_at(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_llen(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_lfind(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _func_lleft(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _func_lright(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _func_lmid(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_partymoney(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_partynumber(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_yadoname(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_skintype(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_battleround(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_castlevel(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_couponvalue(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_liferatio(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_statusvalue(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_statusround(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StructureValue> _func_selectedcard(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::optional<std::shared_ptr<::cw::header::CardHeader>> _header_from(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index);
std::shared_ptr<::cw::calculator::StringValue> _func_cardname(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_cardtype(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_cardrarity(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_cardprice(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_cardlevel(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_cardcount(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_findkeycode(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _func_find_keycodes_with(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _get_real_material_path_of_card(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _get_target_menu_keycodes(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StructureValue> _func_get_card_ref_by_keycode(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StructureValue> _func_get_card_ref_by_keycode_header_to_cardinfo(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header, ::cw::lib::Integer arg_line = 0, ::cw::lib::Integer arg_pos = 0);
std::shared_ptr<::cw::calculator::StringValue> _func_get_keycode_of_card_with(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _func_zip(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _func_get_cards_of_scenario_by_keycode(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ValueType> _func_at_def(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_lower(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _func_lfind_all(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_get_property_text_of_card_of_scenario(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _get_with_copy_variables(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos, ::cw::lib::String arg_type_name, ::cw::lib::String arg_common_name);
std::shared_ptr<::cw::calculator::BooleanValue> _get_item_with_copy_variables(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _get_skill_with_copy_variables(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _get_beast_with_copy_variables(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_keycodetext(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_unixtime(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _func_for(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ValueType> _func_switch(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_sum(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_floor(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_ceil(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_sind(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_cosd(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_tand(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_distance(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ValueType> _func_parseint(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ValueType> _func_parsefloat(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ValueType> _func_eval(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_scenarioname(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_cardmaxuselimit(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _func_datetime(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_format(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _func_concatstrings(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _func_makelist(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _func_getrandomlist(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_lfindrand(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _func_pickbyindices(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ValueType> _func_lgetrandom(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_lset(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_lsetmulti(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_lremove(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_lremovemulti(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_linsert(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_lunique(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_lremoveall(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_lcount(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_contain(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_inpolygon(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_mousex(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_mousey(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ValueType> _func_getvariantvalueofcard(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_getflagvalueofcard(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::DecimalValue> _func_getstepvalueofcard(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_has_variantvalue(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_has_step(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_has_flag(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_setvariantvalueofcard(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_setstepvalueofcard(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _func_setflagvalueofcard(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::ListValue> _func_get_allcards(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StringValue> _get_propertytext(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::BooleanValue> _have_propertytext(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
std::shared_ptr<::cw::calculator::StructureValue> _create_structure(std::shared_ptr<::cw::calculator::StructureInfo> arg_info, std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos);
bool _assert_s(::cw::lib::String arg_expr, ::cw::lib::String arg_n);
bool _assert_s_val(std::shared_ptr<::cw::calculator::ValueType> arg_val, ::cw::lib::String arg_n);
bool _assert_d(::cw::lib::String arg_expr, ::cw::lib::Value arg_n);
bool _assert_d_val(std::shared_ptr<::cw::calculator::ValueType> arg_val, ::cw::lib::Value arg_n);
bool _assert_b(::cw::lib::String arg_expr, bool arg_n);
bool _assert_b_val(std::shared_ptr<::cw::calculator::ValueType> arg_val, bool arg_n);
void main();
}

