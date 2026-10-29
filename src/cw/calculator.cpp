// cw/calculator.py に対応するC++20の計算処理。
#include "cw/calculator.hpp"
#include "cw/cw.hpp"
#include "cw/character.hpp"
#include "cw/content.hpp"
#include "cw/data.hpp"
#include "cw/deck.hpp"
#include "cw/dice.hpp"
#include "cw/dice_random.hpp"
#include "cw/event.hpp"
#include "cw/battle.hpp"
#include "cw/header.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/novel.hpp"
#include "cw/util.hpp"
#include "cw/sprite/card.hpp"
#include "cw/sprite/message.hpp"
#include "cw/lib/stdlib.hpp"
#include "cw/lib/encoding.hpp"
#include "cw/lib/python_float.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <cstdint>
#include <locale>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace cw::calculator {
#if 0 // 初期生成コードは履歴として保持。実装は後続ブロック。
void CalcOption::initialize(::cw::lib::String arg_evaltype, bool arg_is_differentscenario)
{
    (void)arg_evaltype;
    (void)arg_is_differentscenario;
}

void ComputeException::initialize(::cw::lib::String arg_msg, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_line;
    (void)arg_pos;
}

void Function::initialize(::cw::lib::String arg_name, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos, std::vector<std::vector<::cw::lib::Value>> arg_args)
{
    (void)arg_name;
    (void)arg_line;
    (void)arg_pos;
    (void)arg_args;
}

std::shared_ptr<::cw::calculator::ValueType> Function::call(std::shared_ptr<::cw::calculator::CalcOption> arg_option)
{
    (void)arg_option;
    return {};
}

::cw::lib::String Function::representation()
{
    return {};
}

void Operator::initialize(::cw::lib::String arg_py_operator, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_py_operator;
    (void)arg_line;
    (void)arg_pos;
}

std::tuple<::cw::lib::Value, ::cw::lib::Value> Operator::chk_num(std::shared_ptr<::cw::calculator::ValueType> arg_lhs, std::shared_ptr<::cw::calculator::ValueType> arg_rhs)
{
    (void)arg_lhs;
    (void)arg_rhs;
    return {};
}

std::tuple<::cw::lib::String, ::cw::lib::String> Operator::chk_str(std::shared_ptr<::cw::calculator::ValueType> arg_lhs, std::shared_ptr<::cw::calculator::ValueType> arg_rhs)
{
    (void)arg_lhs;
    (void)arg_rhs;
    return {};
}

std::tuple<bool, bool> Operator::chk_bool(std::shared_ptr<::cw::calculator::ValueType> arg_lhs, std::shared_ptr<::cw::calculator::ValueType> arg_rhs)
{
    (void)arg_lhs;
    (void)arg_rhs;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> Operator::chk_list(std::shared_ptr<::cw::calculator::ValueType> arg_val)
{
    (void)arg_val;
    return {};
}

std::shared_ptr<::cw::calculator::StructureValue> Operator::chk_struct(std::shared_ptr<::cw::calculator::ValueType> arg_val)
{
    (void)arg_val;
    return {};
}

std::shared_ptr<::cw::calculator::Symbol> Operator::chk_structmembername(::cw::lib::Value arg_val)
{
    (void)arg_val;
    return {};
}

bool Operator::equals(std::shared_ptr<::cw::calculator::ValueType> arg_lhs, std::shared_ptr<::cw::calculator::ValueType> arg_rhs, ::cw::lib::String arg_o, bool arg_in_list)
{
    (void)arg_lhs;
    (void)arg_rhs;
    (void)arg_o;
    (void)arg_in_list;
    return {};
}

std::shared_ptr<::cw::calculator::ValueType> Operator::call(::cw::lib::Value arg_lhs, ::cw::lib::Value arg_rhs, std::shared_ptr<::cw::calculator::CalcOption> arg_option, std::optional<::cw::lib::String> arg_o)
{
    (void)arg_lhs;
    (void)arg_rhs;
    (void)arg_option;
    (void)arg_o;
    return {};
}

::cw::lib::String Operator::representation()
{
    return {};
}

void StructureInfo::initialize(::cw::lib::String arg_name, bool arg_is_public, std::vector<std::shared_ptr<::cw::calculator::StructureMember>> arg_members, ::cw::lib::Integer arg_required_member_num)
{
    (void)arg_name;
    (void)arg_is_public;
    (void)arg_members;
    (void)arg_required_member_num;
}

::cw::lib::Integer StructureInfo::index_of(::cw::lib::String arg_name)
{
    (void)arg_name;
    return {};
}

void StructureMember::initialize(::cw::lib::String arg_name, bool arg_is_public, ::cw::lib::Value arg_defvalue, bool arg_type_check, ::cw::lib::String arg_vtype, std::optional<::cw::lib::Value> arg_min_value, ::cw::lib::String arg_struct_name)
{
    (void)arg_name;
    (void)arg_is_public;
    (void)arg_defvalue;
    (void)arg_type_check;
    (void)arg_vtype;
    (void)arg_min_value;
    (void)arg_struct_name;
}

void Symbol::initialize(::cw::lib::String arg_symbol, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_symbol;
    (void)arg_line;
    (void)arg_pos;
}

std::shared_ptr<::cw::calculator::ValueType> Symbol::to_value_obj()
{
    return {};
}

void Token::initialize(::cw::lib::String arg_token, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_token;
    (void)arg_line;
    (void)arg_pos;
}

::cw::lib::String Token::representation()
{
    return {};
}

void UnaryOperator::initialize(::cw::lib::String arg_py_operator, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_py_operator;
    (void)arg_line;
    (void)arg_pos;
}

std::shared_ptr<::cw::calculator::ValueType> UnaryOperator::call(::cw::lib::Value arg_rhs)
{
    (void)arg_rhs;
    return {};
}

::cw::lib::String UnaryOperator::representation()
{
    return {};
}

std::shared_ptr<::cw::calculator::ValueType> ValueType::to_value_obj()
{
    return {};
}

::cw::lib::String ValueType::to_str()
{
    return {};
}

void ArgumentIsNotBooleanException::initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_arg_value, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_func_name;
    (void)arg_arg_index;
    (void)arg_arg_value;
    (void)arg_line;
    (void)arg_pos;
}

void ArgumentIsNotDecimalException::initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_arg_value, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_func_name;
    (void)arg_arg_index;
    (void)arg_arg_value;
    (void)arg_line;
    (void)arg_pos;
}

void ArgumentIsNotListException::initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_arg_value, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_func_name;
    (void)arg_arg_index;
    (void)arg_arg_value;
    (void)arg_line;
    (void)arg_pos;
}

void ArgumentIsNotStringException::initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_arg_value, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_func_name;
    (void)arg_arg_index;
    (void)arg_arg_value;
    (void)arg_line;
    (void)arg_pos;
}

void ArgumentIsNotStructureException::initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_arg_value, ::cw::lib::String arg_struct_name, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_func_name;
    (void)arg_arg_index;
    (void)arg_arg_value;
    (void)arg_struct_name;
    (void)arg_line;
    (void)arg_pos;
}

void ArgumentsCountException::initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_func_name;
    (void)arg_line;
    (void)arg_pos;
}

void BooleanValue::initialize(bool arg_s, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_s;
    (void)arg_line;
    (void)arg_pos;
}

::cw::lib::String BooleanValue::to_str()
{
    return {};
}

::cw::lib::String BooleanValue::representation()
{
    return {};
}

void DecimalValue::initialize(::cw::lib::Value arg_s, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_s;
    (void)arg_line;
    (void)arg_pos;
}

::cw::lib::String DecimalValue::to_str()
{
    return {};
}

::cw::lib::String DecimalValue::representation()
{
    return {};
}

void DifferentScenarioException::initialize(::cw::lib::String arg_msg, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_line;
    (void)arg_pos;
}

void DifferentStructureException::initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_lhs_name, ::cw::lib::String arg_rhs_name, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_lhs_name;
    (void)arg_rhs_name;
    (void)arg_line;
    (void)arg_pos;
}

void ExprPermissionError::initialize(::cw::lib::String arg_msg, std::shared_ptr<::cw::calculator::StructureInfo> arg_info, std::optional<std::shared_ptr<::cw::calculator::StructureMember>> arg_m, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_info;
    (void)arg_m;
    (void)arg_line;
    (void)arg_pos;
}

void FlagNotFoundException::initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_path, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_path;
    (void)arg_line;
    (void)arg_pos;
}

void FunctionIsNotDefinedException::initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_func_name;
    (void)arg_line;
    (void)arg_pos;
}

void InvalidArgumentException::initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_arg_value, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_func_name;
    (void)arg_arg_index;
    (void)arg_arg_value;
    (void)arg_line;
    (void)arg_pos;
}

void InvalidStepValueException::initialize(::cw::lib::String arg_msg, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_line;
    (void)arg_pos;
}

void ListIndexOutOfRangeException::initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_arg_value, ::cw::lib::Integer arg_n, ::cw::lib::Integer arg_list_len, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_func_name;
    (void)arg_arg_index;
    (void)arg_arg_value;
    (void)arg_n;
    (void)arg_list_len;
    (void)arg_line;
    (void)arg_pos;
}

void ListValue::initialize(std::vector<::cw::lib::Value> arg_s, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_s;
    (void)arg_line;
    (void)arg_pos;
}

std::shared_ptr<::cw::calculator::ValueType> ListValue::eval(::cw::lib::Integer arg_i)
{
    (void)arg_i;
    return {};
}

::cw::lib::String ListValue::to_str()
{
    return {};
}

::cw::lib::String ListValue::representation()
{
    return {};
}

void SemanticsException::initialize(::cw::lib::String arg_msg, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_line;
    (void)arg_pos;
}

void StepNotFoundException::initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_path, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_path;
    (void)arg_line;
    (void)arg_pos;
}

void StringValue::initialize(::cw::lib::String arg_s, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_s;
    (void)arg_line;
    (void)arg_pos;
}

::cw::lib::String StringValue::to_str()
{
    return {};
}

::cw::lib::String StringValue::representation()
{
    return {};
}

void StructureValue::initialize(::cw::lib::String arg_name, std::vector<::cw::lib::Value> arg_s, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_name;
    (void)arg_s;
    (void)arg_line;
    (void)arg_pos;
}

std::shared_ptr<::cw::calculator::ValueType> StructureValue::eval(::cw::lib::Integer arg_i)
{
    (void)arg_i;
    return {};
}

::cw::lib::String StructureValue::to_str()
{
    return {};
}

::cw::lib::String StructureValue::representation()
{
    return {};
}

void TokanizeException::initialize(::cw::lib::String arg_msg, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_line;
    (void)arg_pos;
}

void VariantNotFoundException::initialize(::cw::lib::String arg_msg, ::cw::lib::String arg_path, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_path;
    (void)arg_line;
    (void)arg_pos;
}

void ZeroDivisionException::initialize(::cw::lib::String arg_msg, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_msg;
    (void)arg_line;
    (void)arg_pos;
}

::cw::lib::Value g__NUM_REG{};
std::map<::cw::lib::String, ::cw::lib::Value> g__functions{};
std::map<::cw::lib::String, std::shared_ptr<::cw::calculator::StructureInfo>> g__structures{};
std::map<::cw::lib::String, ::cw::lib::Value> g__symbols{};
::cw::lib::Value g__test_list1{};
::cw::lib::Value g__test_list2{};
::cw::lib::Value g__test_list2_0{};
::cw::lib::Value g__test_list2_1{};

void Function_F_call___init__(::cw::lib::Value arg_self, std::vector<::cw::lib::Value> arg_arg, std::shared_ptr<::cw::calculator::CalcOption> arg_option)
{
    (void)arg_self;
    (void)arg_arg;
    (void)arg_option;
}

std::shared_ptr<::cw::calculator::ValueType> Function_F_call_eval_arg(::cw::lib::Value arg_self)
{
    (void)arg_self;
    return {};
}

::cw::lib::String ListValue_to_str_to_str(::cw::lib::Integer arg_i)
{
    (void)arg_i;
    return {};
}

std::shared_ptr<::cw::calculator::StructureInfo> struct_info(::cw::lib::String arg_name)
{
    (void)arg_name;
    return {};
}

std::vector<::cw::lib::Value> cut_optionalmembers(::cw::lib::String arg_name, std::vector<::cw::lib::Value> arg_members)
{
    (void)arg_name;
    (void)arg_members;
    return {};
}

bool is_valid_structure(::cw::lib::String arg_name, std::vector<std::tuple<::cw::lib::String, ::cw::lib::Value>> arg_members)
{
    (void)arg_name;
    (void)arg_members;
    return {};
}

std::vector<::cw::lib::Value> parse(::cw::lib::String arg_s)
{
    (void)arg_s;
    return {};
}

std::tuple<::cw::lib::Integer, std::vector<std::vector<::cw::lib::Value>>> parse_parse_arguments(std::vector<std::shared_ptr<::cw::calculator::Token>> arg_tokens, ::cw::lib::Integer arg_i)
{
    (void)arg_tokens;
    (void)arg_i;
    return {};
}

std::tuple<::cw::lib::Integer, std::vector<::cw::lib::Value>> parse_parse_semantics(std::vector<std::shared_ptr<::cw::calculator::Token>> arg_tokens, ::cw::lib::Integer arg_i)
{
    (void)arg_tokens;
    (void)arg_i;
    return {};
}

std::shared_ptr<::cw::calculator::ValueType> calculate(std::vector<::cw::lib::Value> arg_st, std::shared_ptr<::cw::calculator::CalcOption> arg_option)
{
    (void)arg_st;
    (void)arg_option;
    return {};
}

std::shared_ptr<::cw::data::Variant> eval_expr(std::vector<::cw::lib::Value> arg_st, std::shared_ptr<::cw::calculator::CalcOption> arg_option)
{
    (void)arg_st;
    (void)arg_option;
    return {};
}

::cw::lib::Value to_variantvalue(std::shared_ptr<::cw::calculator::ValueType> arg_val)
{
    (void)arg_val;
    return {};
}

void _chk_diffsc(std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
}

void _chk_argscount(std::vector<::cw::lib::Callback> arg_args, ::cw::lib::Integer arg_n, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_n;
    (void)arg_func_name;
    (void)arg_line;
    (void)arg_pos;
}

void _chk_argscount2(std::vector<::cw::lib::Callback> arg_args, ::cw::lib::Integer arg_n1, ::cw::lib::Integer arg_n2, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_n1;
    (void)arg_n2;
    (void)arg_func_name;
    (void)arg_line;
    (void)arg_pos;
}

::cw::lib::Value _chk_decimal(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index)
{
    (void)arg_arg;
    (void)arg_func_name;
    (void)arg_arg_index;
    return {};
}

::cw::lib::Value _chk_minvalue(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::Value arg_minvalue)
{
    (void)arg_arg;
    (void)arg_func_name;
    (void)arg_arg_index;
    (void)arg_minvalue;
    return {};
}

::cw::lib::String _chk_string(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index)
{
    (void)arg_arg;
    (void)arg_func_name;
    (void)arg_arg_index;
    return {};
}

bool _chk_boolean(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index)
{
    (void)arg_arg;
    (void)arg_func_name;
    (void)arg_arg_index;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _chk_list(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index)
{
    (void)arg_arg;
    (void)arg_func_name;
    (void)arg_arg_index;
    return {};
}

std::shared_ptr<::cw::calculator::StructureValue> _chk_structure(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index, ::cw::lib::String arg_struct_name)
{
    (void)arg_arg;
    (void)arg_func_name;
    (void)arg_arg_index;
    (void)arg_struct_name;
    return {};
}

bool _is_alldecimal(std::vector<std::shared_ptr<::cw::calculator::ValueType>> arg_args, ::cw::lib::String arg_func_name)
{
    (void)arg_args;
    (void)arg_func_name;
    return {};
}

std::vector<std::shared_ptr<::cw::calculator::ValueType>> _all_eval(std::vector<::cw::lib::Callback> arg_args)
{
    (void)arg_args;
    return {};
}

#endif

// 字句解析、構文木の値、値オブジェクト、演算子、構造情報、評価処理を
// 扱う。 再帰的なPython値の宣言には compat::Value
// を使い、下のObject保持クラスが公開ABIを変えずに内容を所
// 有する。 DecimalValue は cw::lib::Decimal
// を所有する。 compat::Value は再帰的な構文木・
// ゲームデータの境界だけに残し、算術はdoubleを経由しない。
namespace {
using ::cw::lib::Integer;
using ::cw::lib::Real;
using ::cw::lib::String;
using ::cw::lib::Value;
using ::cw::lib::Object;
using ::cw::lib::Decimal;

String lower_ascii(String s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>('A' <= c && c <= 'Z' ? c + ('a' - 'A') : c);
    });
    return s;
}
String python_upper(String s) {
    return ::cw::lib::encoding::python_upper(s);
}

Integer as_integer(const Decimal& value);

Integer as_integer(const Value& v) {
    if (const auto* p = std::get_if<Integer>(&v.storage())) return *p;
    if (const auto* p = std::get_if<Real>(&v.storage())) return static_cast<Integer>(*p);
    if (const auto* p = std::get_if<bool>(&v.storage())) return *p ? 1 : 0;
    if (const auto* p = std::get_if<Decimal>(&v.storage())) return as_integer(*p);
    throw ::cw::lib::DecimalInvalidOperation("value is not an integer");
}

Real as_real(const Value& v) {
    if (const auto* p = std::get_if<Real>(&v.storage())) return *p;
    if (const auto* p = std::get_if<Integer>(&v.storage())) return static_cast<Real>(*p);
    if (const auto* p = std::get_if<bool>(&v.storage())) return *p ? 1.0 : 0.0;
    if (const auto* p = std::get_if<Decimal>(&v.storage())) {
        return p->to_real(); // アンダーフローはオーバーフローと分けて保持する。
    }
    if (const auto* p = std::get_if<String>(&v.storage())) {
        try { return std::stod(*p); } catch (const std::exception& error) { throw ::cw::lib::DecimalInvalidOperation(String("invalid real text: ") + error.what()); }
    }
    throw ::cw::lib::DecimalInvalidOperation("value is not real");
}

Decimal decimal_from_real(Real value) {
    if (!std::isfinite(value)) return Decimal::parse(::cw::lib::python_float_repr(value));
    // RealとDecimalの意図的な表現境界として、ネイティブRealを往
    // 復可能な max_digits10 桁の十進表記で渡す。 Pythonの
    // Decimal(float) は二進小数を正確に保持するため、末尾の十
    // 進桁は異なる場合がある。 既存のDecimal入力は正確な係数を保つ。
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<Real>::max_digits10) << value;
    return Decimal::parse(out.str());
}

Decimal decimal_from_value(const Value& value) {
    if (const auto* p = std::get_if<Decimal>(&value.storage())) return *p;
    if (const auto* p = std::get_if<Integer>(&value.storage())) return Decimal(*p);
    if (const auto* p = std::get_if<Real>(&value.storage())) return decimal_from_real(*p);
    if (const auto* p = std::get_if<bool>(&value.storage())) return Decimal(*p ? 1 : 0);
    if (const auto* p = std::get_if<String>(&value.storage())) return Decimal::parse(*p);
    throw ::cw::lib::DecimalInvalidOperation("value is not a Decimal");
}

Value decimal_to_compat(const Decimal& value) {
    // compat::Value はDecimalを型付きの値として所有する。 一覧・プ
    // ロパティ・コールバックの境界でも係数を正確に保ち、 本当に基本型が必要なネイティブ
    // 変換だけが Value::as_real/as_integer を明示的に使う。
    return Value(value);
}

Real decimal_to_real(const Decimal& value) {
    // 超越関数には標準ライブラリのdouble境界が必要。 オーバーフローは明示し
    // 、呼び出し元が ComputeException に変換できるようにする。
    try {
        const auto result = value.to_real();
        if (!std::isfinite(result)) throw ::cw::lib::DecimalOverflow("Decimal is outside the finite Real range");
        return result;
    } catch (const ::cw::lib::DecimalError&) { throw; }
    catch (const std::exception& error) { throw ::cw::lib::DecimalOverflow(String("Decimal-to-Real conversion failed: ") + error.what()); }
}

Integer as_integer(const Decimal& value) {
    try { return value.to_integer(::cw::lib::DecimalRounding::Down); }
    catch (const ::cw::lib::DecimalError&) { throw ::cw::lib::DecimalOverflow("Decimal integer does not fit compat::Integer"); }
}

Integer bounded_count(const Decimal& value, Integer limit) {
    // Python版と同様、任意精度整数を有限の文字列・一覧の長さへ制限する。 変
    // 換前に比較し、巨大な要求にネイティブのインデックスを必要としないようにする。
    return value.compare(Decimal(limit)) >= 0 ? limit : as_integer(value);
}

Real as_real(const Decimal& value) { return decimal_to_real(value); }

String as_string(const Value& v) {
    if (const auto* p = std::get_if<String>(&v.storage())) return *p;
    if (const auto* p = std::get_if<Decimal>(&v.storage())) return p->to_string(true);
    if (const auto* p = std::get_if<Integer>(&v.storage())) return std::to_string(*p);
    if (const auto* p = std::get_if<Real>(&v.storage())) {
        std::ostringstream out; out << std::setprecision(15) << *p; return out.str();
    }
    if (const auto* p = std::get_if<bool>(&v.storage())) return *p ? "TRUE" : "FALSE";
    return {};
}

template<typename T>
std::shared_ptr<T> object_as(const Value& v) {
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&v.storage())) return std::dynamic_pointer_cast<T>(*p);
    return {};
}

struct ValueListHolder : Object { std::vector<Value> values; };
struct MemberListHolder : Object { std::vector<std::shared_ptr<StructureMember>> values; };
struct AstNode : Object {
    using Payload = std::variant<std::shared_ptr<ValueType>, std::shared_ptr<Function>,
                                 std::shared_ptr<UnaryOperator>, std::shared_ptr<Operator>,
                                 std::shared_ptr<Symbol>>;
    Payload value;
    explicit AstNode(Payload p) : value(std::move(p)) {}
};
// ListValue はValueを保存するが、PythonのLISTはコールバックを保存する。 Object
// 保持クラスにコールバックを入れ、ListValue::eval の遅延評価の時点を保つ。
struct CallbackHolder : Object { ::cw::lib::Callback callback; };

template<typename T>
Value boxed(std::shared_ptr<T> p) { return Value(std::static_pointer_cast<Object>(std::move(p))); }

template<typename T>
Value ast_box(std::shared_ptr<T> p) {
    return boxed(std::make_shared<AstNode>(typename AstNode::Payload(std::move(p))));
}

std::shared_ptr<AstNode> ast_node(const Value& v) { return object_as<AstNode>(v); }
std::shared_ptr<Symbol> value_symbol(const Value& v) {
    if (auto a = ast_node(v)) if (auto p = std::get_if<std::shared_ptr<Symbol>>(&a->value)) return *p;
    return object_as<Symbol>(v);
}
std::shared_ptr<Function> value_function(const Value& v) {
    if (auto a = ast_node(v)) if (auto p = std::get_if<std::shared_ptr<Function>>(&a->value)) return *p;
    return object_as<Function>(v);
}
std::shared_ptr<UnaryOperator> value_unary(const Value& v) {
    if (auto a = ast_node(v)) if (auto p = std::get_if<std::shared_ptr<UnaryOperator>>(&a->value)) return *p;
    return object_as<UnaryOperator>(v);
}
std::shared_ptr<Operator> value_operator(const Value& v) {
    if (auto a = ast_node(v)) if (auto p = std::get_if<std::shared_ptr<Operator>>(&a->value)) return *p;
    return object_as<Operator>(v);
}
std::shared_ptr<ValueType> value_type(const Value& v) {
    if (auto a = ast_node(v)) if (auto p = std::get_if<std::shared_ptr<ValueType>>(&a->value)) return *p;
    if (auto p = object_as<ValueType>(v)) return p;
    if (const auto* p = std::get_if<bool>(&v.storage())) { auto r = std::make_shared<BooleanValue>(); r->initialize(*p, 0, 0); return r; }
    if (const auto* p = std::get_if<Integer>(&v.storage())) { auto r = std::make_shared<DecimalValue>(); r->initialize(Value(*p), 0, 0); return r; }
    if (const auto* p = std::get_if<Real>(&v.storage())) { auto r = std::make_shared<DecimalValue>(); r->initialize(Value(*p), 0, 0); return r; }
    if (const auto* p = std::get_if<Decimal>(&v.storage())) { auto r = std::make_shared<DecimalValue>(); r->initialize(Value(*p), 0, 0); return r; }
    if (const auto* p = std::get_if<String>(&v.storage())) { auto r = std::make_shared<StringValue>(); r->initialize(*p, 0, 0); return r; }
    return {};
}
Integer line_of(const std::shared_ptr<ValueType>& v) {
    if (auto p = std::dynamic_pointer_cast<DecimalValue>(v)) return as_integer(p->m_line);
    if (auto p = std::dynamic_pointer_cast<StringValue>(v)) return as_integer(p->m_line);
    if (auto p = std::dynamic_pointer_cast<BooleanValue>(v)) return as_integer(p->m_line);
    if (auto p = std::dynamic_pointer_cast<ListValue>(v)) return as_integer(p->m_line);
    if (auto p = std::dynamic_pointer_cast<StructureValue>(v)) return as_integer(p->m_line);
    return 0;
}
Integer pos_of(const std::shared_ptr<ValueType>& v) {
    if (auto p = std::dynamic_pointer_cast<DecimalValue>(v)) return as_integer(p->m_pos);
    if (auto p = std::dynamic_pointer_cast<StringValue>(v)) return as_integer(p->m_pos);
    if (auto p = std::dynamic_pointer_cast<BooleanValue>(v)) return as_integer(p->m_pos);
    if (auto p = std::dynamic_pointer_cast<ListValue>(v)) return as_integer(p->m_pos);
    if (auto p = std::dynamic_pointer_cast<StructureValue>(v)) return as_integer(p->m_pos);
    return 0;
}
String repr_value(const Value& v) {
    // 関数の引数一覧は、値の表示文字列ではなくPythonの repr(token)
    // を使う。 表示文字列ではDecimalの小数桁が落ち、真偽値も大文字になるため。
    if (auto t = object_as<DecimalValue>(v)) return t->representation();
    if (auto t = object_as<BooleanValue>(v)) return t->representation();
    if (auto t = object_as<StringValue>(v)) return t->representation();
    if (auto t = value_type(v)) return t->to_str();
    if (auto s = value_symbol(v)) {
        // PythonのSymbolには独自のreprがないため、型とオブジェクトの識別情報を保持する。
        std::ostringstream text;
        text << "<cw.calculator.Symbol object at 0x" << std::hex <<
            reinterpret_cast<std::uintptr_t>(s.get()) << ">";
        return text.str();
    }
    if (auto f = value_function(v)) return f->representation();
    if (auto u = value_unary(v)) return u->representation();
    if (auto o = value_operator(v)) return o->representation();
    return as_string(v);
}
String python_decimal_text(const Decimal& value) {
    if (!value.is_finite()) return value.to_string();
    const auto digits = value.coefficient_string();
    const auto scale = value.scale();
    const auto length = static_cast<Integer>(digits.size() - 1);
    if (scale < 0 && length > std::numeric_limits<Integer>::max() + scale)
        throw ::cw::lib::DecimalOverflow("decimal display exponent is too large");
    const auto exponent = length - scale;
    // Decimal.__str__ と同様、指数がゼロ以下かつ調整済み指数が
    // -6以上のときだけ固定小数点表記にする。 符号付きゼロと係数を保持する。
    if (scale >= 0 && exponent >= -6) return value.to_string(true);
    String result = value.negative() ? "-" : "";
    result += digits.front();
    if (digits.size() > 1) result += "." + digits.substr(1);
    result += "E";
    if (exponent >= 0) result += "+";
    return result + std::to_string(exponent);
}
String real_string(Real value) {
    std::ostringstream out; out << std::fixed << std::setprecision(8) << value;
    String s = out.str(); while (s.size() > 1 && s.back() == '0') s.pop_back(); if (!s.empty() && s.back() == '.') s.pop_back();
    if (s.empty() || s == "-0") s = "0"; return s;
}
[[noreturn]] void throw_semantics(const String& msg, Integer line, Integer pos) { SemanticsException e; e.initialize(msg, line, pos); throw e; }
std::shared_ptr<ValueListHolder> list_holder(const Value& v) { return object_as<ValueListHolder>(v); }
std::vector<Value> list_values(const Value& v) { auto h = list_holder(v); return h ? h->values : std::vector<Value>{}; }
std::vector<std::shared_ptr<StructureMember>> members_of(const StructureInfo& info) {
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&info.m_members.storage())) {
        if (auto h = std::dynamic_pointer_cast<MemberListHolder>(*p)) return h->values;
    }
    return {};
}
std::vector<Value> structure_values(const StructureValue& s) { return list_values(s.m_value); }
String structure_name(const StructureValue& s) { return as_string(s.m_name); }
bool is_zero(const Value& v) {
    try { return decimal_from_value(v).is_zero(); } catch (...) { return true; }
}
String real_text(Real v) { return real_string(v); }
std::shared_ptr<DecimalValue> decimal_result(const Decimal& value, Integer line, Integer pos) { auto r = std::make_shared<DecimalValue>(); r->initialize(value, line, pos); return r; }
std::shared_ptr<DecimalValue> decimal_result(int value, Integer line, Integer pos) { const Decimal decimal(static_cast<Integer>(value)); return decimal_result(decimal, line, pos); }
std::shared_ptr<DecimalValue> decimal_result(Integer value, Integer line, Integer pos) { return decimal_result(Decimal(value), line, pos); }
std::shared_ptr<DecimalValue> decimal_result(Real value, Integer line, Integer pos) { return decimal_result(decimal_from_real(value), line, pos); }
std::shared_ptr<BooleanValue> boolean_result(bool v, Integer line, Integer pos) { auto r = std::make_shared<BooleanValue>(); r->initialize(v, line, pos); return r; }
std::shared_ptr<StringValue> string_result(String v, Integer line, Integer pos) { auto r = std::make_shared<StringValue>(); r->initialize(std::move(v), line, pos); return r; }
std::shared_ptr<ValueType> eval_value(const Value& v) { if (auto t = value_type(v)) return t; if (auto s = value_symbol(v)) return s->to_value_obj(); if (auto c = object_as<CallbackHolder>(v)) return value_type(c->callback({}, {})); throw_semantics("Invalid value.", 0, 0); }

std::shared_ptr<::cw::data::SystemData> current_sdata() {
    if (!::cw::g_cwpy) return {};
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&::cw::g_cwpy->m_sdata.storage())) {
        return object && *object ? std::dynamic_pointer_cast<::cw::data::SystemData>(*object) : nullptr;
    }
    return {};
}
std::shared_ptr<::cw::event::Event> current_event() {
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_event) return {};
    const auto event = ::cw::g_cwpy->m_event->get_nowrunningevent();
    return event ? *event : nullptr;
}
std::shared_ptr<::cw::character::Character> as_character(const Value& value) {
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage())) {
        return object && *object ? std::dynamic_pointer_cast<::cw::character::Character>(*object) : nullptr;
    }
    return {};
}
// Pythonの FINDCOUPON/FINDGOSSIP/FINDKEYCODE と同様、fnmatch.translate と
// re.match の規則を使う。 Unicodeのコードポイント・文字クラスを扱い、Windowsでも大文字小文字を区別する。
bool glob_match(const String& pattern, const String& text, std::size_t = 0, std::size_t = 0) {
    return ::cw::lib::fnmatchcase(text, pattern);
}

std::size_t utf8_cp_advance(const String& text, std::size_t offset) {
    if (offset >= text.size()) return offset;
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80U) return offset + 1;
    const unsigned width = (first & 0xe0U) == 0xc0U ? 2U : ((first & 0xf0U) == 0xe0U ? 3U : 4U);
    if (offset + width > text.size()) return offset + 1;
    return offset + width;
}
Integer utf8_cp_len(const String& text) {
    Integer n = 0;
    for (std::size_t i = 0; i < text.size(); i = utf8_cp_advance(text, i)) ++n;
    return n;
}
std::size_t utf8_whitespace_width(const String& text, std::size_t offset) {
    if (offset >= text.size()) return 0;
    const auto c0 = static_cast<unsigned char>(text[offset]);
    if (c0 < 0x80U)
        return (std::isspace(c0) != 0 || (0x1cU <= c0 && c0 <= 0x1fU)) ? 1U : 0U;
    const auto byte = [&](std::size_t n) -> unsigned char {
        return offset + n < text.size() ? static_cast<unsigned char>(text[offset + n]) : 0U;
    };
    // 全角空白U+3000も含め、Pythonの str.isspace() に合わせる。
    if (c0 == 0xc2U && (byte(1) == 0x85U || byte(1) == 0xa0U)) return 2U;
    if (c0 == 0xe1U && byte(1) == 0x9aU && byte(2) == 0x80U) return 3U;
    if (c0 == 0xe2U && byte(1) == 0x80U &&
        ((0x80U <= byte(2) && byte(2) <= 0x8aU) || byte(2) == 0xa8U ||
         byte(2) == 0xa9U || byte(2) == 0xafU)) return 3U;
    if (c0 == 0xe2U && byte(1) == 0x81U && byte(2) == 0x9fU) return 3U;
    if (c0 == 0xe3U && byte(1) == 0x80U && byte(2) == 0x80U) return 3U;
    return 0U;
}
String utf8_trim_copy(const String& text) {
    std::size_t begin = 0;
    while (const auto width = utf8_whitespace_width(text, begin)) begin += width;
    std::size_t end = begin;
    for (std::size_t i = begin; i < text.size();) {
        const auto width = utf8_whitespace_width(text, i);
        const auto next = width ? i + width : utf8_cp_advance(text, i);
        if (!width) end = next;
        i = next;
    }
    return text.substr(begin, end - begin);
}
std::size_t utf8_cp_byte_index(const String& text, Integer cp_index) {
    Integer n = 0;
    std::size_t i = 0;
    while (i < text.size() && n < cp_index) { i = utf8_cp_advance(text, i); ++n; }
    return i;
}
Integer utf8_byte_to_cp(const String& text, std::size_t byte_index) {
    Integer n = 0;
    std::size_t i = 0;
    while (i < byte_index && i < text.size()) { i = utf8_cp_advance(text, i); ++n; }
    return n;
}
String utf8_cp_slice(const String& text, Integer start, Integer count) {
    if (start < 0) start = 0;
    if (count < 0) count = 0;
    const auto begin = utf8_cp_byte_index(text, start);
    const auto end = utf8_cp_byte_index(text, start + count);
    return text.substr(begin, end - begin);
}
String unescape_quoted(const String& inner) {
    String out;
    out.reserve(inner.size());
    for (std::size_t i = 0; i < inner.size(); ++i) {
        if (inner[i] == '"' && i + 1 < inner.size() && inner[i + 1] == '"') { out.push_back('"'); ++i; }
        else out.push_back(inner[i]);
    }
    return out;
}
std::shared_ptr<ListValue> list_result(std::vector<Value> values, Integer line, Integer pos) { auto r = std::make_shared<ListValue>(); r->initialize(std::move(values), line, pos); return r; }
Value callback_value(::cw::lib::Callback callback) { auto holder = std::make_shared<CallbackHolder>(); holder->callback = std::move(callback); return boxed(std::move(holder)); }

struct LexToken { String token; Integer line; Integer pos; };
std::size_t expression_letter_width(const String& source, std::size_t i) {
    if (i >= source.size()) return 0;
    const auto c = static_cast<unsigned char>(source[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return 1;
    // Pythonの re.I の [a-z] と同様、U+0130/U+0131/U+017F/U+212Aにも一致させる。
    for (const String letter : {String("\xc4\xb0"), String("\xc4\xb1"), String("\xc5\xbf"), String("\xe2\x84\xaa")})
        if (source.compare(i, letter.size(), letter) == 0) return letter.size();
    return 0;
}
std::size_t expression_match_end(const String& source, std::size_t i) {
    const auto digit = [&](std::size_t at) { return at < source.size() && source[at] >= '0' && source[at] <= '9'; };
    if (digit(i)) {
        auto end = i + 1; while (digit(end)) ++end;
        if (end < source.size() && source[end] == '.' && digit(end + 1)) { end += 2; while (digit(end)) ++end; }
        return end;
    }
    if (const auto width = expression_letter_width(source, i)) {
        auto end = i + width;
        while (end < source.size()) {
            if (const auto next = expression_letter_width(source, end)) end += next;
            else if (digit(end)) ++end;
            else break;
        }
        return end;
    }
    if (String("+-*/%~.(),").find(source[i]) != String::npos) return i + 1;
    auto quote = i;
    if (source[i] == '@' && i + 1 < source.size() && source[i + 1] == '"') ++quote;
    if (source[quote] == '"') {
        // 貪欲な正規表現 @?"([^"]|"")*" に合わせる。 末尾の二重引
        // 用符を戻って解釈する必要がある場合に備え、閉じ引用符の候補を保持する。
        std::size_t end = quote + 1, candidate = i;
        while (end < source.size()) {
            if (source[end] != '"') { end = utf8_cp_advance(source, end); continue; }
            candidate = end + 1;
            if (end + 1 < source.size() && source[end + 1] == '"') { end += 2; continue; }
            break;
        }
        return candidate;
    }
    if (i + 1 < source.size() && (source.compare(i, 2, "<=") == 0 || source.compare(i, 2, ">=") == 0 || source.compare(i, 2, "<>") == 0)) return i + 2;
    if (String("<>=").find(source[i]) != String::npos) return i + 1;
    if (source[i] == '\n') return i + 1;
    if (const auto width = utf8_whitespace_width(source, i)) {
        auto end = i + width;
        while (const auto next = utf8_whitespace_width(source, end)) end += next;
        return end;
    }
    return i;
}
std::vector<LexToken> lex_source(const String& source, Integer& final_line, Integer& final_pos) {
    std::vector<LexToken> out;
    Integer line = 1, pos = 1;
    std::size_t i = 0, previous_end = 0;
    bool previous_match = false;
    // Python版と同様、全体一致の字句解析ではなく re.finditer を使う。
    // 末尾の不一致部分は無視し、後の一致の前に隙間があれば、直前の終了位置で例外を出す。
    while (i < source.size()) {
        const auto end = expression_match_end(source, i);
        if (end == i) { i = utf8_cp_advance(source, i); continue; }
        if (i != previous_end) {
            // Pythonの assert bm is not None に相当する、ネイティブの例外境界。
            if (!previous_match) throw std::logic_error("calculator tokenizer assertion: previous match is absent");
            TokanizeException error;
            error.initialize("Invalid Character: " + source.substr(previous_end, i - previous_end), line, pos);
            throw error;
        }
        const String text = source.substr(i, end - i);
        if (!utf8_whitespace_width(text, 0)) out.push_back({text, line, pos});
        if (text == "\n") { ++line; pos = 1; }
        else pos += ::cw::util::get_strlen(text);
        previous_match = true; previous_end = end; i = end;
    }
    final_line = line; final_pos = pos;
    return out;
}

int precedence(const String& token) { const String t = lower_ascii(token); if (t == "or") return 0; if (t == "and") return 1; if (t == "=" || t == "<>" || t == "<" || t == ">" || t == "<=" || t == ">=") return 3; if (t == "+" || t == "-" || t == "~") return 4; if (t == "*" || t == "/" || t == "%") return 5; if (t == ".") return 100; return -1; }

// 不完全なスタックも受け付ける点を含め、Python版の演算子スタックに従
// う。 結果の式の妥当性は、ここではなく calculate が検査する。
std::vector<Value> parse_semantics_impl(const std::vector<std::shared_ptr<Token>>& tokens, std::size_t& index);
std::vector<std::vector<Value>> parse_arguments_impl(const std::vector<std::shared_ptr<Token>>& tokens, std::size_t& index) {
    const auto& name = tokens.at(index);
    if (tokens.size() <= index + 1)
        throw_semantics("Invalid function call.", as_integer(name->m_line), as_integer(name->m_pos));
    ++index;
    if (as_string(tokens[index]->m_token) != "(")
        throw_semantics("Need an open parenthesis here.", as_integer(tokens[index]->m_line), as_integer(tokens[index]->m_pos));
    std::vector<std::vector<Value>> args;
    while (index + 1 < tokens.size() && as_string(tokens[index]->m_token) != ")") {
        const auto next = tokens[index + 1];
        if (as_string(next->m_token) == ")") { ++index; break; }
        ++index;
        auto arg = parse_semantics_impl(tokens, index);
        if (arg.empty()) throw_semantics("No argument.", as_integer(next->m_line), as_integer(next->m_pos));
        args.push_back(std::move(arg));
    }
    ++index;
    return args;
}
std::vector<Value> parse_semantics_impl(const std::vector<std::shared_ptr<Token>>& tokens, std::size_t& index) {
    struct Pending { int parentheses; int priority; bool unary; std::shared_ptr<Token> token; };
    std::vector<Value> result;
    std::vector<Pending> operators;
    bool isop = true;
    int parentheses = 0;
    const auto emit = [&](const Pending& pending) {
        const auto& t = pending.token;
        if (pending.unary) {
            auto op = std::make_shared<UnaryOperator>();
            op->initialize(as_string(t->m_token), as_integer(t->m_line), as_integer(t->m_pos));
            result.push_back(ast_box(op));
        } else {
            auto op = std::make_shared<Operator>();
            op->initialize(as_string(t->m_token), as_integer(t->m_line), as_integer(t->m_pos));
            result.push_back(ast_box(op));
        }
    };
    while (index < tokens.size()) {
        const auto tok = tokens[index];
        const String text = as_string(tok->m_token), lower = lower_ascii(text);
        const Integer line = as_integer(tok->m_line), pos = as_integer(tok->m_pos);
        bool unary = false;
        int priority = precedence(text);
        if (lower == "not") {
            if (!isop) throw_semantics("Need a boolean here.", line, pos);
            priority = 2; unary = true;
        } else if (text == "+" || text == "-") {
            if (isop) { priority = 99; unary = true; }
        } else if (priority >= 0) {
            if (isop) throw_semantics("Need a symbol or number here.", line, pos);
        } else if (text == "(") {
            if (!isop) throw_semantics("Need an operator here.", line, pos);
            ++parentheses; ++index; continue;
        } else if (text == ")") {
            if (isop) throw_semantics("Need a symbol or number here.", line, pos);
            if (parentheses <= 0) break;
            --parentheses; ++index; continue;
        } else if (text == ",") {
            if (parentheses <= 0) break;
            throw_semantics(isop ? "Need a symbol or number here." : "Need an operator here.", line, pos);
        } else {
            if (!isop) throw_semantics("Need an operator here.", line, pos);
            if (!text.empty() && text[0] >= '0' && text[0] <= '9') {
                auto v = std::make_shared<DecimalValue>(); v->initialize(Value(text), line, pos); result.push_back(ast_box(v)); ++index;
            } else if (!text.empty() && text[0] == '"') {
                auto v = std::make_shared<StringValue>(); v->initialize(unescape_quoted(text.substr(1, text.size() - 2)), line, pos); result.push_back(ast_box(v)); ++index;
            } else if (!text.empty() && text[0] == '@') {
                auto v = std::make_shared<StringValue>(); v->initialize(unescape_quoted(text.substr(2, text.size() - 3)), line, pos);
                auto f = std::make_shared<Function>(); f->initialize("var", line, pos, {{ast_box(v)}}); result.push_back(ast_box(f)); ++index;
            } else if (lower == "true" || lower == "false") {
                auto v = std::make_shared<BooleanValue>(); v->initialize(lower == "true", line, pos); result.push_back(ast_box(v)); ++index;
            } else if (index + 1 < tokens.size() && as_string(tokens[index + 1]->m_token) == "(") {
                auto args = parse_arguments_impl(tokens, index);
                auto f = std::make_shared<Function>(); f->initialize(text, line, pos, std::move(args)); result.push_back(ast_box(f));
            } else {
                auto v = std::make_shared<Symbol>(); v->initialize(text, line, pos); result.push_back(ast_box(v)); ++index;
            }
            isop = false; continue;
        }
        const Pending current{parentheses, priority, unary, tok};
        while (!operators.empty() && (parentheses < operators.back().parentheses ||
               (parentheses == operators.back().parentheses && priority <= operators.back().priority))) {
            if (unary && operators.back().unary) break;
            emit(operators.back()); operators.pop_back();
        }
        operators.push_back(current); isop = true; ++index;
    }
    while (!operators.empty()) { emit(operators.back()); operators.pop_back(); }
    return result;
}

} // 計算処理の内部補助関数用の名前空間。

void CalcOption::initialize(String arg_evaltype, bool arg_is_differentscenario) {
    m_evaltype = std::move(arg_evaltype); m_is_differentscenario = arg_is_differentscenario;
}

void ComputeException::initialize(String arg_msg, Integer arg_line, Integer arg_pos) {
    m_msg = std::move(arg_msg); m_line = arg_line; m_pos = arg_pos;
}

void Function::initialize(String arg_name, Integer arg_line, Integer arg_pos, std::vector<std::vector<Value>> arg_args) {
    m_name = lower_ascii(std::move(arg_name)); m_line = arg_line; m_pos = arg_pos;
    auto h = std::make_shared<ValueListHolder>(); for (auto& arg : arg_args) { auto ah = std::make_shared<ValueListHolder>(); ah->values = std::move(arg); h->values.emplace_back(std::static_pointer_cast<Object>(ah)); }
    m_args = boxed(h); m_arg = m_args;
}

String Function::representation() {
    const auto h = list_holder(m_args); String result = as_string(m_name) + "("; if (h) for (std::size_t i = 0; i < h->values.size(); ++i) { if (i) result += ", "; auto args = list_values(h->values[i]); result += "["; for (std::size_t j = 0; j < args.size(); ++j) { if (j) result += ", "; result += repr_value(args[j]); } result += "]"; } return result + ")";
}

std::shared_ptr<ValueType> Function::call(std::shared_ptr<CalcOption> arg_option) {
    const String name = lower_ascii(as_string(m_name)); const auto h = list_holder(m_args); std::vector<::cw::lib::Callback> callbacks;
    if (h) for (const auto& arg_holder : h->values) {
        const auto arg = list_values(arg_holder); callbacks.emplace_back([arg, arg_option](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) { return Value(std::static_pointer_cast<Object>(calculate(arg, arg_option))); });
    }
    const Integer line = as_integer(m_line), pos = as_integer(m_pos);
    // Pythonの _functions と同様、登録関数を組み込み関数より優先する。
    if (auto it = g__functions.find(name); it != g__functions.end()) {
        if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&it->second.storage())) if (auto f = std::dynamic_pointer_cast<FunctionEntry>(*p)) {
            return f->callback(callbacks, arg_option, line, pos);
        }
    }
    if (name == "isnovelmode" || name == "novelmode") {
        _chk_argscount(callbacks, 0, "ISNOVELMODE", line, pos);
        return boolean_result(::cw::novel::enabled(), line, pos);
    }
    if (name == "max") return _func_max(callbacks, arg_option, line, pos);
    if (name == "min") return _func_min(callbacks, arg_option, line, pos);
    if (name == "len") return _func_len(callbacks, arg_option, line, pos);
    if (name == "find") return _func_find(callbacks, arg_option, line, pos);
    if (name == "left") return _func_left(callbacks, arg_option, line, pos);
    if (name == "right") return _func_right(callbacks, arg_option, line, pos);
    if (name == "mid") return _func_mid(callbacks, arg_option, line, pos);
    if (name == "str") return _func_str(callbacks, arg_option, line, pos);
    if (name == "is_str") return _func_is_str(callbacks, arg_option, line, pos);
    if (name == "is_number") return _func_is_number(callbacks, arg_option, line, pos);
    if (name == "is_list") return _func_is_list(callbacks, arg_option, line, pos);
    if (name == "value") return _func_value(callbacks, arg_option, line, pos);
    if (name == "int") return _func_int(callbacks, arg_option, line, pos);
    if (name == "if") return _func_if(callbacks, arg_option, line, pos);
    if (name == "and_d") return _func_and_d(callbacks, arg_option, line, pos);
    if (name == "or_d") return _func_or_d(callbacks, arg_option, line, pos);
    if (name == "var") return _func_var(callbacks, arg_option, line, pos);
    if (name == "flagvalue") return _func_flagvalue(callbacks, arg_option, line, pos);
    if (name == "flagtext") return _func_flagtext(callbacks, arg_option, line, pos);
    if (name == "stepvalue") return _func_stepvalue(callbacks, arg_option, line, pos);
    if (name == "steptext") return _func_steptext(callbacks, arg_option, line, pos);
    if (name == "stepmax") return _func_stepmax(callbacks, arg_option, line, pos);
    if (name == "dice") return _func_dice(callbacks, arg_option, line, pos);
    if (name == "selected") return _func_selected(callbacks, arg_option, line, pos);
    if (name == "latestusecardowner") return _func_latestusecardowner(callbacks, arg_option, line, pos);
    if (name == "charexists") return _func_charexists(callbacks, arg_option, line, pos);
    if (name == "casttype") return _func_casttype(callbacks, arg_option, line, pos);
    if (name == "castname") return _func_castname(callbacks, arg_option, line, pos);
    if (name == "findcoupon") return _func_findcoupon(callbacks, arg_option, line, pos);
    if (name == "coupontext") return _func_coupontext(callbacks, arg_option, line, pos);
    if (name == "findgossip") return _func_findgossip(callbacks, arg_option, line, pos);
    if (name == "gossiptext") return _func_gossiptext(callbacks, arg_option, line, pos);
    if (name == "partyname") return _func_partyname(callbacks, arg_option, line, pos);
    if (name == "list") return _func_list(callbacks, arg_option, line, pos);
    if (name == "list_e") return _func_list_e(callbacks, arg_option, line, pos);
    if (name == "at") return _func_at(callbacks, arg_option, line, pos);
    if (name == "llen") return _func_llen(callbacks, arg_option, line, pos);
    if (name == "lfind") return _func_lfind(callbacks, arg_option, line, pos);
    if (name == "lleft") return _func_lleft(callbacks, arg_option, line, pos);
    if (name == "lright") return _func_lright(callbacks, arg_option, line, pos);
    if (name == "lmid") return _func_lmid(callbacks, arg_option, line, pos);
    if (name == "partymoney") return _func_partymoney(callbacks, arg_option, line, pos);
    if (name == "partynumber") return _func_partynumber(callbacks, arg_option, line, pos);
    if (name == "yadoname") return _func_yadoname(callbacks, arg_option, line, pos);
    if (name == "skintype") return _func_skintype(callbacks, arg_option, line, pos);
    if (name == "battleround") return _func_battleround(callbacks, arg_option, line, pos);
    if (name == "castlevel") return _func_castlevel(callbacks, arg_option, line, pos);
    if (name == "couponvalue") return _func_couponvalue(callbacks, arg_option, line, pos);
    if (name == "liferatio") return _func_liferatio(callbacks, arg_option, line, pos);
    if (name == "statusvalue") return _func_statusvalue(callbacks, arg_option, line, pos);
    if (name == "statusround") return _func_statusround(callbacks, arg_option, line, pos);
    if (name == "selectedcard") return _func_selectedcard(callbacks, arg_option, line, pos);
    if (name == "cardname") return _func_cardname(callbacks, arg_option, line, pos);
    if (name == "cardtype") return _func_cardtype(callbacks, arg_option, line, pos);
    if (name == "cardrarity") return _func_cardrarity(callbacks, arg_option, line, pos);
    if (name == "cardprice") return _func_cardprice(callbacks, arg_option, line, pos);
    if (name == "cardlevel") return _func_cardlevel(callbacks, arg_option, line, pos);
    if (name == "cardcount") return _func_cardcount(callbacks, arg_option, line, pos);
    if (name == "findkeycode") return _func_findkeycode(callbacks, arg_option, line, pos);
    if (name == "findkeycodeswith") return _func_find_keycodes_with(callbacks, arg_option, line, pos);
    if (name == "getrealmaterialpathofcard") return _get_real_material_path_of_card(callbacks, arg_option, line, pos);
    if (name == "gettargetmenukeycodes") return _get_target_menu_keycodes(callbacks, arg_option, line, pos);
    if (name == "getcardrefbykeycode") return _func_get_card_ref_by_keycode(callbacks, arg_option, line, pos);
    if (name == "getkeycodeofcardwith") return _func_get_keycode_of_card_with(callbacks, arg_option, line, pos);
    if (name == "zip") return _func_zip(callbacks, arg_option, line, pos);
    if (name == "getcardsofscenariobykeycode") return _func_get_cards_of_scenario_by_keycode(callbacks, arg_option, line, pos);
    if (name == "atdef") return _func_at_def(callbacks, arg_option, line, pos);
    if (name == "lower") return _func_lower(callbacks, arg_option, line, pos);
    if (name == "lfindall") return _func_lfind_all(callbacks, arg_option, line, pos);
    if (name == "getpropertytextofcardofscenario") return _func_get_property_text_of_card_of_scenario(callbacks, arg_option, line, pos);
    if (name == "getitemwithcopyvariables") return _get_item_with_copy_variables(callbacks, arg_option, line, pos);
    if (name == "getskillwithcopyvariables") return _get_skill_with_copy_variables(callbacks, arg_option, line, pos);
    if (name == "getbeastwithcopyvariables") return _get_beast_with_copy_variables(callbacks, arg_option, line, pos);
    if (name == "keycodetext") return _func_keycodetext(callbacks, arg_option, line, pos);
    if (name == "unixtime") return _func_unixtime(callbacks, arg_option, line, pos);
    if (name == "for") return _func_for(callbacks, arg_option, line, pos);
    if (name == "switch") return _func_switch(callbacks, arg_option, line, pos);
    if (name == "sum") return _func_sum(callbacks, arg_option, line, pos);
    if (name == "floor") return _func_floor(callbacks, arg_option, line, pos);
    if (name == "ceil") return _func_ceil(callbacks, arg_option, line, pos);
    if (name == "sind") return _func_sind(callbacks, arg_option, line, pos);
    if (name == "cosd") return _func_cosd(callbacks, arg_option, line, pos);
    if (name == "tand") return _func_tand(callbacks, arg_option, line, pos);
    if (name == "distance") return _func_distance(callbacks, arg_option, line, pos);
    if (name == "parseint") return _func_parseint(callbacks, arg_option, line, pos);
    if (name == "parsefloat") return _func_parsefloat(callbacks, arg_option, line, pos);
    if (name == "eval") return _func_eval(callbacks, arg_option, line, pos);
    if (name == "scenarioname") return _func_scenarioname(callbacks, arg_option, line, pos);
    if (name == "cardmaxuselimit") return _func_cardmaxuselimit(callbacks, arg_option, line, pos);
    if (name == "datetime") return _func_datetime(callbacks, arg_option, line, pos);
    if (name == "format") return _func_format(callbacks, arg_option, line, pos);
    if (name == "concatstrings") return _func_concatstrings(callbacks, arg_option, line, pos);
    if (name == "makelist") return _func_makelist(callbacks, arg_option, line, pos);
    if (name == "getrandomlist") return _func_getrandomlist(callbacks, arg_option, line, pos);
    if (name == "lfindrand") return _func_lfindrand(callbacks, arg_option, line, pos);
    if (name == "pickbyindices") return _func_pickbyindices(callbacks, arg_option, line, pos);
    if (name == "lgetrandom") return _func_lgetrandom(callbacks, arg_option, line, pos);
    if (name == "lset") return _func_lset(callbacks, arg_option, line, pos);
    if (name == "lsetmulti") return _func_lsetmulti(callbacks, arg_option, line, pos);
    if (name == "lremove") return _func_lremove(callbacks, arg_option, line, pos);
    if (name == "lremovemulti") return _func_lremovemulti(callbacks, arg_option, line, pos);
    if (name == "linsert") return _func_linsert(callbacks, arg_option, line, pos);
    if (name == "lunique") return _func_lunique(callbacks, arg_option, line, pos);
    if (name == "lremoveall") return _func_lremoveall(callbacks, arg_option, line, pos);
    if (name == "lcount") return _func_lcount(callbacks, arg_option, line, pos);
    if (name == "contain") return _func_contain(callbacks, arg_option, line, pos);
    if (name == "inpolygon") return _func_inpolygon(callbacks, arg_option, line, pos);
    if (name == "mousex") return _func_mousex(callbacks, arg_option, line, pos);
    if (name == "mousey") return _func_mousey(callbacks, arg_option, line, pos);
    if (name == "getvariantvalueofcard") return _func_getvariantvalueofcard(callbacks, arg_option, line, pos);
    if (name == "getflagvalueofcard") return _func_getflagvalueofcard(callbacks, arg_option, line, pos);
    if (name == "getstepvalueofcard") return _func_getstepvalueofcard(callbacks, arg_option, line, pos);
    if (name == "has_variantvalue") return _func_has_variantvalue(callbacks, arg_option, line, pos);
    if (name == "has_step") return _func_has_step(callbacks, arg_option, line, pos);
    if (name == "has_flag") return _func_has_flag(callbacks, arg_option, line, pos);
    if (name == "setvariantvalueofcard") return _func_setvariantvalueofcard(callbacks, arg_option, line, pos);
    if (name == "setstepvalueofcard") return _func_setstepvalueofcard(callbacks, arg_option, line, pos);
    if (name == "setflagvalueofcard") return _func_setflagvalueofcard(callbacks, arg_option, line, pos);
    if (name == "cardinfo" || g__structures.find(name) != g__structures.end()) return _create_structure(struct_info(name), callbacks, arg_option, as_integer(m_line), as_integer(m_pos));
    FunctionIsNotDefinedException e; e.initialize("Function \"" + name + "\" is not defined.", name, as_integer(m_line), as_integer(m_pos)); throw e;
}

void Operator::initialize(String arg_py_operator, Integer arg_line, Integer arg_pos) { m_py_operator = std::move(arg_py_operator); m_line = arg_line; m_pos = arg_pos; }
void StructureMember::initialize(String arg_name, bool arg_is_public, Value arg_defvalue, bool arg_type_check, String arg_vtype, std::optional<Value> arg_min_value, String arg_struct_name) {
    m_name = std::move(arg_name); m_is_public = arg_is_public; m_defvalue = std::move(arg_defvalue); m_type_check = arg_type_check; m_vtype = std::move(arg_vtype); m_min_value = arg_min_value ? *arg_min_value : Value(); m_struct_name = std::move(arg_struct_name);
}
void StructureInfo::initialize(String arg_name, bool arg_is_public, std::vector<std::shared_ptr<StructureMember>> arg_members, Integer arg_required_member_num) {
    m_name = std::move(arg_name); m_is_public = arg_is_public; m_required_member_num = arg_required_member_num; auto h = std::make_shared<MemberListHolder>();
    // Pythonの __init__ と同様、メンバー一覧が空でも位置の表を置き換える。
    m__index_table.clear();
    for (Integer i = 0; i < static_cast<Integer>(arg_members.size()); ++i) { m__index_table[as_string(arg_members[static_cast<std::size_t>(i)]->m_name)] = i; h->values.push_back(std::move(arg_members[static_cast<std::size_t>(i)])); } m_members = boxed(h);
}
Integer StructureInfo::index_of(String arg_name) { const auto it = m__index_table.find(::cw::lib::encoding::lower(arg_name)); return it == m__index_table.end() ? -1 : as_integer(it->second); }
void Symbol::initialize(String arg_symbol, Integer arg_line, Integer arg_pos) { m_symbol = std::move(arg_symbol); m_line = arg_line; m_pos = arg_pos; }
void Token::initialize(String arg_token, Integer arg_line, Integer arg_pos) { m_token = std::move(arg_token); m_line = arg_line; m_pos = arg_pos; }
String Token::representation() { return "Token(" + as_string(m_token) + ", " + std::to_string(as_integer(m_line)) + ":" + std::to_string(as_integer(m_pos)) + ")"; }
void UnaryOperator::initialize(String arg_py_operator, Integer arg_line, Integer arg_pos) { m_py_operator = std::move(arg_py_operator); m_line = arg_line; m_pos = arg_pos; }
String UnaryOperator::representation() { return "UOp(" + as_string(m_py_operator) + ", " + std::to_string(as_integer(m_line)) + ":" + std::to_string(as_integer(m_pos)) + ")"; }
String Operator::representation() { return "Op(" + as_string(m_py_operator) + ", " + std::to_string(as_integer(m_line)) + ":" + std::to_string(as_integer(m_pos)) + ")"; }

std::shared_ptr<ValueType> Symbol::to_value_obj() {
    // ネイティブ版の意図的な方針として、組み込み定数を拡張マップより
    // 優先する。 Pythonの _symbols は変更可能な辞
    // 書だが、ネイティブの拡張から標準のシンボル値は置換できない。
    const String key = lower_ascii(as_string(m_symbol));
    static const std::map<String, Value> standard_symbols{
        {"newline", Value(String("\n"))}, {"player", Value(1.0)}, {"enemy", Value(2.0)}, {"friend", Value(3.0)},
        {"skill", Value(1.0)}, {"item", Value(2.0)}, {"beast", Value(3.0)}, {"actioncard", Value(-1.0)},
        {"rare", Value(1.0)}, {"premier", Value(2.0)}, {"poison", Value(8.0)}, {"sleep", Value(9.0)},
        {"bind", Value(10.0)}, {"paralyze", Value(11.0)}, {"confuse", Value(12.0)}, {"overheat", Value(13.0)},
        {"brave", Value(14.0)}, {"panic", Value(15.0)}, {"silence", Value(16.0)}, {"faceup", Value(17.0)},
        {"antimagic", Value(18.0)}, {"enhaction", Value(19.0)}, {"enhavoid", Value(20.0)},
        {"enhresist", Value(21.0)}, {"enhdefense", Value(22.0)}
    };
    if (const auto standard = standard_symbols.find(key); standard != standard_symbols.end())
        return _variantvalue_to_valuetype(standard->second, as_integer(m_line), as_integer(m_pos));
    if (const auto extension = g__symbols.find(key); extension != g__symbols.end())
        return _variantvalue_to_valuetype(extension->second, as_integer(m_line), as_integer(m_pos));
    SemanticsException e; e.initialize("Unknown symbol: " + python_upper(as_string(m_symbol)), as_integer(m_line), as_integer(m_pos)); throw e;
}

std::shared_ptr<ValueType> ValueType::to_value_obj() { return shared_from_this(); }
String ValueType::to_str() { return {}; }
void BooleanValue::initialize(bool arg_s, Integer arg_line, Integer arg_pos) { m_value = arg_s; m_line = arg_line; m_pos = arg_pos; }
String BooleanValue::to_str() { return std::get_if<bool>(&m_value.storage()) && *std::get_if<bool>(&m_value.storage()) ? "TRUE" : "FALSE"; }
String BooleanValue::representation() { return "Boolean(\"" + String(std::get_if<bool>(&m_value.storage()) && *std::get_if<bool>(&m_value.storage()) ? "True" : "False") + "\")"; }
void DecimalValue::initialize(Value arg_s, Integer arg_line, Integer arg_pos) {
    try {
        m_decimal = decimal_from_value(arg_s);
    } catch (const ::cw::lib::DecimalError& error) {
        SemanticsException e;
        e.initialize("Invalid Decimal: " + String(error.what()), arg_line, arg_pos);
        throw e;
    }
    m_value = decimal_to_compat(m_decimal);
    m_line = arg_line;
    m_pos = arg_pos;
}
void DecimalValue::initialize(Decimal arg_s, Integer arg_line, Integer arg_pos) {
    m_decimal = std::move(arg_s);
    m_value = decimal_to_compat(m_decimal);
    m_line = arg_line;
    m_pos = arg_pos;
}
String DecimalValue::to_str() {
    if (m_decimal.is_nan() && !m_decimal.is_snan()) return "nan";
    // Pythonの DecimalValue.to_str と同様、小数八桁で整形して末尾の0を除き、その後で末尾の小数点を除く。
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::fixed << std::setprecision(8);
    try { oss << m_decimal.to_real(); }
    catch (const std::exception& error) { throw_semantics("Invalid Decimal formatting: " + String(error.what()), as_integer(m_line), as_integer(m_pos)); }
    String s = oss.str();
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s.empty() || s == "-") s = "0";
    return s;
}
String DecimalValue::representation() { return "Decimal(" + python_decimal_text(m_decimal) + ")"; }
void StringValue::initialize(String arg_s, Integer arg_line, Integer arg_pos) { m_value = std::move(arg_s); m_line = arg_line; m_pos = arg_pos; }
String StringValue::to_str() { return as_string(m_value); }
String StringValue::representation() { return "String(\"" + as_string(m_value) + "\")"; }

void ListValue::initialize(std::vector<Value> arg_s, Integer arg_line, Integer arg_pos) { auto h = std::make_shared<ValueListHolder>(); h->values = std::move(arg_s); m_value = boxed(h); m_line = arg_line; m_pos = arg_pos; } // 所有権の意図的な境界として、ネイティブ版はベクターの複製を所有する。 Python版は渡されたリスト自体を保持し、その後の変更も反映する。
std::shared_ptr<ValueType> ListValue::eval(Integer arg_i) {
    auto h = list_holder(m_value);
    // 直接のAPI呼び出しでも、負の位置を含むPythonのリスト参照規則を使う。
    const auto size = h ? static_cast<Integer>(h->values.size()) : 0;
    const auto index = arg_i < 0 ? arg_i + size : arg_i;
    if (!h || index < 0 || index >= size) {
        SemanticsException e; e.initialize("List index out of range.", as_integer(m_line), as_integer(m_pos)); throw e;
    }
    auto& slot = h->values[static_cast<std::size_t>(index)];
    auto result = eval_value(slot);
    if (object_as<CallbackHolder>(slot)) slot = boxed(result);
    return result;
}
String ListValue::to_str() { return ::cw::data::Variant::value_to_str(to_variantvalue(shared_from_this()), false); }
String ListValue::representation() { return to_str(); }
void StructureValue::initialize(String arg_name, std::vector<Value> arg_s, Integer arg_line, Integer arg_pos) {
    m_name = ::cw::lib::encoding::lower(arg_name); m_line = arg_line; m_pos = arg_pos; auto info = struct_info(as_string(m_name)); auto members = members_of(*info); while (arg_s.size() < members.size()) arg_s.push_back(boxed(_variantvalue_to_valuetype(members[arg_s.size()]->m_defvalue, arg_line, arg_pos))); auto h = std::make_shared<ValueListHolder>(); h->values = std::move(arg_s); m_value = boxed(h);
}
std::shared_ptr<ValueType> StructureValue::eval(Integer arg_i) {
    auto h = list_holder(m_value);
    const auto size = h ? static_cast<Integer>(h->values.size()) : 0;
    const auto index = arg_i < 0 ? arg_i + size : arg_i;
    if (!h || index < 0 || index >= size) throw_semantics("Structure member index out of range.", as_integer(m_line), as_integer(m_pos));
    auto& slot = h->values[static_cast<std::size_t>(index)];
    const bool was_callback = static_cast<bool>(object_as<CallbackHolder>(slot));
    auto result = eval_value(slot);
    if (was_callback) {
        const auto definitions = members_of(*struct_info(as_string(m_name)));
        // Python版と同様、値と定義を別々に参照する。 診断には
        // 指定位置を保ち、それぞれのリストの長さで個別に正規化する。
        const auto definition_size = static_cast<Integer>(definitions.size());
        const auto definition_index = arg_i < 0 ? arg_i + definition_size : arg_i;
        if (definition_index < 0 || definition_index >= definition_size)
            throw_semantics("Structure member definition is missing.", as_integer(m_line), as_integer(m_pos));
        const auto& definition = definitions[static_cast<std::size_t>(definition_index)];
        const bool type_check = std::get_if<bool>(&definition->m_type_check.storage()) &&
                                *std::get_if<bool>(&definition->m_type_check.storage());
        if (type_check) {
            const auto expected = as_string(definition->m_vtype);
            const auto function = as_string(m_name);
            if (expected == "Number") {
                if (definition->m_min_value.is_none()) (void)_chk_decimal(result, function, arg_i);
                else (void)_chk_minvalue(result, function, arg_i, definition->m_min_value);
            } else if (expected == "String") (void)_chk_string(result, function, arg_i);
            else if (expected == "Boolean") (void)_chk_boolean(result, function, arg_i);
            else if (expected == "List") (void)_chk_list(result, function, arg_i);
            else if (expected == "Structure") (void)_chk_structure(result, function, arg_i, as_string(definition->m_struct_name));
            else throw_semantics("Unknown structure member type: " + expected, line_of(result), pos_of(result));
        }
        slot = boxed(result);
    }
    return result;
}
String StructureValue::to_str() { return ::cw::data::Variant::value_to_str(to_variantvalue(shared_from_this()), false); }
String StructureValue::representation() { return to_str(); }

void DifferentScenarioException::initialize(String msg, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); }
void DifferentStructureException::initialize(String msg, String lhs, String rhs, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); m_lhs_name = std::move(lhs); m_rhs_name = std::move(rhs); }
void ExprPermissionError::initialize(String msg, std::shared_ptr<StructureInfo> info,
                                     std::optional<std::shared_ptr<StructureMember>> m,
                                     Integer line, Integer pos) {
    ComputeException::initialize(std::move(msg), line, pos);
    m_info = boxed(std::move(info));
    // Python版と同様、明示的なNoneも含め、初期化のたびに self.m を代入する。
    m_m = m && *m ? boxed(std::move(*m)) : Value{};
}
void FlagNotFoundException::initialize(String msg, String path, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); m_path = std::move(path); }
void FunctionIsNotDefinedException::initialize(String msg, String name, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); m_func_name = std::move(name); }
void InvalidArgumentException::initialize(String msg, String name, Integer index, String value, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); m_func_name = std::move(name); m_arg_index = index; m_arg_value = std::move(value); }
void InvalidStepValueException::initialize(String msg, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); }
void ListIndexOutOfRangeException::initialize(String msg, String name, Integer index, String value, Integer n, Integer len, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); m_func_name = std::move(name); m_arg_index = index; m_arg_value = std::move(value); m_n = n; m_list_len = len; }
void SemanticsException::initialize(String msg, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); }
void StepNotFoundException::initialize(String msg, String path, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); m_path = std::move(path); }
void TokanizeException::initialize(String msg, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); }
void VariantNotFoundException::initialize(String msg, String path, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); m_path = std::move(path); }
void ZeroDivisionException::initialize(String msg, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); }

template<typename E>
void init_arg_exception(E& e, String msg, String name, Integer index, String value, Integer line, Integer pos) { e.initialize(std::move(msg), std::move(name), index, std::move(value), line, pos); }
void ArgumentIsNotBooleanException::initialize(String msg, String name, Integer index, String value, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); m_func_name = std::move(name); m_arg_index = index; m_arg_value = std::move(value); }
void ArgumentIsNotDecimalException::initialize(String msg, String name, Integer index, String value, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); m_func_name = std::move(name); m_arg_index = index; m_arg_value = std::move(value); }
void ArgumentIsNotListException::initialize(String msg, String name, Integer index, String value, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); m_func_name = std::move(name); m_arg_index = index; m_arg_value = std::move(value); }
void ArgumentIsNotStringException::initialize(String msg, String name, Integer index, String value, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); m_func_name = std::move(name); m_arg_index = index; m_arg_value = std::move(value); }
void ArgumentIsNotStructureException::initialize(String msg, String name, Integer index, String value, String struct_name, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); m_func_name = std::move(name); m_arg_index = index; m_arg_value = std::move(value); m_struct_name = std::move(struct_name); }
void ArgumentsCountException::initialize(String msg, String name, Integer line, Integer pos) { ComputeException::initialize(std::move(msg), line, pos); m_func_name = std::move(name); }

std::tuple<Decimal, Decimal> Operator::chk_num(std::shared_ptr<ValueType> lhs, std::shared_ptr<ValueType> rhs) {
    auto l = std::dynamic_pointer_cast<DecimalValue>(lhs), r = std::dynamic_pointer_cast<DecimalValue>(rhs);
    if (!l) throw_semantics("lhs [" + lhs->to_str() + "] is not number.", line_of(lhs), pos_of(lhs));
    if (!r) throw_semantics("rhs [" + rhs->to_str() + "] is not number.", line_of(rhs), pos_of(rhs));
    return {l->m_decimal, r->m_decimal};
}
std::tuple<String, String> Operator::chk_str(std::shared_ptr<ValueType> lhs, std::shared_ptr<ValueType> rhs) {
    auto l = std::dynamic_pointer_cast<StringValue>(lhs), r = std::dynamic_pointer_cast<StringValue>(rhs); if (!l) throw_semantics("lhs [" + lhs->to_str() + "] is not string.", line_of(lhs), pos_of(lhs)); if (!r) throw_semantics("rhs [" + rhs->to_str() + "] is not string.", line_of(rhs), pos_of(rhs)); return {as_string(l->m_value), as_string(r->m_value)};
}
std::tuple<bool, bool> Operator::chk_bool(std::shared_ptr<ValueType> lhs, std::shared_ptr<ValueType> rhs) {
    auto l = std::dynamic_pointer_cast<BooleanValue>(lhs), r = std::dynamic_pointer_cast<BooleanValue>(rhs); if (!l) throw_semantics("lhs [" + lhs->to_str() + "] is not boolean.", line_of(lhs), pos_of(lhs)); if (!r) throw_semantics("rhs [" + rhs->to_str() + "] is not boolean.", line_of(rhs), pos_of(rhs)); return {*std::get_if<bool>(&l->m_value.storage()), *std::get_if<bool>(&r->m_value.storage())};
}
std::shared_ptr<ListValue> Operator::chk_list(std::shared_ptr<ValueType> val) { auto p = std::dynamic_pointer_cast<ListValue>(val); if (!p) throw_semantics("rhs [" + val->to_str() + "] is not list.", line_of(val), pos_of(val)); return p; }
std::shared_ptr<StructureValue> Operator::chk_struct(std::shared_ptr<ValueType> val) { auto p = std::dynamic_pointer_cast<StructureValue>(val); if (!p) throw_semantics("rhs [" + val->to_str() + "] is not structure.", line_of(val), pos_of(val)); return p; }
std::shared_ptr<Symbol> Operator::chk_structmembername(Value val) { auto p = value_symbol(val); if (!p) { auto t = value_type(val); throw_semantics("rhs [" + (t ? t->to_str() : String{}) + "] is not struct member name.", t ? line_of(t) : 0, t ? pos_of(t) : 0); } return p; }

bool Operator::equals(std::shared_ptr<ValueType> lhs, std::shared_ptr<ValueType> rhs, String op, bool in_list) {
    if (auto ll = std::dynamic_pointer_cast<ListValue>(lhs)) {
        auto rr = in_list ? std::dynamic_pointer_cast<ListValue>(rhs) : chk_list(rhs);
        if (!rr) return op == "<>";
        const auto left_size = list_values(ll->m_value).size(), right_size = list_values(rr->m_value).size();
        for (std::size_t i = 0; i < std::min(left_size, right_size); ++i) {
            const auto left = ll->eval(static_cast<Integer>(i));
            const auto right = rr->eval(static_cast<Integer>(i));
            if (!equals(left, right, "=", true)) return op == "<>";
        }
        const bool result = left_size == right_size;
        return op == "<>" ? !result : result;
    }
    if (auto ls = std::dynamic_pointer_cast<StructureValue>(lhs)) {
        auto rs = in_list ? std::dynamic_pointer_cast<StructureValue>(rhs) : chk_struct(rhs);
        if (!rs) return op == "<>";
        if (structure_name(*ls) != structure_name(*rs)) { if (in_list) return op == "<>"; DifferentStructureException e; e.initialize("different structure " + structure_name(*ls) + ":" + structure_name(*rs) + ".", structure_name(*ls), structure_name(*rs), line_of(rs), pos_of(rs)); throw e; }
        const auto count = std::min(structure_values(*ls).size(), structure_values(*rs).size());
        for (std::size_t i = 0; i < count; ++i) {
            const auto left = ls->eval(static_cast<Integer>(i));
            const auto right = rs->eval(static_cast<Integer>(i));
            if (!equals(left, right, "=", true)) return op == "<>";
        }
        return op == "=";
    }
    if (!in_list && (std::dynamic_pointer_cast<StringValue>(lhs) || std::dynamic_pointer_cast<StringValue>(rhs))) {
        if (std::dynamic_pointer_cast<ListValue>(rhs)) throw_semantics("lhs [" + lhs->to_str() + "] is not list.", line_of(lhs), pos_of(lhs));
        if (std::dynamic_pointer_cast<StructureValue>(rhs)) throw_semantics("lhs [" + lhs->to_str() + "] is not structure.", line_of(lhs), pos_of(lhs));
        if (std::dynamic_pointer_cast<ListValue>(lhs)) throw_semantics("rhs [" + rhs->to_str() + "] is not list.", line_of(rhs), pos_of(rhs));
        if (std::dynamic_pointer_cast<StructureValue>(lhs)) throw_semantics("rhs [" + rhs->to_str() + "] is not structure.", line_of(rhs), pos_of(rhs));
        const bool result = lhs->to_str() == rhs->to_str();
        return op == "<>" ? !result : result;
    }
    if (auto l = std::dynamic_pointer_cast<StringValue>(lhs)) { if (in_list && !std::dynamic_pointer_cast<StringValue>(rhs)) return op == "<>"; auto r = std::dynamic_pointer_cast<StringValue>(rhs); if (!r) return op == "<>"; bool result = as_string(l->m_value) == as_string(r->m_value); return op == "<>" ? !result : result; }
    if (auto l = std::dynamic_pointer_cast<BooleanValue>(lhs)) {
        if (in_list && !std::dynamic_pointer_cast<BooleanValue>(rhs)) return op == "<>";
        const auto [left, right] = chk_bool(lhs, rhs);
        const bool result = left == right;
        return op == "<>" ? !result : result;
    }
    if (in_list && !std::dynamic_pointer_cast<DecimalValue>(rhs)) return op == "<>";
    auto l = std::dynamic_pointer_cast<DecimalValue>(lhs), r = std::dynamic_pointer_cast<DecimalValue>(rhs);
    // 診断だけの意図的な違いとして、不正な数値オペランドには左側の位置で汎
    // 用の SemanticsException を出す。 Python版
    // の左右別の文言・位置は再現しないが、有効な数値の等価判定は変えない。
    if (!l || !r) throw_semantics("value is not number.", line_of(lhs), pos_of(lhs));
    const bool result = l->m_decimal == r->m_decimal;
    return op == "<>" ? !result : result;
}

// 直接呼び出し用の意図的な変換として、ここでSymbolのオペランドを解決する。 Python
// の UnaryOperator.call は未解決のSymbolを拒否するが、通常の
// calculateは演算前に解決済み。 直接呼ぶネイティブ側にも同じ変換を提供する。
std::shared_ptr<ValueType> UnaryOperator::call(Value rhs) {
    auto v = value_symbol(rhs) ? value_symbol(rhs)->to_value_obj() : value_type(rhs);
    if (!v) throw_semantics("Invalid value.", as_integer(m_line), as_integer(m_pos)); const String op = as_string(m_py_operator);
    if (op == "+" || op == "-") {
        auto d = std::dynamic_pointer_cast<DecimalValue>(v);
        if (!d) throw_semantics("value [" + v->to_str() + "] is not number.", line_of(v), pos_of(v));
        if (op == "+") return decimal_result(d->m_decimal, as_integer(m_line), as_integer(m_pos));
        return decimal_result(d->m_decimal.minus(),
                              as_integer(m_line), as_integer(m_pos));
    }
    if (lower_ascii(op) == "not") { auto b = std::dynamic_pointer_cast<BooleanValue>(v); if (!b) throw_semantics("value [" + v->to_str() + "] is not boolean.", line_of(v), pos_of(v)); return boolean_result(!*std::get_if<bool>(&b->m_value.storage()), as_integer(m_line), as_integer(m_pos)); }
    throw_semantics("Invalid operator: " + op, as_integer(m_line), as_integer(m_pos));
}

// UnaryOperator::call と同じ、ネイティブの直接呼び出し用のSymbol変換を両オペランドに
// 適用する。 Pythonの Operator.call はcalculateによる解決済みの値を前提とする。
std::shared_ptr<ValueType> Operator::call(Value lhs_value, Value rhs_value, std::shared_ptr<CalcOption> option, std::optional<String> requested) {
    const String op = requested ? *requested : as_string(m_py_operator);
    auto lhs = value_symbol(lhs_value) ? value_symbol(lhs_value)->to_value_obj() : value_type(lhs_value);
    if (!lhs) throw_semantics("Invalid lhs.", as_integer(m_line), as_integer(m_pos));
    if (op == ".") {
        auto st = chk_struct(lhs);
        auto rs = chk_structmembername(rhs_value);
        std::shared_ptr<StructureInfo> info;
        try { info = struct_info(structure_name(*st)); }
        catch (const std::runtime_error&) { throw_semantics("structure " + structure_name(*st) + " is not found.", as_integer(rs->m_line), as_integer(rs->m_pos)); }
        const Integer idx = info->index_of(as_string(rs->m_symbol)); // Python版の不具合への意図的な修正として、非公開の検査前に存在しないメンバーを拒否する。
        if (idx < 0) throw_semantics("structure " + structure_name(*st) + " has not been " + as_string(rs->m_symbol) + ".", as_integer(rs->m_line), as_integer(rs->m_pos)); // Python版は先に members[-1] を参照するため、最後のメンバーが非公開だと、存在しない名前のエラーが隠れる。
        const auto members = members_of(*info);
        if (idx < static_cast<Integer>(members.size()) && !*std::get_if<bool>(&members[idx]->m_is_public.storage()) && as_string(option->m_evaltype) != "Test") {
            ExprPermissionError e; e.initialize("Structure member " + python_upper(as_string(info->m_name)) + "." + python_upper(as_string(members[idx]->m_name)) + " is not accesible.", info, members[idx], as_integer(m_line), as_integer(m_pos)); throw e;
        }
        return st->eval(idx);
    }
    auto rhs = value_symbol(rhs_value) ? value_symbol(rhs_value)->to_value_obj() : value_type(rhs_value);
    if (!rhs) throw_semantics("Invalid rhs.", as_integer(m_line), as_integer(m_pos));
    if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
        auto [l, r] = chk_num(lhs, rhs);
        {
            if (op == "+") return decimal_result(l.add(r), as_integer(m_line), as_integer(m_pos));
            if (op == "-") return decimal_result(l.subtract(r), as_integer(m_line), as_integer(m_pos));
            if (op == "*") return decimal_result(l.multiply(r), as_integer(m_line), as_integer(m_pos));
            if ((op == "/" || op == "%") && r.is_zero()) {
                ZeroDivisionException error;
                error.initialize("Division by zero.", as_integer(m_line), as_integer(m_pos));
                throw error;
            }
            if (op == "/") return decimal_result(l.divide(r), as_integer(m_line), as_integer(m_pos));
            return decimal_result(l.remainder(r), as_integer(m_line), as_integer(m_pos));
        }}
    if (op == "~") { auto ll = std::dynamic_pointer_cast<ListValue>(lhs), rr = std::dynamic_pointer_cast<ListValue>(rhs); if (ll || rr) { std::vector<Value> vals = ll ? list_values(ll->m_value) : std::vector<Value>{boxed(lhs)}; auto more = rr ? list_values(rr->m_value) : std::vector<Value>{boxed(rhs)}; vals.insert(vals.end(), more.begin(), more.end()); auto result = std::make_shared<ListValue>(); result->initialize(std::move(vals), as_integer(m_line), as_integer(m_pos)); return result; } return string_result(lhs->to_str() + rhs->to_str(), as_integer(m_line), as_integer(m_pos)); }
    if (op == "=" || op == "<>") return boolean_result(equals(lhs, rhs, op, false), as_integer(m_line), as_integer(m_pos));
    const String logical_op = lower_ascii(op);
    if (logical_op == "and" || logical_op == "or") { auto [l, r] = chk_bool(lhs, rhs); return boolean_result(logical_op == "and" ? l && r : l || r, as_integer(m_line), as_integer(m_pos)); }
    if (op == "<" || op == ">" || op == "<=" || op == ">=") {
        if (auto ll = std::dynamic_pointer_cast<ListValue>(lhs)) {
            auto rr = chk_list(rhs); const auto lv = list_values(ll->m_value), rvv = list_values(rr->m_value);
            for (std::size_t i = 0; i < std::min(lv.size(), rvv.size()); ++i) {
                const auto left = ll->eval(static_cast<Integer>(i));
                const auto right = rr->eval(static_cast<Integer>(i));
                auto b = call(Value(left), Value(right), option, std::optional<String>(op == "<=" ? "<" : op == ">=" ? ">" : op));
                if (*std::get_if<bool>(&std::dynamic_pointer_cast<BooleanValue>(b)->m_value.storage())) return b;
            }
            bool result = op == "<" ? lv.size() < rvv.size() : op == ">" ? lv.size() > rvv.size() : op == "<=" ? lv.size() <= rvv.size() : lv.size() >= rvv.size();
            return boolean_result(result, as_integer(m_line), as_integer(m_pos));
        }
        auto [l, r] = chk_num(lhs, rhs);
        const int comparison = l.compare(r);
        const bool result = op == "<" ? comparison < 0 : op == ">" ? comparison > 0 : op == "<=" ? comparison <= 0 : comparison >= 0;
        return boolean_result(result, as_integer(m_line), as_integer(m_pos));
    }
    throw_semantics("Invalid operator: " + op, as_integer(m_line), as_integer(m_pos));
}

::cw::lib::Value g__NUM_REG{};
std::map<String, Value> g__functions{};
std::map<String, std::shared_ptr<StructureInfo>> g__structures{};
std::map<String, Value> g__symbols{};
::cw::lib::Value g__test_list1{};
::cw::lib::Value g__test_list2{};
::cw::lib::Value g__test_list2_0{};
::cw::lib::Value g__test_list2_1{};

void Function_F_call___init__(Value, std::vector<Value>, std::shared_ptr<CalcOption>) {
    // Pythonの内部コールバッククラスは Function::call のク
    // ロージャーで表す。 この生成用の補助クラスに、外部から見える状態はない。
}
std::shared_ptr<ValueType> Function_F_call_eval_arg(Value arg_self) { return value_type(arg_self); }
String ListValue_to_str_to_str(Integer arg_i) { return std::to_string(arg_i); }

std::shared_ptr<StructureInfo> struct_info(String arg_name) {
    const String name = ::cw::lib::encoding::lower(arg_name); auto it = g__structures.find(name); if (it != g__structures.end()) return it->second;
    if (name == "cardinfo") {
        auto info = std::make_shared<StructureInfo>(); std::vector<std::shared_ptr<StructureMember>> members;
        auto m0 = std::make_shared<StructureMember>(); m0->initialize("castindex", false, Value(0.0), true, "Number", Value(-2.0), ""); members.push_back(m0);
        auto m1 = std::make_shared<StructureMember>(); m1->initialize("cardindex", false, Value(0.0), true, "Number", Value(0.0), ""); members.push_back(m1);
        auto m2 = std::make_shared<StructureMember>(); m2->initialize("actioncardid", false, Value(-2.0), true, "Number", std::nullopt, ""); members.push_back(m2);
        info->initialize("cardinfo", false, std::move(members), 2); g__structures[name] = info; return info;
    }
    throw std::runtime_error("Structure " + name + " has been declared.");
}

std::vector<Value> cut_optionalmembers(String arg_name, std::vector<Value> arg_members) {
    auto info = struct_info(std::move(arg_name)); const auto members = members_of(*info);
    // Python版と同様、切り詰める前にメンバー数の一致を要求する。 Release構
    // 成でも不正値を拒否するよう、assertではなくネイティブ例外で前提違反を示す。
    if (members.size() != arg_members.size())
        throw std::invalid_argument("Structure member count does not match its definition.");
    while (arg_members.size() > static_cast<std::size_t>(as_integer(info->m_required_member_num))) {
        const std::size_t i = arg_members.size() - 1;
        const auto& expected = members[i]->m_defvalue;
        const auto& actual = arg_members[i];
        if (!Operator::equals(_variantvalue_to_valuetype(expected, 0, 0),
                              _variantvalue_to_valuetype(actual, 0, 0), "=", true)) break;
        arg_members.pop_back();
    }
    return arg_members;
}
std::vector<Value> complete_optionalmembers(String arg_name, std::vector<Value> arg_members) {
    const auto members = members_of(*struct_info(std::move(arg_name)));
    while (arg_members.size() < members.size()) arg_members.push_back(members[arg_members.size()]->m_defvalue);
    return arg_members;
}
std::vector<String> structure_member_names(String arg_name) {
    std::vector<String> result;
    for (const auto& member : members_of(*struct_info(std::move(arg_name))))
        result.push_back(as_string(member->m_name));
    return result;
}
bool is_valid_structure(String arg_name, std::vector<std::tuple<String, Value>> arg_members) {
    std::shared_ptr<StructureInfo> info;
    try { info = struct_info(::cw::lib::encoding::lower(arg_name)); }
    catch (...) { return false; }
    const auto members = members_of(*info);
    if (members.size() < arg_members.size()) return false;
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (arg_members.size() <= i)
            return static_cast<std::size_t>(as_integer(info->m_required_member_num)) <= i;
        const auto& definition = members[i];
        const auto& [actual_name, value] = arg_members[i];
        // Python版と同様、正規化するのは渡されたメンバー名だけ。登録名は完全一致で扱う。
        if (as_string(definition->m_name) !=
            ::cw::lib::encoding::lower(actual_name)) return false;
        const bool type_check = std::get_if<bool>(&definition->m_type_check.storage()) &&
                                *std::get_if<bool>(&definition->m_type_check.storage());
        if (!type_check) continue;
        const String expected_type = as_string(definition->m_vtype);
        if (::cw::data::Variant::value_to_type(value) != expected_type) return false;
        if (expected_type == "Number" && !definition->m_min_value.is_none()) {
            try {
                if (decimal_from_value(value).compare(decimal_from_value(definition->m_min_value)) < 0) return false;
            } catch (...) { return false; }
        }
        if (expected_type == "Structure") {
            const auto structure = object_as<::cw::data::StructVal>(value);
            // Python版と同様、入れ子の構造名は完全一致で比較する。
            if (!structure || as_string(structure->m_name) !=
                              as_string(definition->m_struct_name)) return false;
        }
    }
    return true;
}

std::vector<Value> parse(String arg_s) {
    Integer final_line = 1, final_pos = 1;
    const auto lexed = lex_source(arg_s, final_line, final_pos); std::vector<std::shared_ptr<Token>> tokens; tokens.reserve(lexed.size()); for (const auto& l : lexed) { auto t = std::make_shared<Token>(); t->initialize(l.token, l.line, l.pos); tokens.push_back(std::move(t)); }
    std::size_t i = 0; auto result = parse_semantics_impl(tokens, i); if (i != tokens.size()) { throw_semantics("Invalid semantics.", final_line, final_pos); } return result;
}
std::tuple<Integer, std::vector<std::vector<Value>>> parse_parse_arguments(std::vector<std::shared_ptr<Token>> tokens, Integer arg_i) {
    std::size_t i = static_cast<std::size_t>(arg_i);
    auto args = parse_arguments_impl(tokens, i);
    return {static_cast<Integer>(i), std::move(args)};
}
std::tuple<Integer, std::vector<Value>> parse_parse_semantics(std::vector<std::shared_ptr<Token>> tokens, Integer arg_i) {
    std::size_t i = static_cast<std::size_t>(arg_i);
    auto result = parse_semantics_impl(tokens, i);
    return {static_cast<Integer>(i), std::move(result)};
}

std::shared_ptr<ValueType> calculate(std::vector<Value> arg_st, std::shared_ptr<CalcOption> arg_option) {
    std::vector<Value> stack; for (const auto& node : arg_st) { if (auto f = value_function(node)) stack.emplace_back(boxed(f->call(arg_option))); else if (auto u = value_unary(node)) { if (stack.empty()) throw_semantics("Invalid semantics.", as_integer(u->m_line), as_integer(u->m_pos)); Value rhs = stack.back(); stack.pop_back(); stack.emplace_back(boxed(u->call(rhs))); } else if (auto o = value_operator(node)) { if (stack.size() < 2) throw_semantics("Invalid semantics.", as_integer(o->m_line), as_integer(o->m_pos)); Value rhs = stack.back(); stack.pop_back(); Value lhs = stack.back(); stack.pop_back(); stack.emplace_back(boxed(o->call(lhs, rhs, arg_option, std::nullopt))); } else stack.push_back(node); }
    if (stack.empty()) throw_semantics("Invalid semantics.", 0, 0); if (stack.size() != 1) throw_semantics("Invalid semantics.", 0, 0); return eval_value(stack.back()); // ネイティブ版の意図的な検証として、余分なオペランドが残る逆ポーランド記法を拒否する。 Python版は最後の値を返すが、通常の解析済み式は一つだけ値を残す。
}
// 一時的な評価結果には指定された初期値がないため、ネイテ
// ィブ版はその項目にNone、Python版は空文字列を
// 使う。 この表現上の違いは、評価結果の値を変えない。
std::shared_ptr<::cw::data::Variant> eval_expr(std::vector<Value> arg_st, std::shared_ptr<CalcOption> arg_option) { auto result = std::make_shared<::cw::data::Variant>(); result->initialize(std::nullopt, std::nullopt, to_variantvalue(calculate(std::move(arg_st), std::move(arg_option))), "", Value()); return result; }
Value to_variantvalue(std::shared_ptr<ValueType> arg_val) {
    // ネイティブ版の未対応型の境界として、一覧・構造・Decimal・真
    // 偽値・文字列以外のValueTypeはNoneを返す。 Python
    // 版はassertで失敗する。対応する値は通常の変換を維持する。
    if (auto l = std::dynamic_pointer_cast<ListValue>(arg_val)) {
        auto h = std::make_shared<::cw::data::VariantSequence>();
        const auto count = list_values(l->m_value).size();
        for (std::size_t i = 0; i < count; ++i) h->m_values.push_back(to_variantvalue(l->eval(static_cast<Integer>(i))));
        return boxed(h);
    }
    if (auto s = std::dynamic_pointer_cast<StructureValue>(arg_val)) {
        auto out = std::make_shared<::cw::data::StructVal>();
        std::vector<Value> vals;
        const auto count = structure_values(*s).size();
        for (std::size_t i = 0; i < count; ++i) vals.push_back(to_variantvalue(s->eval(static_cast<Integer>(i))));
        out->initialize(structure_name(*s), std::move(vals));
        return boxed(out);
    }
    if (auto d = std::dynamic_pointer_cast<DecimalValue>(arg_val)) return decimal_to_compat(d->m_decimal); if (auto b = std::dynamic_pointer_cast<BooleanValue>(arg_val)) return b->m_value; if (auto str = std::dynamic_pointer_cast<StringValue>(arg_val)) return str->m_value; return {};
}

// 公開のネイティブ評価はoptionなしを許し、その場合はこの検査を省く。 Python版は option.is_differentscenario
// を直接読む。通常の呼び出し元は指定する。
void _chk_diffsc(std::shared_ptr<CalcOption> arg_option, Integer arg_line, Integer arg_pos) { if (arg_option && *std::get_if<bool>(&arg_option->m_is_differentscenario.storage())) { DifferentScenarioException e; e.initialize("Read a variable at different scenario.", arg_line, arg_pos); throw e; } }
void _chk_argscount(std::vector<::cw::lib::Callback> arg_args, Integer arg_n, String arg_func_name, Integer arg_line, Integer arg_pos) { if (static_cast<Integer>(arg_args.size()) != arg_n) { ArgumentsCountException e; e.initialize("Invalid arguments count: " + std::to_string(arg_n) + " != " + std::to_string(arg_args.size()), std::move(arg_func_name), arg_line, arg_pos); throw e; } }
void _chk_argscount2(std::vector<::cw::lib::Callback> arg_args, Integer arg_n1, Integer arg_n2, String arg_func_name, Integer arg_line, Integer arg_pos) { if (arg_n1 == arg_n2) return _chk_argscount(std::move(arg_args), arg_n1, std::move(arg_func_name), arg_line, arg_pos); if (static_cast<Integer>(arg_args.size()) != arg_n1 && static_cast<Integer>(arg_args.size()) != arg_n2) { ArgumentsCountException e; e.initialize("Invalid arguments count: " + std::to_string(arg_n1) + "-" + std::to_string(arg_n2) + " != " + std::to_string(arg_args.size()), std::move(arg_func_name), arg_line, arg_pos); throw e; } }
static Decimal check_decimal_indexed(std::shared_ptr<ValueType> arg_arg, String arg_func_name, const Value& arg_arg_index) {
    if (auto d = std::dynamic_pointer_cast<DecimalValue>(arg_arg)) return d->m_decimal;
    ArgumentIsNotDecimalException e;
    e.initialize(arg_arg->to_str() + " is not Decimal.", std::move(arg_func_name), 0, arg_arg->to_str(), line_of(arg_arg), pos_of(arg_arg));
    e.m_arg_index = arg_arg_index; throw e;
}
static Decimal check_minvalue_indexed(std::shared_ptr<ValueType> arg_arg, String arg_func_name, const Value& arg_arg_index, Value arg_minvalue) {
    const Decimal result = check_decimal_indexed(arg_arg, arg_func_name, arg_arg_index);
    const Decimal minimum = decimal_from_value(arg_minvalue);
    if (result.compare(minimum) < 0) {
        InvalidArgumentException e;
        e.initialize(python_decimal_text(result) + " < " + python_decimal_text(minimum) + ".", std::move(arg_func_name), 0, arg_arg->to_str(), line_of(arg_arg), pos_of(arg_arg));
        e.m_arg_index = arg_arg_index; throw e;
    }
    return result;
}
static String check_string_indexed(std::shared_ptr<ValueType> arg_arg, String arg_func_name, const Value& arg_arg_index) { if (auto s = std::dynamic_pointer_cast<StringValue>(arg_arg)) return as_string(s->m_value); ArgumentIsNotStringException e; e.initialize(arg_arg->to_str() + " is not String.", std::move(arg_func_name), 0, arg_arg->to_str(), line_of(arg_arg), pos_of(arg_arg)); e.m_arg_index = arg_arg_index; throw e; }
static std::shared_ptr<ListValue> check_list_indexed(std::shared_ptr<ValueType> arg_arg, String arg_func_name, const Value& arg_arg_index) { if (auto l = std::dynamic_pointer_cast<ListValue>(arg_arg)) return l; ArgumentIsNotListException e; e.initialize(arg_arg->to_str() + " is not List.", std::move(arg_func_name), 0, arg_arg->to_str(), line_of(arg_arg), pos_of(arg_arg)); e.m_arg_index = arg_arg_index; throw e; }
// 公開のIntegerのABIを維持する。入れ子の呼び出し元もValueを渡して同じ
// 共通検査を使い、 遅延したeval/to_strの例外は自身の位置情報を保持する。
Decimal _chk_decimal(std::shared_ptr<ValueType> arg, String function, Integer index) { return check_decimal_indexed(std::move(arg), std::move(function), Value(index)); }
Decimal _chk_minvalue(std::shared_ptr<ValueType> arg, String function, Integer index, Value minimum) { return check_minvalue_indexed(std::move(arg), std::move(function), Value(index), std::move(minimum)); }
String _chk_string(std::shared_ptr<ValueType> arg, String function, Integer index) { return check_string_indexed(std::move(arg), std::move(function), Value(index)); }
bool _chk_boolean(std::shared_ptr<ValueType> arg_arg, String arg_func_name, Integer arg_arg_index) { if (auto b = std::dynamic_pointer_cast<BooleanValue>(arg_arg)) return *std::get_if<bool>(&b->m_value.storage()); ArgumentIsNotBooleanException e; e.initialize(arg_arg->to_str() + " is not Boolean.", std::move(arg_func_name), arg_arg_index, arg_arg->to_str(), line_of(arg_arg), pos_of(arg_arg)); throw e; }
std::shared_ptr<ListValue> _chk_list(std::shared_ptr<ValueType> arg, String function, Integer index) { return check_list_indexed(std::move(arg), std::move(function), Value(index)); }
std::shared_ptr<StructureValue> _chk_structure(std::shared_ptr<ValueType> arg_arg, String arg_func_name, Integer arg_arg_index, String arg_struct_name) { if (auto s = std::dynamic_pointer_cast<StructureValue>(arg_arg); s && structure_name(*s) == arg_struct_name) return s; ArgumentIsNotStructureException e; e.initialize(arg_arg->to_str() + " is not Structure.", std::move(arg_func_name), arg_arg_index, arg_arg->to_str(), std::move(arg_struct_name), line_of(arg_arg), pos_of(arg_arg)); throw e; }
bool _is_alldecimal(std::vector<std::shared_ptr<ValueType>> arg_args, String arg_func_name) { for (Integer i = 0; i < static_cast<Integer>(arg_args.size()); ++i) (void)_chk_decimal(arg_args[static_cast<std::size_t>(i)], arg_func_name, i); return true; }
std::vector<std::shared_ptr<ValueType>> _all_eval(std::vector<::cw::lib::Callback> arg_args) { std::vector<std::shared_ptr<ValueType>> values; for (auto& callback : arg_args) values.push_back(value_type(callback({}, {}))); return values; }
#if 0 // 第2段階の実装で置換済み。旧生成スタブは履歴として保持。
std::shared_ptr<::cw::calculator::DecimalValue> _func_max(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

::cw::lib::Value _func_max_val_int(std::shared_ptr<::cw::calculator::ValueType> arg_a)
{
    (void)arg_a;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_min(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

::cw::lib::Value _func_min_val_int(std::shared_ptr<::cw::calculator::ValueType> arg_a)
{
    (void)arg_a;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_len(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_find(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_left(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_right(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_mid(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_str(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_is_str(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_is_number(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_is_list(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_value(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_int(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ValueType> _func_if(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_and_d(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_or_d(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

#endif

// 第2段階: 副作用のない組み込み関数（文字列、数値、リスト、遅延評価）。
std::shared_ptr<DecimalValue> _func_max(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    auto values = _all_eval(std::move(args)); if (values.empty() || !_is_alldecimal(values, "MAX")) { ArgumentsCountException e; e.initialize("No argments of max.", "MAX", line, pos); throw e; }
    Decimal result = std::dynamic_pointer_cast<DecimalValue>(values.front())->m_decimal; for (const auto& v : values) { const Decimal candidate = std::dynamic_pointer_cast<DecimalValue>(v)->m_decimal; if (candidate.compare(result) > 0) result = candidate; } return decimal_result(result, line, pos);
}
std::shared_ptr<DecimalValue> _func_min(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    auto values = _all_eval(std::move(args)); if (values.empty() || !_is_alldecimal(values, "MIN")) { ArgumentsCountException e; e.initialize("No argments of min.", "MIN", line, pos); throw e; }
    Decimal result = std::dynamic_pointer_cast<DecimalValue>(values.front())->m_decimal; for (const auto& v : values) { const Decimal candidate = std::dynamic_pointer_cast<DecimalValue>(v)->m_decimal; if (candidate.compare(result) < 0) result = candidate; } return decimal_result(result, line, pos);
}
std::shared_ptr<DecimalValue> _func_len(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "LEN", line, pos); auto v = _all_eval(std::move(args)); return decimal_result(utf8_cp_len(_chk_string(v[0], "LEN", 0)), line, pos); }
std::shared_ptr<DecimalValue> _func_find(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount2(args, 2, 3, "FIND", line, pos); auto v = _all_eval(std::move(args));
    const String needle = _chk_string(v[0], "FIND", 0), haystack = _chk_string(v[1], "FIND", 1);
    Integer start = 0;
    const Integer hay_len = utf8_cp_len(haystack);
    if (v.size() == 3) {
        start = bounded_count(_chk_minvalue(v[2], "FIND", 2, Value(0)), hay_len + 1);
        if (start == 0) return decimal_result(0, line, pos);
        --start;
        if (start >= hay_len) return decimal_result(0, line, pos);
    }
    if (needle.empty() && haystack.empty()) return decimal_result(0, line, pos);
    const auto found = haystack.find(needle, utf8_cp_byte_index(haystack, start));
    return decimal_result(found == String::npos ? 0 : static_cast<Real>(utf8_byte_to_cp(haystack, found) + 1), line, pos);
}
std::shared_ptr<StringValue> _func_left(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "LEFT", line, pos); auto v = _all_eval(std::move(args));
    const String s = _chk_string(v[0], "LEFT", 0); const Integer n = bounded_count(_chk_minvalue(v[1], "LEFT", 1, Value(0)), utf8_cp_len(s));
    const Integer take = std::min(n, utf8_cp_len(s));
    return string_result(utf8_cp_slice(s, 0, take), line, pos);
}
std::shared_ptr<StringValue> _func_right(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "RIGHT", line, pos); auto v = _all_eval(std::move(args));
    const String s = _chk_string(v[0], "RIGHT", 0); const Integer n = bounded_count(_chk_minvalue(v[1], "RIGHT", 1, Value(0)), utf8_cp_len(s));
    const Integer len = utf8_cp_len(s); const Integer take = std::min(n, len);
    return string_result(utf8_cp_slice(s, len - take, take), line, pos);
}
std::shared_ptr<StringValue> _func_mid(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount2(args, 2, 3, "MID", line, pos); auto v = _all_eval(std::move(args));
    const String s = _chk_string(v[0], "MID", 0); const Integer start = bounded_count(_chk_minvalue(v[1], "MID", 1, Value(1)), utf8_cp_len(s) + 1);
    const Integer len = utf8_cp_len(s);
    if (start > len) return string_result({}, line, pos);
    Integer count = len - (start - 1);
    if (v.size() == 3) count = bounded_count(_chk_minvalue(v[2], "MID", 2, Value(0)), count);
    return string_result(utf8_cp_slice(s, start - 1, count), line, pos);
}
std::shared_ptr<StringValue> _func_str(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "STR", line, pos); auto v = _all_eval(std::move(args)); if (std::dynamic_pointer_cast<ListValue>(v[0]) || std::dynamic_pointer_cast<StructureValue>(v[0])) { InvalidArgumentException e; e.initialize("Invalid argument: " + v[0]->to_str(), "STR", 0, v[0]->to_str(), line_of(v[0]), pos_of(v[0])); throw e; } return string_result(v[0]->to_str(), line, pos); }
std::shared_ptr<BooleanValue> _func_is_str(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "IS_STR", line, pos); return boolean_result(static_cast<bool>(std::dynamic_pointer_cast<StringValue>(_all_eval(std::move(args))[0])), line, pos); }
std::shared_ptr<BooleanValue> _func_is_number(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "IS_NUMBER", line, pos); return boolean_result(static_cast<bool>(std::dynamic_pointer_cast<DecimalValue>(_all_eval(std::move(args))[0])), line, pos); }
std::shared_ptr<BooleanValue> _func_is_list(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "IS_LIST", line, pos); return boolean_result(static_cast<bool>(std::dynamic_pointer_cast<ListValue>(_all_eval(std::move(args))[0])), line, pos); }
std::shared_ptr<DecimalValue> _func_value(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 1, "VALUE", line, pos); auto v = _all_eval(std::move(args)); Decimal value;
    try {
        if (auto d = std::dynamic_pointer_cast<DecimalValue>(v[0])) value = d->m_decimal;
        else if (auto s = std::dynamic_pointer_cast<StringValue>(v[0])) {
            // decimal.Decimal と同様、前後にはU+3000も含む str.isspace()
            // の全空白を許す。 std::regexの \s と Decimal::parse
            // はASCIIの一部しか認識しないため、別に扱う。
            const String text = utf8_trim_copy(as_string(s->m_value));
            static const std::regex number(R"(-?([0-9]+(\.[0-9]*)?|([0-9]*\.)?[0-9]+))");
            if (!std::regex_match(text, number)) throw ::cw::lib::DecimalInvalidOperation("not a decimal literal");
            value = Decimal::parse(text);
        } else throw ::cw::lib::DecimalInvalidOperation("not a Decimal or String");
    } catch (const ::cw::lib::DecimalError&) {
        InvalidArgumentException e; e.initialize("Invalid argument: " + v[0]->to_str(), "VALUE", 0, v[0]->to_str(), line_of(v[0]), pos_of(v[0])); throw e;
    }
    return decimal_result(value, line, pos);
}
std::shared_ptr<DecimalValue> _func_int(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 1, "INT", line, pos); auto v = _all_eval(std::move(args)); Decimal value;
    try {
        if (auto d = std::dynamic_pointer_cast<DecimalValue>(v[0])) value = d->m_decimal;
        else if (auto s = std::dynamic_pointer_cast<StringValue>(v[0]))
            value = Decimal::parse(utf8_trim_copy(as_string(s->m_value)));
        else throw ::cw::lib::DecimalInvalidOperation("not a Decimal or String");
    } catch (const ::cw::lib::DecimalError&) {
        InvalidArgumentException e; e.initialize("Invalid argument: " + v[0]->to_str(), "INT", 0, v[0]->to_str(), line_of(v[0]), pos_of(v[0])); throw e;
    }
    return decimal_result(value.trunc(), line, pos);
}
std::shared_ptr<ValueType> _func_if(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 3, "IF", line, pos); auto condition = value_type(args[0]({}, {})); if (!_chk_boolean(condition, "IF", 0)) return value_type(args[2]({}, {})); return value_type(args[1]({}, {})); }
std::shared_ptr<BooleanValue> _func_and_d(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { if (args.empty()) { ArgumentsCountException e; e.initialize("Invalid arguments count: >=1 != 0", "AND_DFS", line, pos); throw e; } for (Integer i = 0; i < static_cast<Integer>(args.size()); ++i) if (!_chk_boolean(value_type(args[static_cast<std::size_t>(i)]({}, {})), "AND_DFS", i)) return boolean_result(false, line, pos); return boolean_result(true, line, pos); }
std::shared_ptr<BooleanValue> _func_or_d(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { if (args.empty()) { ArgumentsCountException e; e.initialize("Invalid arguments count: >=1 != 0", "OR_DFS", line, pos); throw e; } for (Integer i = 0; i < static_cast<Integer>(args.size()); ++i) if (_chk_boolean(value_type(args[static_cast<std::size_t>(i)]({}, {})), "OR_DFS", i)) return boolean_result(true, line, pos); return boolean_result(false, line, pos); }

std::shared_ptr<::cw::calculator::ValueType> _func_var(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    _chk_argscount(arg_args, 1, "VAR", arg_line, arg_pos); auto values = _all_eval(std::move(arg_args)); const String path = _chk_string(values[0], "VAR", 0); Value value; bool found = false;
    if (auto event = current_event()) { auto it = event->m_variants.find(path); if (it != event->m_variants.end() && it->second) { value = it->second->m_value; found = true; } }
    if (!found) if (auto data = current_sdata()) { auto it = data->m_variants.find(path); if (it != data->m_variants.end() && it->second) { _chk_diffsc(arg_option, arg_line, arg_pos); value = it->second->m_value; found = true; } }
    // Python版と同様、文言の %s はそのまま残し、存在しないパスは別の例外フィールドに格納する。
    if (!found) { VariantNotFoundException e; e.initialize("Variant \"%s\" is not found.", path, line_of(values[0]), pos_of(values[0])); throw e; }
    return _variantvalue_to_valuetype(value, arg_line, arg_pos);
}

std::shared_ptr<::cw::calculator::ValueType> _variantvalue_to_valuetype(::cw::lib::Value arg_val, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    if (const auto* p = std::get_if<String>(&arg_val.storage())) { auto r = std::make_shared<StringValue>(); r->initialize(*p, arg_line, arg_pos); return r; }
    if (const auto* p = std::get_if<bool>(&arg_val.storage())) { auto r = std::make_shared<BooleanValue>(); r->initialize(*p, arg_line, arg_pos); return r; }
    if (std::holds_alternative<Integer>(arg_val.storage()) || std::holds_alternative<Real>(arg_val.storage()) || std::holds_alternative<Decimal>(arg_val.storage())) { auto r = std::make_shared<DecimalValue>(); r->initialize(arg_val, arg_line, arg_pos); return r; }
    if (const auto* p = std::get_if<::cw::lib::ObjectPtr>(&arg_val.storage())) {
        if (auto seq = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*p)) {
            // Python版と同様、コモンの一覧を再帰的に変換する。 浅い変換では
            // 内側の座標一覧が生のVariantSequenceとして残り、 ListValue::eval
            // で評価できず0:0の構文エラーになる。
            std::vector<Value> values;
            values.reserve(seq->m_values.size());
            for (const auto& value : seq->m_values)
                values.push_back(boxed(_variantvalue_to_valuetype(value, arg_line, arg_pos)));
            auto r = std::make_shared<ListValue>(); r->initialize(std::move(values), arg_line, arg_pos); return r;
        }
        if (auto st = std::dynamic_pointer_cast<::cw::data::StructVal>(*p)) {
            std::vector<Value> members;
            members.reserve(st->m_member_values.size());
            for (const auto& value : st->m_member_values)
                members.push_back(boxed(_variantvalue_to_valuetype(value, arg_line, arg_pos)));
            auto r = std::make_shared<StructureValue>(); r->initialize(as_string(st->m_name), std::move(members), arg_line, arg_pos); return r;
        }
    }
    throw_semantics("Invalid variant value.", arg_line, arg_pos);
}

#if 0 // 第2段階: 状態依存組み込みの生成プレースホルダは実装ブロックへ移行。
std::shared_ptr<::cw::calculator::BooleanValue> _func_flagvalue(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_flagtext(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_stepvalue(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_steptext(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_stepmax(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_dice(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

::cw::lib::Integer _ccard_index(std::shared_ptr<::cw::character::Character> arg_ccard)
{
    (void)arg_ccard;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_selected(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_latestusecardowner(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::optional<std::shared_ptr<::cw::character::Character>> _ccard_from(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name)
{
    (void)arg_arg;
    (void)arg_func_name;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_charexists(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_casttype(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_castname(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_findcoupon(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

::cw::lib::Value _func_findcoupon_lambda_line_1650(::cw::lib::Value arg_name)
{
    (void)arg_name;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_coupontext(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_findgossip(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

::cw::lib::Value _func_findgossip_lambda_line_1681(::cw::lib::Value arg_name)
{
    (void)arg_name;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_gossiptext(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_partyname(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _func_list(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _func_list_e(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ValueType> _func_at(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_llen(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_lfind(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _func_lleft(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _func_lright(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _func_lmid(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_partymoney(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_partynumber(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_yadoname(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_skintype(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_battleround(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_castlevel(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_couponvalue(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_liferatio(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_statusvalue(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_statusround(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StructureValue> _func_selectedcard(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::optional<std::shared_ptr<::cw::header::CardHeader>> _header_from(std::shared_ptr<::cw::calculator::ValueType> arg_arg, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_index)
{
    (void)arg_arg;
    (void)arg_func_name;
    (void)arg_arg_index;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_cardname(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_cardtype(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_cardrarity(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_cardprice(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_cardlevel(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_cardcount(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_findkeycode(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

::cw::lib::Value _func_findkeycode_lambda_line_2169(::cw::lib::Value arg_name)
{
    (void)arg_name;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _func_find_keycodes_with(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

void _func_find_keycodes_with_push_keycode(std::optional<::cw::lib::String> arg_kc)
{
    (void)arg_kc;
}

std::vector<::cw::lib::String> _func_find_keycodes_with_get_header_keycodes(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header)
{
    (void)arg_header;
    return {};
}

void _func_find_keycodes_with_add_from_card_header(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header)
{
    (void)arg_header;
}

void _func_find_keycodes_with_add_from_selected_card()
{
}

void _func_find_keycodes_with_add_from_character(std::optional<std::shared_ptr<::cw::character::Character>> arg_ch)
{
    (void)arg_ch;
}

void _func_find_keycodes_with_add_from_party_as_backpack(std::optional<::cw::lib::Value> arg_party)
{
    (void)arg_party;
}

void _func_find_keycodes_with_add_from_target(std::optional<::cw::lib::Value> arg_t)
{
    (void)arg_t;
}
#endif

// イベント・シナリオの状態を動的に参照する。Python
// の属性に直接相当するものがない場合は既存の C++ モ
// デルへ接続し、未対応の状態を固定値で黙って代替しない。

std::shared_ptr<BooleanValue> _func_flagvalue(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption> option, Integer line, Integer pos) {
    _chk_argscount(args, 1, "FLAGVALUE", line, pos); auto values = _all_eval(std::move(args)); const String path = _chk_string(values[0], "FLAGVALUE", 0);
    std::shared_ptr<::cw::data::Flag> flag; if (auto event = current_event()) { auto it = event->m_flags.find(path); if (it != event->m_flags.end()) flag = it->second; }
    if (!flag) if (auto data = current_sdata()) { auto it = data->m_flags.find(path); if (it != data->m_flags.end()) { _chk_diffsc(option, line, pos); flag = it->second; } }
    if (!flag) { FlagNotFoundException e; e.initialize("Flag \"%s\" is not found.", path, line_of(values[0]), pos_of(values[0])); throw e; }
    return boolean_result(flag->m_value, line, pos);
}
std::shared_ptr<StringValue> _func_flagtext(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption> option, Integer line, Integer pos) {
    _chk_argscount2(args, 1, 2, "FLAGTEXT", line, pos); auto values = _all_eval(std::move(args)); const String path = _chk_string(values[0], "FLAGTEXT", 0);
    std::shared_ptr<::cw::data::Flag> flag; if (auto event = current_event()) { auto it = event->m_flags.find(path); if (it != event->m_flags.end()) flag = it->second; }
    if (!flag) if (auto data = current_sdata()) { auto it = data->m_flags.find(path); if (it != data->m_flags.end()) { _chk_diffsc(option, line, pos); flag = it->second; } }
    if (!flag) { FlagNotFoundException e; e.initialize("Flag \"%s\" is not found.", path, line_of(values[0]), pos_of(values[0])); throw e; }
    const bool value = values.size() == 2 ? _chk_boolean(values[1], "FLAGTEXT", 1) : flag->m_value;
    String text = flag->get_valuename(value);
    if (flag->m_spchars) text = std::get<0>(::cw::sprite::message::rpl_specialstr(text, std::nullopt, true, "All", true, true, std::nullopt));
    return string_result(std::move(text), line, pos);
}
std::shared_ptr<DecimalValue> _func_stepvalue(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption> option, Integer line, Integer pos) {
    _chk_argscount(args, 1, "STEPVALUE", line, pos); auto values = _all_eval(std::move(args)); const String path = _chk_string(values[0], "STEPVALUE", 0); std::shared_ptr<::cw::data::Step> step;
    if (auto event = current_event()) { auto it = event->m_steps.find(path); if (it != event->m_steps.end()) step = it->second; }
    if (!step) if (auto data = current_sdata()) { auto it = data->m_steps.find(path); if (it != data->m_steps.end()) { _chk_diffsc(option, line, pos); step = it->second; } }
    if (!step) { StepNotFoundException e; e.initialize("Step \"%s\" is not found.", path, line_of(values[0]), pos_of(values[0])); throw e; } return decimal_result(static_cast<Real>(step->m_value), line, pos);
}
std::shared_ptr<StringValue> _func_steptext(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption> option, Integer line, Integer pos) {
    _chk_argscount2(args, 1, 2, "STEPTEXT", line, pos); auto values = _all_eval(std::move(args)); const String path = _chk_string(values[0], "STEPTEXT", 0); std::shared_ptr<::cw::data::Step> step;
    if (auto event = current_event()) { auto it = event->m_steps.find(path); if (it != event->m_steps.end()) step = it->second; }
    if (!step) if (auto data = current_sdata()) { auto it = data->m_steps.find(path); if (it != data->m_steps.end()) { _chk_diffsc(option, line, pos); step = it->second; } }
    if (!step) { StepNotFoundException e; e.initialize("Step \"%s\" is not found.", path, line_of(values[0]), pos_of(values[0])); throw e; }
    const Integer value = values.size() == 2 ? as_integer(_chk_decimal(values[1], "STEPTEXT", 1)) : step->m_value; if (value < 0 || value >= static_cast<Integer>(step->m_valuenames.size())) { /* Python版は例外を作る前に args_r[1] を参照する。 未検査のベクター参照をせず、そのIndexErrorをCompute以外のネイティブの失敗へ対応させる。 */ if (values.size() < 2) throw std::out_of_range("STEPTEXT args_r[1]"); InvalidStepValueException e; e.initialize("Invalid step value: \"" + path + "\"[" + std::to_string(value) + "]", line_of(values[1]), pos_of(values[1])); throw e; }
    String text = step->m_valuenames[static_cast<std::size_t>(value)];
    if (step->m_spchars) text = std::get<0>(::cw::sprite::message::rpl_specialstr(text, std::nullopt, true, "All", true, true, std::nullopt));
    return string_result(std::move(text), line, pos);
}
std::shared_ptr<DecimalValue> _func_stepmax(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption> option, Integer line, Integer pos) {
    _chk_argscount(args, 1, "STEPMAX", line, pos); auto values = _all_eval(std::move(args)); const String path = _chk_string(values[0], "STEPMAX", 0); std::shared_ptr<::cw::data::Step> step; if (auto event = current_event()) { auto it = event->m_steps.find(path); if (it != event->m_steps.end()) step = it->second; } if (!step) if (auto data = current_sdata()) { auto it = data->m_steps.find(path); if (it != data->m_steps.end()) { _chk_diffsc(option, line, pos); step = it->second; } } if (!step) { StepNotFoundException e; e.initialize("Step \"%s\" is not found.", path, line_of(values[0]), pos_of(values[0])); throw e; } return decimal_result(static_cast<Integer>(step->m_valuenames.size()) - Integer{1}, line, pos);
}
std::shared_ptr<DecimalValue> _func_dice(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 2, "DICE", line, pos); auto values = _all_eval(std::move(args)); const Integer times = as_integer(_chk_minvalue(values[0], "DICE", 0, Value(0))), sided = as_integer(_chk_minvalue(values[1], "DICE", 0, Value(0))); if (!times || !sided) return decimal_result(0, line, pos); if (!::cw::g_cwpy || !::cw::g_cwpy->m_dice) { throw_semantics("DICE requires an initialized game runtime.", line, pos); } return decimal_result(::cw::g_cwpy->m_dice->roll(times, sided), line, pos); }

Integer _ccard_index(std::shared_ptr<::cw::character::Character> card) {
    // ネイティブの組み込み側は実行環境の生成前にこのAPIを呼べる。 Python版
    // には常に cw.cwpy があるため、ネイティブの寿命管理の保護だけを残す。
    if (!::cw::g_cwpy) return 0;
    const auto find_index = [&](const auto& cards) -> Integer {
        for (std::size_t i = 0; i < cards.size(); ++i)
            if (std::dynamic_pointer_cast<::cw::character::Character>(cards[i]) == card)
                return static_cast<Integer>(i) + 1;
        // Pythonの list.index と同様、未存在ではValueErrorに相当す
        // る失敗を返す。 復旧可能かは呼び出し元が決め、SELECTEDは記録してゼロを返す。
        throw std::out_of_range("Character card is not in its runtime list.");
    };
    if (std::dynamic_pointer_cast<::cw::sprite::card::PlayerCard>(card))
        return find_index(::cw::g_cwpy->get_pcards(Value()));
    if (std::dynamic_pointer_cast<::cw::sprite::card::EnemyCard>(card)) {
        const auto players = ::cw::g_cwpy->get_pcards(Value());
        return static_cast<Integer>(players.size()) + find_index(::cw::g_cwpy->get_ecards(Value()));
    }
    if (std::dynamic_pointer_cast<::cw::sprite::card::FriendCard>(card)) {
        const auto players = ::cw::g_cwpy->get_pcards(Value());
        const auto enemies = ::cw::g_cwpy->get_ecards(Value());
        return static_cast<Integer>(players.size() + enemies.size()) + find_index(::cw::g_cwpy->get_fcards(Value()));
    }
    throw std::logic_error("Expected a PlayerCard, EnemyCard or FriendCard.");
}
std::shared_ptr<DecimalValue> _func_selected(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 0, "SELECTED", line, pos);
    // ネイティブの呼び出し元は、実行環境・イベントの初期化前にも動く場合がある。
    // Python版は計算関数の評価前に、プロセス全体の実行環境が存在する。
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_event) return decimal_result(0, line, pos);
    if (!::cw::g_cwpy->m_event->has_selectedmember()) return decimal_result(0, line, pos);
    try {
        const auto selected = ::cw::g_cwpy->m_event->get_selectedmember();
        if (!selected || !*selected) throw std::logic_error("Selected member is absent.");
        return decimal_result(_ccard_index(*selected), line, pos);
    } catch (const std::out_of_range&) {
        ::cw::util::print_ex(std::nullopt);
        return decimal_result(0, line, pos);
    }
}
std::shared_ptr<DecimalValue> _func_latestusecardowner(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 0, "LATESTUSECARDOWNER", line, pos);
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_event) return decimal_result(0, line, pos);
    const auto events = ::cw::g_cwpy->m_event->get_events();
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        auto event = *it;
        std::shared_ptr<::cw::character::Character> owner;
        bool matched = false;
        if (auto card = std::dynamic_pointer_cast<::cw::event::CardEvent>(event)) {
            owner = as_character(card->m_user);
            matched = true;
        } else if (event->m_inusecard) {
            owner = as_character((*event->m_inusecard)->get_owner().value_or(Value()));
            matched = true;
        }
        if (!matched) continue;
        // Python版と同様、所有者がなくても最新のcard/inusecardイベントで必ず止まる。
        if (owner) {
            try { return decimal_result(_ccard_index(owner), line, pos); }
            catch (const std::out_of_range&) { ::cw::util::print_ex(std::nullopt); }
        }
        return decimal_result(0, line, pos);
    }
    return decimal_result(0, line, pos);
}
std::optional<std::shared_ptr<::cw::character::Character>> _ccard_from(std::shared_ptr<ValueType> arg, String func_name) { const Integer n = as_integer(_chk_minvalue(arg, std::move(func_name), 0, Value(0))); if (n <= 0 || !::cw::g_cwpy) return std::nullopt; Integer index = n; for (const auto& item : ::cw::g_cwpy->get_pcards(Value())) { if (--index == 0) return std::dynamic_pointer_cast<::cw::character::Character>(item); } for (const auto& item : ::cw::g_cwpy->get_ecards(Value())) { if (--index == 0) return std::dynamic_pointer_cast<::cw::character::Character>(item); } for (const auto& item : ::cw::g_cwpy->get_fcards(Value())) { if (--index == 0) return std::dynamic_pointer_cast<::cw::character::Character>(item); } return std::nullopt; }
std::shared_ptr<BooleanValue> _func_charexists(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "CHAREXISTS", line, pos); auto values = _all_eval(std::move(args)); return boolean_result(_ccard_from(values[0], "CHAREXISTS").has_value(), line, pos); }
std::shared_ptr<DecimalValue> _func_casttype(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "CASTTYPE", line, pos); auto values = _all_eval(std::move(args)); const auto card = _ccard_from(values[0], "CASTTYPE"); if (!card || !*card) return decimal_result(0, line, pos); if (std::dynamic_pointer_cast<::cw::character::Player>(*card)) return decimal_result(1, line, pos); if (std::dynamic_pointer_cast<::cw::character::Enemy>(*card)) return decimal_result(2, line, pos); if (std::dynamic_pointer_cast<::cw::character::Friend>(*card)) return decimal_result(3, line, pos); return decimal_result(0, line, pos); }
std::shared_ptr<StringValue> _func_castname(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "CASTNAME", line, pos); auto values = _all_eval(std::move(args)); const auto card = _ccard_from(values[0], "CASTNAME"); return string_result(card && *card ? (*card)->get_showingname() : String{}, line, pos); }
std::shared_ptr<DecimalValue> _func_findcoupon(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount2(args, 2, 3, "FINDCOUPON", line, pos); auto values = _all_eval(std::move(args)); const auto card = _ccard_from(values[0], "FINDCOUPON"); const String pattern = _chk_string(values[1], "FINDCOUPON", 1); Integer start = values.size() == 3 ? as_integer(_chk_minvalue(values[2], "FINDCOUPON", 0, Value(0))) : 1; if (!card || !*card || start <= 0) return decimal_result(0, line, pos); for (Integer i = start - 1; i < (*card)->coupons_len(); ++i) if (glob_match(pattern, std::get<0>((*card)->get_coupon_at(i)))) return decimal_result(static_cast<Real>(i + 1), line, pos); return decimal_result(0, line, pos); }
std::shared_ptr<StringValue> _func_coupontext(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 2, "COUPONTEXT", line, pos); auto values = _all_eval(std::move(args)); const auto card = _ccard_from(values[0], "COUPONTEXT"); const Integer index = as_integer(_chk_minvalue(values[1], "COUPONTEXT", 0, Value(0))) - 1; return string_result(card && *card && index >= 0 && index < (*card)->coupons_len() ? std::get<0>((*card)->get_coupon_at(index)) : String{}, line, pos); }
std::shared_ptr<DecimalValue> _func_findgossip(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount2(args, 1, 2, "FINDGOSSIP", line, pos); auto values = _all_eval(std::move(args)); const String pattern = _chk_string(values[0], "FINDGOSSIP", 0);
    const Integer start = values.size() == 2 ? as_integer(_chk_minvalue(values[1], "FINDGOSSIP", 0, Value(0))) : 1; const Integer start_index = start - 1;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || start_index < 0 || (*::cw::g_cwpy->m_ydata)->gossips_len() <= start_index) return decimal_result(0, line, pos);
    const Integer found = (*::cw::g_cwpy->m_ydata)->find_gossip([pattern](const ::cw::lib::Arguments& args, const ::cw::lib::KeywordArguments&) { return Value(!args.empty() && glob_match(pattern, as_string(args.front()))); }, start_index);
    return decimal_result(found < 0 ? 0 : static_cast<Real>(found + 1), line, pos);
}
std::shared_ptr<StringValue> _func_gossiptext(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 1, "GOSSIPTEXT", line, pos); auto values = _all_eval(std::move(args)); const Integer index = as_integer(_chk_minvalue(values[0], "GOSSIPTEXT", 0, Value(0))) - 1;
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || index < 0 || (*::cw::g_cwpy->m_ydata)->gossips_len() <= index) return string_result({}, line, pos);
    return string_result((*::cw::g_cwpy->m_ydata)->get_gossip_at(index), line, pos);
}
std::shared_ptr<StringValue> _func_partyname(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 0, "PARTYNAME", line, pos); if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !(*::cw::g_cwpy->m_ydata)->m_party) return string_result({}, line, pos); return string_result((*::cw::g_cwpy->m_ydata)->m_party.value()->get_showingname(), line, pos); }
std::shared_ptr<ListValue> _func_list(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { std::vector<Value> values; values.reserve(args.size()); for (auto& callback : args) values.push_back(callback_value(callback)); return list_result(std::move(values), line, pos); }
std::shared_ptr<ListValue> _func_list_e(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { std::vector<Value> values; for (auto& value : _all_eval(std::move(args))) values.push_back(boxed(value)); return list_result(std::move(values), line, pos); }
std::shared_ptr<ValueType> _func_at(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 2, "AT", line, pos); auto values = _all_eval(std::move(args)); auto list = _chk_list(values[0], "AT", 0); const Integer index = bounded_count(_chk_minvalue(values[1], "AT", 1, Value(1)), static_cast<Integer>(list_values(list->m_value).size()) + 1) - 1; if (index < 0 || index >= static_cast<Integer>(list_values(list->m_value).size())) { ListIndexOutOfRangeException e; e.initialize("List index is out of range.", "AT", 1, values[1]->to_str(), index + 1, static_cast<Integer>(list_values(list->m_value).size()), line_of(values[1]), pos_of(values[1])); throw e; } return list->eval(index); }
std::shared_ptr<DecimalValue> _func_llen(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "LLEN", line, pos); auto values = _all_eval(std::move(args)); return decimal_result(static_cast<Integer>(list_values(_chk_list(values[0], "LLEN", 0)->m_value).size()), line, pos); }
std::shared_ptr<DecimalValue> _func_lfind(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount2(args, 2, 3, "LFIND", line, pos); auto values = _all_eval(std::move(args)); auto list = _chk_list(values[1], "LFIND", 1); const auto items = list_values(list->m_value); Integer start = values.size() == 3 ? bounded_count(_chk_minvalue(values[2], "LFIND", 2, Value(0)), static_cast<Integer>(items.size()) + 1) : 1; if (start <= 0 || start > static_cast<Integer>(items.size())) return decimal_result(0, line, pos); for (Integer i = start - 1; i < static_cast<Integer>(items.size()); ++i) if (Operator::equals(values[0], list->eval(i), "=", true)) return decimal_result(static_cast<Real>(i + 1), line, pos); return decimal_result(0, line, pos); }
std::shared_ptr<ListValue> _func_lleft(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 2, "LLEFT", line, pos); auto values = _all_eval(std::move(args)); const auto items = list_values(_chk_list(values[0], "LLEFT", 0)->m_value); const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(bounded_count(_chk_minvalue(values[1], "LLEFT", 1, Value(0)), static_cast<Integer>(items.size()))), items.size()); return list_result({items.begin(), items.begin() + static_cast<std::ptrdiff_t>(n)}, line, pos); }
std::shared_ptr<ListValue> _func_lright(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 2, "LRIGHT", line, pos); auto values = _all_eval(std::move(args)); const auto items = list_values(_chk_list(values[0], "LRIGHT", 0)->m_value); const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(bounded_count(_chk_minvalue(values[1], "LRIGHT", 1, Value(0)), static_cast<Integer>(items.size()))), items.size()); return list_result({items.begin() + static_cast<std::ptrdiff_t>(items.size() - n), items.end()}, line, pos); }
std::shared_ptr<ListValue> _func_lmid(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount2(args, 2, 3, "LMID", line, pos); auto values = _all_eval(std::move(args)); const auto items = list_values(_chk_list(values[0], "LMID", 0)->m_value); const Integer start = bounded_count(_chk_minvalue(values[1], "LMID", 1, Value(1)), static_cast<Integer>(items.size()) + 1); if (start > static_cast<Integer>(items.size())) return list_result({}, line, pos); const std::size_t begin = static_cast<std::size_t>(start - 1); const std::size_t count = values.size() == 3 ? std::min<std::size_t>(static_cast<std::size_t>(bounded_count(_chk_minvalue(values[2], "LMID", 2, Value(0)), static_cast<Integer>(items.size() - begin))), items.size() - begin) : items.size() - begin; return list_result({items.begin() + static_cast<std::ptrdiff_t>(begin), items.begin() + static_cast<std::ptrdiff_t>(begin + count)}, line, pos); }
std::shared_ptr<DecimalValue> _func_partymoney(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 0, "PARTYMONEY", line, pos); if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !(*::cw::g_cwpy->m_ydata)->m_party) return decimal_result(-1, line, pos); return decimal_result(static_cast<Real>((*::cw::g_cwpy->m_ydata)->m_party.value()->m_money), line, pos); }
std::shared_ptr<DecimalValue> _func_partynumber(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 0, "PARTYNUMBER", line, pos); if (!::cw::g_cwpy || !::cw::g_cwpy->m_ydata || !(*::cw::g_cwpy->m_ydata)->m_party) return decimal_result(0, line, pos); return decimal_result(static_cast<Real>(::cw::g_cwpy->get_pcards(Value()).size()), line, pos); }
std::shared_ptr<StringValue> _func_yadoname(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 0, "YADONAME", line, pos); return string_result(::cw::g_cwpy && ::cw::g_cwpy->m_ydata ? (*::cw::g_cwpy->m_ydata)->get_showingname() : String{}, line, pos); }
// ネイティブの実行環境・設定ができるまでは、空のスキン名を未設定値として使う。 Python
// 版は初期化済みのデスクトップ環境で cw.cwpy.setting を直接読む。
std::shared_ptr<StringValue> _func_skintype(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 0, "SKINTYPE", line, pos); return string_result(::cw::g_cwpy && ::cw::g_cwpy->m_setting ? ::cw::g_cwpy->m_setting->m_skintype : String{}, line, pos); }
// ネイティブの戦闘情報がなければ、非戦闘の値-1を維持する。 Python版は is_battlestatus()
// がtrueなら戦闘オブジェクトの存在をassertで要求する。
std::shared_ptr<DecimalValue> _func_battleround(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 0, "BATTLEROUND", line, pos); return decimal_result(::cw::g_cwpy && ::cw::g_cwpy->is_battlestatus() && ::cw::g_cwpy->m_battle ? static_cast<Real>((*::cw::g_cwpy->m_battle)->m_round) : -1, line, pos); }
std::shared_ptr<DecimalValue> _func_castlevel(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "CASTLEVEL", line, pos); auto values = _all_eval(std::move(args)); const auto card = _ccard_from(values[0], "CASTLEVEL"); return decimal_result(card && *card ? static_cast<Real>((*card)->m_level) : 0, line, pos); }
std::shared_ptr<DecimalValue> _func_couponvalue(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 2, "COUPONVALUE", line, pos); auto values = _all_eval(std::move(args)); const auto card = _ccard_from(values[0], "COUPONVALUE"); const auto coupon = _chk_string(values[1], "COUPONVALUE", 1); if (!card || !*card) return decimal_result(0, line, pos); const auto n = (*card)->get_couponvalue(coupon, false); return decimal_result(n ? static_cast<Real>(*n) : 0, line, pos); }
// 最大生命力がゼロで使えない場合は、ゼロ除算せず-1を返す。
// Python版はCharacterが存在すれば直接割る。
std::shared_ptr<DecimalValue> _func_liferatio(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "LIFERATIO", line, pos); auto values = _all_eval(std::move(args)); const auto card = _ccard_from(values[0], "LIFERATIO"); return decimal_result(card && *card && (*card)->m_maxlife != 0 ? static_cast<Real>((*card)->m_life) / (*card)->m_maxlife : -1, line, pos); }
std::shared_ptr<DecimalValue> _func_statusvalue(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 2, "STATUSVALUE", line, pos); auto values = _all_eval(std::move(args)); const auto card = _ccard_from(values[0], "STATUSVALUE"); const Decimal kind = _chk_decimal(values[1], "STATUSVALUE", 1); if (kind != kind.trunc() || kind < Decimal(8) || kind > Decimal(22)) return decimal_result(0, line, pos); const Integer type = as_integer(kind); if (!card || !*card) return decimal_result(0, line, pos); auto& c = **card; Integer result = 0; switch (type) { case 8: result = c.m_poison; break; case 9: result = c.is_sleep() ? c.m_mentality_dur : 0; break; case 10: result = c.m_bind; break; case 11: result = c.m_paralyze; break; case 12: result = c.is_confuse() ? c.m_mentality_dur : 0; break; case 13: result = c.is_overheat() ? c.m_mentality_dur : 0; break; case 14: result = c.is_brave() ? c.m_mentality_dur : 0; break; case 15: result = c.is_panic() ? c.m_mentality_dur : 0; break; case 16: result = c.m_silence; break; case 17: result = c.m_faceup; break; case 18: result = c.m_antimagic; break; case 19: result = c.m_enhance_act; break; case 20: result = c.m_enhance_avo; break; case 21: result = c.m_enhance_res; break; case 22: result = c.m_enhance_def; break; default: break; } return decimal_result(static_cast<Real>(result), line, pos); }
std::shared_ptr<DecimalValue> _func_statusround(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption> option, Integer line, Integer pos) { (void)option; _chk_argscount(args, 2, "STATUSROUND", line, pos); auto values = _all_eval(std::move(args)); const auto card = _ccard_from(values[0], "STATUSROUND"); const Decimal kind = _chk_decimal(values[1], "STATUSROUND", 1); if (kind != kind.trunc() || kind < Decimal(8) || kind > Decimal(22)) return decimal_result(0, line, pos); const Integer type = as_integer(kind); if (!card || !*card) return decimal_result(0, line, pos); auto& c = **card; Integer result = 0; switch (type) { case 8: result = c.m_poison; break; case 9: result = c.is_sleep() ? c.m_mentality_dur : 0; break; case 10: result = c.m_bind; break; case 11: result = c.m_paralyze; break; case 12: result = c.is_confuse() ? c.m_mentality_dur : 0; break; case 13: result = c.is_overheat() ? c.m_mentality_dur : 0; break; case 14: result = c.is_brave() ? c.m_mentality_dur : 0; break; case 15: result = c.is_panic() ? c.m_mentality_dur : 0; break; case 16: result = c.m_silence; break; case 17: result = c.m_faceup; break; case 18: result = c.m_antimagic; break; case 19: result = c.m_enhance_act_dur; break; case 20: result = c.m_enhance_avo_dur; break; case 21: result = c.m_enhance_res_dur; break; case 22: result = c.m_enhance_def_dur; break; default: break; } return decimal_result(static_cast<Real>(result), line, pos); }

std::shared_ptr<StructureValue> cardinfo_result(Integer castindex, Integer cardindex, Integer actioncardid, Integer line, Integer pos) { auto r = std::make_shared<StructureValue>(); r->initialize("cardinfo", {boxed(decimal_result(castindex, line, pos)), boxed(decimal_result(cardindex, line, pos)), boxed(decimal_result(actioncardid, line, pos))}, line, pos); return r; }
std::shared_ptr<StructureValue> _func_selectedcard(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 0, "SELECTEDCARD", line, pos);
    const auto invalid = [&] { return cardinfo_result(0, 0, -2, line, pos); };
    std::shared_ptr<::cw::header::CardHeader> header;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_event) {
        const auto selected = ::cw::g_cwpy->m_event->get_selectedcard();
        if (selected) header = *selected;
    }
    if (!header) return invalid();
    const auto original = header->get_original();
    if (!original) return invalid();
    const auto owner = header->get_owner();
    if (!owner) return invalid();
    // Pythonの list.index が未所属でValueErrorを出す
    // のに対応し、ネイティブでは invalid_argument を使う。
    // カード位置を仮に作ったり、別の所有コンテナーへ黙って切り替えたりしない。
    const auto index_in = [&](const auto& cards) -> Integer {
        const auto found = std::find(cards.begin(), cards.end(), original);
        if (found == cards.end())
            throw std::invalid_argument("SELECTEDCARD: original card is not in its owner's container");
        return static_cast<Integer>(found - cards.begin()) + 1;
    };
    // 袋のベクターはValueではないため、ネイティブの所有者には共通のBACKPACK/STOREHOUSE
    // の識別値を使う。 Python版と同様、選択ヘッダー
    // の所有者を解決し、別の場所への所属で分岐を変えない。
    if (const auto* name = std::get_if<String>(&owner->storage())) {
        const auto yado = ::cw::g_cwpy && ::cw::g_cwpy->m_ydata ? *::cw::g_cwpy->m_ydata : nullptr;
        if (*name == "STOREHOUSE") {
            if (!yado) throw std::logic_error("SELECTEDCARD: STOREHOUSE owner requires inn data");
            return cardinfo_result(-2, index_in(yado->m_storehouse), -2, line, pos);
        }
        if (*name == "BACKPACK") {
            if (!yado || !yado->m_party) throw std::logic_error("SELECTEDCARD: BACKPACK owner requires a party");
            return cardinfo_result(-1, index_in((*yado->m_party)->m_backpack), -2, line, pos);
        }
        throw std::logic_error("SELECTEDCARD: unsupported owner locator");
    }
    if (const auto character = as_character(*owner)) {
        const Integer castindex = _ccard_index(character);
        if (original->m_type == "ActionCard")
            return cardinfo_result(castindex, 0, original->m_id, line, pos);
        const auto& pockets = character->m_cardpocket_typed;
        const Integer pocket = ::cw::header::cardtype_to_pocket(original->m_type);
        const auto& typed = pocket == 0 ? std::get<0>(pockets) : (pocket == 1 ? std::get<1>(pockets) : std::get<2>(pockets));
        Integer cardindex = index_in(typed);
        for (Integer i = 0; i < pocket; ++i)
            cardindex += static_cast<Integer>(i == 0 ? std::get<0>(pockets).size() : std::get<1>(pockets).size());
        return cardinfo_result(castindex, cardindex, -2, line, pos);
    }
    throw std::logic_error("SELECTEDCARD: unsupported owner type");
}
// 不正なネイティブのカード位置は、コンテナーを参照する前に拒否する。 Python版は castindex
// >= -2、cardindex >= 0 をassertで要求するが、ここではヘッダーなしを返す。
std::optional<std::shared_ptr<::cw::header::CardHeader>> _header_from(std::shared_ptr<ValueType> arg, String func_name, Integer arg_index) {
    auto info = _chk_structure(arg, std::move(func_name), arg_index, "cardinfo"); const Integer castindex = as_integer(_chk_decimal(info->eval(0), "CARDINFO", 0)); Integer cardindex = as_integer(_chk_decimal(info->eval(1), "CARDINFO", 1)); const Integer actionid = as_integer(_chk_decimal(info->eval(2), "CARDINFO", 2)); if (!::cw::g_cwpy || castindex < -2 || cardindex < 0) return std::nullopt;
    if (castindex == 0 || cardindex == 0) { if (::cw::g_cwpy->m_rsrc) { auto it = ::cw::g_cwpy->m_rsrc->m_actioncards.find(actionid); if (it != ::cw::g_cwpy->m_rsrc->m_actioncards.end()) return it->second; } return std::nullopt; }
    if (::cw::g_cwpy->m_ydata) { auto yado = *::cw::g_cwpy->m_ydata; if (castindex == -2) return cardindex <= static_cast<Integer>(yado->m_storehouse.size()) ? std::optional<std::shared_ptr<::cw::header::CardHeader>>(yado->m_storehouse[static_cast<std::size_t>(cardindex - 1)]) : std::nullopt; if (castindex == -1 && yado->m_party) { const auto& bag = (*yado->m_party)->m_backpack; return cardindex <= static_cast<Integer>(bag.size()) ? std::optional<std::shared_ptr<::cw::header::CardHeader>>(bag[static_cast<std::size_t>(cardindex - 1)]) : std::nullopt; } }
    if (castindex < 0) return std::nullopt; // Python版と同様、要求したコンテナーがなければヘッダーなしを返す。
    const auto character = _ccard_from(decimal_result(castindex, 0, 0), "CARDINFO");
    if (!character || !*character) return std::nullopt;
    const auto& pockets = (*character)->m_cardpocket_typed;
    for (const auto* pocket : {&std::get<0>(pockets), &std::get<1>(pockets), &std::get<2>(pockets)}) {
        if (cardindex <= static_cast<Integer>(pocket->size()))
            return (*pocket)[static_cast<std::size_t>(cardindex - 1)];
        cardindex -= static_cast<Integer>(pocket->size());
    }
    return std::nullopt;
}
std::shared_ptr<StringValue> _func_cardname(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "CARDNAME", line, pos); auto values = _all_eval(std::move(args)); const auto header = _header_from(values[0], "CARDNAME", 0); return string_result(header && *header ? (*header)->get_showingname() : String{}, line, pos); }
std::shared_ptr<DecimalValue> _func_cardtype(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "CARDTYPE", line, pos); auto values = _all_eval(std::move(args)); const auto header = _header_from(values[0], "CARDTYPE", 0); if (!header || !*header) return decimal_result(0, line, pos); return decimal_result((*header)->m_type == "ActionCard" ? -1 : static_cast<Real>(::cw::header::cardtype_to_pocket((*header)->m_type) + 1), line, pos); }
std::shared_ptr<DecimalValue> _func_cardrarity(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "CARDRARITY", line, pos); auto values = _all_eval(std::move(args)); const auto header = _header_from(values[0], "CARDRARITY", 0); if (!header || !*header) return decimal_result(-1, line, pos); return decimal_result((*header)->m_premium == "Rare" ? 1 : ((*header)->m_premium == "Premium" ? 2 : 0), line, pos); }
std::shared_ptr<DecimalValue> _func_cardprice(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "CARDPRICE", line, pos); auto values = _all_eval(std::move(args)); const auto header = _header_from(values[0], "CARDPRICE", 0); return decimal_result(header && *header ? static_cast<Real>((*header)->m_price) : -1, line, pos); }
std::shared_ptr<DecimalValue> _func_cardlevel(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "CARDLEVEL", line, pos); auto values = _all_eval(std::move(args)); const auto header = _header_from(values[0], "CARDLEVEL", 0); return decimal_result(header && *header && (*header)->m_type == "SkillCard" ? static_cast<Real>((*header)->m_level) : -1, line, pos); }
std::shared_ptr<DecimalValue> _func_cardcount(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "CARDCOUNT", line, pos); auto values = _all_eval(std::move(args)); const auto header = _header_from(values[0], "CARDCOUNT", 0); return decimal_result(!header || !*header ? -1 : ((*header)->m_type == "ActionCard" ? 0 : static_cast<Real>((*header)->m_uselimit)), line, pos); }
std::shared_ptr<DecimalValue> _func_findkeycode(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) { _chk_argscount2(args, 2, 3, "FINDKEYCODE", line, pos); auto values = _all_eval(std::move(args)); const auto header = _header_from(values[0], "FINDKEYCODE", 0); const String pattern = _chk_string(values[1], "FINDKEYCODE", 0); const Integer start = values.size() == 3 ? as_integer(_chk_minvalue(values[2], "FINDKEYCODE", 0, Value(0))) : 1; if (!header || !*header || start <= 0) return decimal_result(0, line, pos); const Integer index = (*header)->find_keycode_position_excluding_empty([pattern](const ::cw::lib::Arguments& a, const ::cw::lib::KeywordArguments&) { return Value(glob_match(pattern, as_string(a.empty() ? Value() : a[0]))); }, start - 1); return decimal_result(index < 0 ? 0 : static_cast<Real>(index + 1), line, pos); }
std::shared_ptr<ListValue> _func_find_keycodes_with(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount2(args, 1, 3, "FINDKEYCODESWITH", line, pos);
    auto values = _all_eval(std::move(args));
    const String prefix = _chk_string(values[0], "FINDKEYCODESWITH", 0);
    std::set<String> tags;
    if (values.size() >= 2) {
        auto taglist = _chk_list(values[1], "FINDKEYCODESWITH", 1);
        for (Integer i = 0; i < static_cast<Integer>(list_values(taglist->m_value).size()); ++i) {
            tags.insert(::cw::lib::encoding::lower(check_string_indexed(taglist->eval(i), "FINDKEYCODESWITH", Value("1[" + std::to_string(i) + "]"))));
        }
    }
    const bool include = values.size() >= 3 && _chk_boolean(values[2], "FINDKEYCODESWITH", 2);
    bool skill = tags.count("skill") != 0;
    bool item = tags.count("item") != 0;
    bool beast = tags.count("beast") != 0;
    const bool hand = tags.count("hand") != 0;
    if (!skill && !item && !beast && !hand) skill = item = beast = true;

    const bool selected_card = tags.count("selectedcard") != 0;
    const bool want_selected = tags.count("selected") != 0;
    const bool want_backpack = tags.count("backpack") != 0;
    const bool want_party_and_backpack = tags.count("partyandbackpack") != 0;
    std::vector<String> scopes;
    if (want_selected) scopes.push_back("Selected");
    if (want_backpack) scopes.push_back("Backpack");
    if (want_party_and_backpack) scopes.push_back("PartyAndBackpack");
    if (scopes.empty()) scopes.push_back("Selected");

    std::set<String> seen;
    std::vector<Value> out;
    auto add_header = [&](const std::shared_ptr<::cw::header::CardHeader>& header) {
        if (!header) return;
        // Python版と同様、CardHeader.get_keycodes()
        // は既定の with_name=True を使い、カード名も
        // 検索可能なキーコードに含める。 1.20以前の互換規則で除く
        // 場合は例外。 ネイティブは動的な型判定ではなく具体的な型付き
        // 取得関数を使い、 取得・確保の失敗をXMLの Property/KeyCodes
        // への代替で隠さず、そのまま伝える。
        for (const auto& key : header->get_keycodes(true)) {
            if (key.rfind(prefix, 0) != 0) continue;
            const String result = include ? key : key.substr(prefix.size());
            if (seen.insert(result).second) out.push_back(Value(result));
        }
    };
    auto add_character = [&](const std::shared_ptr<::cw::character::Character>& character) {
        if (!character) return;
        const auto& pockets = character->m_cardpocket_typed;
        if (skill) for (const auto& h : std::get<0>(pockets)) add_header(h);
        if (item) for (const auto& h : std::get<1>(pockets)) add_header(h);
        if (beast) for (const auto& h : std::get<2>(pockets)) add_header(h);
        if (hand && ::cw::g_cwpy && ::cw::g_cwpy->is_battlestatus() && character->m_deck) {
            for (const auto& h : character->m_deck->m_hand) add_header(h);
        }
    };
    auto add_target = [&](const Value& target) {
        if (auto character = as_character(target)) {
            add_character(character);
        } else if (auto header = object_as<::cw::header::CardHeader>(target)) {
            add_header(header);
        } else if (auto party = object_as<::cw::data::Party>(target)) {
            for (const auto& h : party->m_backpack) add_header(h);
        }
    };

    if (::cw::g_cwpy && ::cw::g_cwpy->m_event) {
        if (selected_card) {
            auto selected = ::cw::g_cwpy->m_event->get_selectedcard();
            if (selected && (((*selected)->m_type == "SkillCard" && skill) ||
                             ((*selected)->m_type == "ItemCard" && item) ||
                             ((*selected)->m_type == "BeastCard" && beast))) {
                add_header(*selected);
            }
        }
        for (const auto& scope : scopes) {
            for (const auto& target : ::cw::g_cwpy->m_event->get_targetscope(scope, true, false, "")) {
                add_target(target);
            }
        }
    }
    return list_result(std::move(out), line, pos);
}
#if 0 // 第3段階: 生成された空スタブは履歴として保持。
std::shared_ptr<::cw::calculator::StringValue> _get_real_material_path_of_card(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _get_target_menu_keycodes(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StructureValue> _func_get_card_ref_by_keycode(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StructureValue> _func_get_card_ref_by_keycode_invalid_cardinfo()
{
    return {};
}

std::vector<::cw::lib::String> _func_get_card_ref_by_keycode_get_header_keycodes(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header)
{
    (void)arg_header;
    return {};
}

bool _func_get_card_ref_by_keycode_header_has_keycode(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header)
{
    (void)arg_header;
    return {};
}

bool _func_get_card_ref_by_keycode_is_header_in_target_card_type(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header)
{
    (void)arg_header;
    return {};
}

std::shared_ptr<::cw::calculator::StructureValue> _func_get_card_ref_by_keycode_header_to_cardinfo(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header)
{
    (void)arg_header;
    return {};
}

::cw::lib::Value _func_get_card_ref_by_keycode_header_to_cardinfo_lambda_line_2530()
{
    return {};
}

std::optional<std::shared_ptr<::cw::header::CardHeader>> _func_get_card_ref_by_keycode_find_in_character(std::optional<std::shared_ptr<::cw::character::Character>> arg_ch)
{
    (void)arg_ch;
    return {};
}

std::optional<std::shared_ptr<::cw::header::CardHeader>> _func_get_card_ref_by_keycode_find_in_party_backpack(std::optional<::cw::lib::Value> arg_party)
{
    (void)arg_party;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_get_keycode_of_card_with(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::vector<::cw::lib::String> _func_get_keycode_of_card_with_get_header_keycodes(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_h)
{
    (void)arg_h;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _func_zip(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _func_get_cards_of_scenario_by_keycode(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::vector<::cw::lib::String> _func_get_cards_of_scenario_by_keycode_get_header_keycodes(std::optional<std::shared_ptr<::cw::header::CardHeader>> arg_header)
{
    (void)arg_header;
    return {};
}

std::vector<::cw::lib::Value> _func_get_cards_of_scenario_by_keycode_list_by_pocket(::cw::lib::Integer arg_pocket)
{
    (void)arg_pocket;
    return {};
}

void _func_get_cards_of_scenario_by_keycode_push_if_match(::cw::lib::Integer arg_pocket, ::cw::lib::Value arg_header_obj)
{
    (void)arg_pocket;
    (void)arg_header_obj;
}

std::shared_ptr<::cw::calculator::ValueType> _func_at_def(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_lower(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _func_lfind_all(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_get_property_text_of_card_of_scenario(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::vector<::cw::lib::Value> _func_get_property_text_of_card_of_scenario_list_by_pocket(::cw::lib::Integer arg_p)
{
    (void)arg_p;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _get_with_copy_variables(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos, ::cw::lib::String arg_type_name, ::cw::lib::String arg_common_name)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    (void)arg_type_name;
    (void)arg_common_name;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _get_item_with_copy_variables(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _get_skill_with_copy_variables(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _get_beast_with_copy_variables(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_keycodetext(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_unixtime(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _func_for(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

void _func_for__set_target(::cw::lib::Value arg_t, std::shared_ptr<::cw::data::Variant> arg_variant_obj)
{
    (void)arg_t;
    (void)arg_variant_obj;
}

void _func_for__restore_all()
{
}

std::shared_ptr<::cw::calculator::ValueType> _func_switch(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_sum(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

::cw::lib::Value _func_sum_lambda_line_3179(::cw::lib::Value arg_e)
{
    (void)arg_e;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_floor(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_ceil(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_sind(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_cosd(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_tand(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_distance(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ValueType> _func_parseint(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ValueType> _func_parsefloat(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ValueType> _func_eval(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ValueType> _func_eval__eval_value(std::shared_ptr<::cw::calculator::ValueType> arg_v)
{
    (void)arg_v;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_scenarioname(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_cardmaxuselimit(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _func_datetime(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_format(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _func_concatstrings(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _func_makelist(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _func_getrandomlist(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_lfindrand(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _func_pickbyindices(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ValueType> _func_lgetrandom(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}
#endif

// 第3段階実装（カード参照、リスト高階関数、数学・日時・文字列関数）。

std::shared_ptr<::cw::calculator::StringValue> _get_real_material_path_of_card(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "GETREALMATERIALPATHOFCARD", line, pos);
    auto values = _all_eval(std::move(args));
    const auto header = _header_from(values[0], "GETREALMATERIALPATHOFCARD", 0);
    const String image = _chk_string(values[1], "GETREALMATERIALPATHOFCARD", 1);
    if (image.empty()) return string_result({}, line, pos);
    String result;
    try {
        if (header && *header && (*header)->m_scenariocard) {
            const auto scenario = current_sdata();
            result = ::cw::util::join_paths({Value(scenario ? scenario->m_scedir : String{}), Value(image)});
        } else result = ::cw::util::join_yadodir(image);
    } catch (...) { ::cw::util::print_ex(std::nullopt); }
    if (!result.empty() && result.front() != '/') result.insert(result.begin(), '/');
    return string_result(std::move(result), line, pos);
}

std::shared_ptr<::cw::calculator::ListValue> _get_target_menu_keycodes(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 0, "GETTARGETMENUKEYCODES", line, pos);
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_event) return list_result({}, line, pos);
    // 追加した検索では、ネイティブの現在のTargetingが使用カードの対象を管
    // 理する。 Python版は任意の use_card_targets の別名か
    // ら得るが、 ここでは二つ目の対象一覧を作らず、共通の効果イベント経路を使う。
    const auto targeting = ::cw::g_cwpy->m_event->get_effectevent();
    if (!targeting || !*targeting || (*targeting)->m_target_cards.size() != 1) return list_result({}, line, pos);
    const auto menu = std::dynamic_pointer_cast<::cw::sprite::card::MenuCard>((*targeting)->m_target_cards.front());
    if (!menu || !menu->m_events) return list_result({}, line, pos);
    std::vector<Value> result;
    for (const auto& event : menu->m_events->m_event_list) if (event)
        for (const auto& keycode : event->m_keycodes) result.push_back(boxed(string_result(keycode, line, pos)));
    return list_result(std::move(result), line, pos);
}

std::shared_ptr<::cw::calculator::StructureValue> _func_get_card_ref_by_keycode_header_to_cardinfo(std::optional<std::shared_ptr<::cw::header::CardHeader>> header, Integer line, Integer pos) {
    const auto invalid = [&] { return cardinfo_result(0, 0, -2, line, pos); };
    if (!header || !*header) return invalid();
    const auto original = (*header)->get_original();
    if (!original) return invalid();
    const auto owner = (*header)->get_owner();
    // ネイティブの袋のベクターは、CardHeader::get_owner のBACKPACK/STOREHOUSE
    // 識別値を使う。 所属を調べる前にその表現を解決する。 Python
    // 版では、袋に含まれるだけでその袋が所有者とは限らない。
    const auto owner_name = owner ? std::get_if<String>(&owner->storage()) : nullptr;
    if (::cw::g_cwpy && ::cw::g_cwpy->m_ydata && *::cw::g_cwpy->m_ydata) {
        const auto yado = *::cw::g_cwpy->m_ydata;
        if (owner_name && *owner_name == "STOREHOUSE") {
            const auto it = std::find(yado->m_storehouse.begin(), yado->m_storehouse.end(), original);
            if (it == yado->m_storehouse.end()) return invalid();
            return cardinfo_result(-2, static_cast<Integer>(it - yado->m_storehouse.begin()) + 1, -2, line, pos);
        }
        if (owner_name && *owner_name == "BACKPACK" && yado->m_party && *yado->m_party) {
            const auto& bag = (*yado->m_party)->m_backpack;
            const auto it = std::find(bag.begin(), bag.end(), original);
            if (it == bag.end()) return invalid();
            return cardinfo_result(-1, static_cast<Integer>(it - bag.begin()) + 1, -2, line, pos);
        }
    }
    const auto character = owner ? as_character(*owner) : nullptr;
    if (!character) return invalid();
    const Integer castindex = _ccard_index(character);
    if (original->m_type == "ActionCard") return cardinfo_result(castindex, 0, original->m_id, line, pos);
    const Integer pocket = ::cw::header::cardtype_to_pocket(original->m_type);
    const auto& pockets = character->m_cardpocket_typed;
    const auto& cards = pocket == 0 ? std::get<0>(pockets) : (pocket == 1 ? std::get<1>(pockets) : std::get<2>(pockets));
    auto found = std::find(cards.begin(), cards.end(), original);
    if (found == cards.end()) {
        const auto root = original->get_original();
        found = std::find_if(cards.begin(), cards.end(), [&](const auto& candidate) {
            return candidate && candidate->get_original() == root;
        });
    }
    if (found == cards.end()) return invalid();
    Integer index = 1 + static_cast<Integer>(found - cards.begin());
    for (Integer p = 0; p < pocket; ++p)
        index += static_cast<Integer>(p == 0 ? std::get<0>(pockets).size() : std::get<1>(pockets).size());
    return cardinfo_result(castindex, index, -2, line, pos);
}

std::shared_ptr<::cw::calculator::StructureValue> _func_get_card_ref_by_keycode(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "GETCARDREFBYKEYCODE", line, pos);
    auto values = _all_eval(std::move(args));
    const String keycode = _chk_string(values[0], "GETCARDREFBYKEYCODE", 0);
    const auto tags = _chk_list(values[1], "GETCARDREFBYKEYCODE", 1);
    std::set<String> tagset;
    for (Integer i = 0; i < static_cast<Integer>(list_values(tags->m_value).size()); ++i) {
        tagset.insert(::cw::lib::encoding::python_lower(check_string_indexed(tags->eval(i), "GETCARDREFBYKEYCODE", Value("1[" + std::to_string(i) + "]"))));
    }
    bool skill = tagset.count("skill"), item = tagset.count("item"), beast = tagset.count("beast"), hand = tagset.count("hand");
    if (!skill && !item && !beast && !hand) skill = item = beast = true;
    const auto allowed = [&](const std::shared_ptr<::cw::header::CardHeader>& h) {
        return h && ((h->m_type == "SkillCard" && skill) || (h->m_type == "ItemCard" && item) || (h->m_type == "BeastCard" && beast));
    };
    const auto has_key = [&](const std::shared_ptr<::cw::header::CardHeader>& h) {
        if (!h || keycode.empty()) return false;
        try {
            const auto keys = h->get_keycodes(true);
            return std::find(keys.begin(), keys.end(), keycode) != keys.end();
        } catch (...) { ::cw::util::print_ex(std::nullopt); return false; }
    };
    const auto matches = [&](const std::shared_ptr<::cw::header::CardHeader>& h) { return allowed(h) && has_key(h); };
    if (tagset.count("selectedcard") && ::cw::g_cwpy && ::cw::g_cwpy->m_event) {
        const auto selected = ::cw::g_cwpy->m_event->get_selectedcard();
        // Python版は、手札側の ref_original 関数自体を呼ばずに、sel.ref_original()
        // のヘッダーと比較する。 標準CardHeaderでは一致しないため
        // 、共通ルートの同一性へ置き換えない。 ネイティブCardHeader
        // には、動的に関数を格納する ref_original 属性はない。
        if (selected && matches(*selected))
            return _func_get_card_ref_by_keycode_header_to_cardinfo(selected, line, pos);
    }
    std::vector<String> scopes;
    if (tagset.count("selected")) scopes.push_back("Selected");
    if (tagset.count("backpack")) scopes.push_back("Backpack");
    if (tagset.count("partyandbackpack")) scopes.push_back("PartyAndBackpack");
    if (scopes.empty()) scopes.push_back("Selected");
    for (const auto& scope : scopes) if (::cw::g_cwpy && ::cw::g_cwpy->m_event) {
        for (const auto& target : ::cw::g_cwpy->m_event->get_targetscope(scope, true, false, "")) {
            const auto* object = std::get_if<::cw::lib::ObjectPtr>(&target.storage());
            if (!object || !*object) continue;
            // EventInterface はPythonのParty対象を
            // 、荷物袋のVariantSequenceの複製として表す。
            // 全範囲のキャラクターを調べた後ではなく、この位置で処理する。
            if (const auto bag = std::dynamic_pointer_cast<::cw::data::VariantSequence>(*object)) {
                for (const auto& entry : bag->m_values) {
                    const auto* card = std::get_if<::cw::lib::ObjectPtr>(&entry.storage());
                    const auto h = card ? std::dynamic_pointer_cast<::cw::header::CardHeader>(*card) : nullptr;
                    if (matches(h)) return _func_get_card_ref_by_keycode_header_to_cardinfo(h, line, pos);
                }
                continue;
            }
            if (const auto c = as_character(target)) {
                const auto& pockets = c->m_cardpocket_typed;
                const auto find = [&](const auto& seq) -> std::optional<std::shared_ptr<::cw::header::CardHeader>> {
                    for (const auto& h : seq) if (matches(h)) return h;
                    return std::nullopt;
                };
                std::optional<std::shared_ptr<::cw::header::CardHeader>> found;
                if (skill) found = find(std::get<0>(pockets));
                if (!found && item) found = find(std::get<1>(pockets));
                if (!found && beast) found = find(std::get<2>(pockets));
                if (!found && hand && ::cw::g_cwpy->is_battlestatus() && c->m_deck) found = find(c->m_deck->m_hand);
                if (found) return _func_get_card_ref_by_keycode_header_to_cardinfo(found, line, pos);
            }
        }
    }
    return cardinfo_result(0, 0, -2, line, pos);
}

std::shared_ptr<::cw::calculator::StringValue> _func_get_keycode_of_card_with(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 4, "GETKEYCODEOFCARDWITH", line, pos); auto values = _all_eval(std::move(args)); const auto h = _header_from(values[0], "GETKEYCODEOFCARDWITH", 0); const String prefix = _chk_string(values[1], "GETKEYCODEOFCARDWITH", 1); const bool include = _chk_boolean(values[2], "GETKEYCODEOFCARDWITH", 2); const String fallback = values[3]->to_str();
    if (h && *h) for (const auto& key : (*h)->get_keycodes(true)) if (!key.empty() && key.size() > prefix.size() && key.rfind(prefix, 0) == 0) return string_result(include ? key : key.substr(prefix.size()), line, pos);
    return string_result(fallback, line, pos);
}

std::shared_ptr<::cw::calculator::ListValue> _func_zip(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) {
    _chk_argscount2(args, 1, 99999, "ZIP", line, pos); auto values = _all_eval(std::move(args)); std::vector<std::shared_ptr<ListValue>> lists; for (Integer i = 0; i < static_cast<Integer>(values.size()); ++i) lists.push_back(_chk_list(values[static_cast<std::size_t>(i)], "ZIP", i)); std::size_t count = lists.empty() ? 0 : list_values(lists.front()->m_value).size(); for (const auto& l : lists) count = std::min(count, list_values(l->m_value).size()); std::vector<Value> out; for (std::size_t i = 0; i < count; ++i) { std::vector<Value> row; for (const auto& l : lists) row.push_back(boxed(l->eval(static_cast<Integer>(i)))); out.push_back(boxed(list_result(std::move(row), line, pos))); } return list_result(std::move(out), line, pos);
}

std::shared_ptr<::cw::calculator::ListValue> _func_get_cards_of_scenario_by_keycode(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "GETCARDSOFSCENARIOBYKEYCODE", line, pos); auto values = _all_eval(std::move(args)); const String key = _chk_string(values[0], "GETCARDSOFSCENARIOBYKEYCODE", 0); const String scope_raw = _chk_string(values[1], "GETCARDSOFSCENARIOBYKEYCODE", 1); const String scope = utf8_trim_copy(python_upper(scope_raw.empty() ? "ALL" : scope_raw)); auto scenario = std::dynamic_pointer_cast<::cw::data::ScenarioData>(current_sdata()); std::vector<Value> out;
    if (!scenario || (scope != "ALL" && scope != "SKILL" && scope != "ITEM" && scope != "BEAST")) return list_result({}, line, pos);
    auto collect = [&](const auto& resources, Integer pocket, auto getter) {
        // Python版はヘッダー一覧を走査する。ネイティブのリソースマップ
        // では、 取得関数がファイル名を再走査してマップを消去・再作成する
        // 前に、候補IDを取得する。 無効になったイテレーターを進めない。
        std::vector<Integer> ids;
        ids.reserve(resources.size());
        for (const auto& [id, unused] : resources) {
            (void)unused;
            ids.push_back(id);
        }
        for (const auto id : ids) {
            auto carddata = getter(id);
            if (!carddata || !*carddata) continue;
            auto keys = ::cw::util::decodetextlist((*carddata)->gettext("Property/KeyCodes", ""));
            // Python版と同様、XMLのKeyCodesだけでなく CardHeader.get_keycodes()
            // を検索する。 CardWirth 1.20互換
            // を除き、カード名が最後の暗黙のキーコードになる。
            bool with_name = true;
            if (::cw::g_cwpy && ::cw::g_cwpy->m_sct &&
                ::cw::g_cwpy->m_sct->lessthan("1.20", scenario->get_versionhint(::cw::g_HINT_AREA)))
                with_name = false;
            if (with_name) keys.push_back((*carddata)->gettext("Property/Name", ""));
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) continue;
            std::vector<Value> row{boxed(decimal_result(static_cast<Real>(pocket), line, pos)), boxed(decimal_result(static_cast<Real>(id), line, pos))};
            out.push_back(boxed(list_result(std::move(row), line, pos)));
        }
    };
    if (scope == "ALL" || scope == "SKILL") collect(scenario->m__skills, ::cw::g_POCKET_SKILL,
        [&](Integer id) { return scenario->get_skilldata(id, "", true, std::nullopt); });
    if (scope == "ALL" || scope == "ITEM") collect(scenario->m__items, ::cw::g_POCKET_ITEM,
        [&](Integer id) { return scenario->get_itemdata(id, "", true, std::nullopt); });
    if (scope == "ALL" || scope == "BEAST") collect(scenario->m__beasts, ::cw::g_POCKET_BEAST,
        [&](Integer id) { return scenario->get_beastdata(id, "", true, std::nullopt); });
    return list_result(std::move(out), line, pos);
}
std::shared_ptr<::cw::calculator::ValueType> _func_at_def(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 3, "ATDEF", line, pos); auto list = _chk_list(value_type(args[0]({}, {})), "ATDEF", 0); const Integer index = as_integer(_chk_minvalue(value_type(args[1]({}, {})), "ATDEF", 1, Value(1))) - 1; if (index < 0 || index >= static_cast<Integer>(list_values(list->m_value).size())) return value_type(args[2]({}, {})); return list->eval(index);
}
std::shared_ptr<::cw::calculator::StringValue> _func_lower(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "LOWER", line, pos); auto v = _all_eval(std::move(args)); return string_result(::cw::lib::encoding::python_lower(_chk_string(v[0], "LOWER", 0)), line, pos); }
std::shared_ptr<::cw::calculator::ListValue> _func_lfind_all(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) {
    _chk_argscount2(args, 2, 3, "LFINDALL", line, pos); auto list = _chk_list(value_type(args[1]({}, {})), "LFINDALL", 1); Integer start = 0; if (args.size() == 3) { const Integer n = as_integer(_chk_minvalue(value_type(args[2]({}, {})), "LFINDALL", 2, Value(0))); if (n == 0) return list_result({}, line, pos); start = n - 1; if (start >= static_cast<Integer>(list_values(list->m_value).size())) return list_result({}, line, pos); } std::vector<Value> out; if (list_values(list->m_value).empty()) return list_result({}, line, pos); auto search = value_type(args[0]({}, {})); for (Integer i = start; i < static_cast<Integer>(list_values(list->m_value).size()); ++i) if (Operator::equals(search, list->eval(i), "=", true)) out.push_back(boxed(decimal_result(static_cast<Real>(i + 1), line, pos))); return list_result(std::move(out), line, pos);
}

std::shared_ptr<::cw::calculator::StringValue> _func_get_property_text_of_card_of_scenario(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "GETPROPERTYTEXTOFCARDOFSCENARIO", line, pos); auto values = _all_eval(std::move(args)); auto scenario = std::dynamic_pointer_cast<::cw::data::ScenarioData>(current_sdata()); if (!scenario) return string_result({}, line, pos); auto place = _chk_list(values[0], "GETPROPERTYTEXTOFCARDOFSCENARIO", 0); if (list_values(place->m_value).size() < 2) return string_result({}, line, pos); const Integer pocket = static_cast<Integer>(std::floor(as_real(check_decimal_indexed(place->eval(0), "GETPROPERTYTEXTOFCARDOFSCENARIO", Value("0[0]"))))); const Integer id = static_cast<Integer>(std::floor(as_real(check_decimal_indexed(place->eval(1), "GETPROPERTYTEXTOFCARDOFSCENARIO", Value("0[1]"))))); String path = utf8_trim_copy(_chk_string(values[1], "GETPROPERTYTEXTOFCARDOFSCENARIO", 1)); if (path.empty()) return string_result({}, line, pos); if (lower_ascii(path).rfind("property/", 0) == 0) path.erase(0, 9); if (lower_ascii(path) == "property") path.clear(); std::optional<std::shared_ptr<::cw::data::CWPyElement>> data; if (pocket == ::cw::g_POCKET_SKILL) data = scenario->get_skilldata(id, "", true, std::nullopt); else if (pocket == ::cw::g_POCKET_ITEM) data = scenario->get_itemdata(id, "", true, std::nullopt); else if (pocket == ::cw::g_POCKET_BEAST) data = scenario->get_beastdata(id, "", true, std::nullopt); if (!data || !*data) return string_result({}, line, pos); return string_result((*data)->gettext("Property/" + path, ""), line, pos);
}
std::shared_ptr<::cw::calculator::BooleanValue> _get_with_copy_variables(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos, String type_name, String common) {
    // Pythonと同じくGetContentへXMLを渡し、対象カード取得時に変数をコピーする。
    try {
        // Python版は引数検査を含む処理全体を捕捉し、失敗してもTRUEを返す。
        _chk_argscount(args, 4, common, line, pos);
        auto values = _all_eval(std::move(args));
        const Integer resid = as_integer(_chk_decimal(values[0], common, 0));
        const Integer number = as_integer(_chk_decimal(values[1], common, 1));
        // Python版と同様、遅延した範囲を評価する前にカードを解決する。
        const auto header = _header_from(values[3], common, 3);
        String scope = lower_ascii(values[2]->to_str());
        if (scope == "selected") scope = "Selected";
        else if (scope == "random") scope = "Random";
        else if (scope == "field" || scope == "partyandbackpack" || scope == "backpack") scope = "Backpack";
        else if (scope == "enemy") scope = "Enemy";
        else if (scope == "npc") scope = "Npc";
        else if (scope == "fieldcasts") scope = "FieldCasts";
        else if (scope == "couponholder") scope = "CouponHolder";
        else { InvalidArgumentException e; e.initialize("Invalid scope: " + scope, common, 2, values[2]->to_str(), line_of(values[2]), pos_of(values[2])); throw e; }
        const String xml = "<Get type='Item' id='" + std::to_string(resid) + "' targets='" + scope + "' number='" + std::to_string(number) + "'/>";
        const auto data = ::cw::data::xml2element({}, {}, std::nullopt, xml, false, std::nullopt);
        auto content = std::make_shared<::cw::content::GetContent>(); content->initialize(data); content->get_cards(type_name, header, std::vector<std::shared_ptr<::cw::header::CardHeader>>{});
    } catch (...) {
        // Python版と同様、引数評価・カード取得のど
        // ちらの失敗も一度診断し、その後trueを返す。
        ::cw::util::print_ex(std::nullopt);
    }
    return boolean_result(true, line, pos);
}
std::shared_ptr<::cw::calculator::BooleanValue> _get_item_with_copy_variables(std::vector<::cw::lib::Callback> a, std::shared_ptr<::cw::calculator::CalcOption> o, Integer l, Integer p) { return _get_with_copy_variables(std::move(a), std::move(o), l, p, "ItemCard", "GETITEMWITHCOPYVARIABLES"); }
std::shared_ptr<::cw::calculator::BooleanValue> _get_skill_with_copy_variables(std::vector<::cw::lib::Callback> a, std::shared_ptr<::cw::calculator::CalcOption> o, Integer l, Integer p) { return _get_with_copy_variables(std::move(a), std::move(o), l, p, "SkillCard", "GETSKILLWITHCOPYVARIABLES"); }
std::shared_ptr<::cw::calculator::BooleanValue> _get_beast_with_copy_variables(std::vector<::cw::lib::Callback> a, std::shared_ptr<::cw::calculator::CalcOption> o, Integer l, Integer p) { return _get_with_copy_variables(std::move(a), std::move(o), l, p, "BeastCard", "GETBEASTWITHCOPYVARIABLES"); }

std::shared_ptr<::cw::calculator::StringValue> _func_keycodetext(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "KEYCODETEXT", line, pos);
    auto values = _all_eval(std::move(args));
    const auto header = _header_from(values[0], "KEYCODETEXT", 0);
    const Decimal requested = _chk_minvalue(values[1], "KEYCODETEXT", 0, Value(0));
    if (!header || !*header) return string_result({}, line, pos);
    // Pythonの整数はネイティブIntegerを超えられる。 正の
    // 位置が確実に範囲外の場合だけ飽和させるが、 先頭の空キーを除く
    // 処理のためヘッダー取得は呼ぶ。位置0はインデックス-1になる。
    const Integer maximum = std::numeric_limits<Integer>::max();
    const Integer index = requested.compare(Decimal(maximum)) > 0
        ? maximum : as_integer(requested) - 1;
    try { return string_result((*header)->get_keycode_at_excluding_empty(index), line, pos); }
    catch (const std::out_of_range&) { return string_result({}, line, pos); }
}
std::shared_ptr<::cw::calculator::DecimalValue> _func_unixtime(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 0, "UNIXTIME", line, pos); const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); return decimal_result(static_cast<Real>(now), line, pos); }

// 一時的なFORの束縛は eval_expr と同じ、初期値なしを表
// すNoneを使う。 Python版は一時的なコモンの初期値を空文
// 字列にする。 既存の指定済みの対象の束縛は、別に保存・復元する。
std::shared_ptr<::cw::calculator::ListValue> _func_for(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption> option, Integer line, Integer pos) {
    _chk_argscount(args, 3, "FOR", line, pos);
    auto first = value_type(args[0]({}, {})); auto varspec = value_type(args[1]({}, {})); const auto expr = args[2]; std::vector<String> names; bool multi = false;
    if (auto s = std::dynamic_pointer_cast<StringValue>(varspec)) names.push_back(_chk_string(s, "FOR", 1));
    else if (auto l = std::dynamic_pointer_cast<ListValue>(varspec)) { multi = true; for (Integer i = 0; i < static_cast<Integer>(list_values(l->m_value).size()); ++i) names.push_back(_chk_string(l->eval(i), "FOR", 1)); }
    else { InvalidArgumentException e; e.initialize("Second argument must be string or list<string>.", "FOR", 1, varspec->to_str(), line_of(varspec), pos_of(varspec)); throw e; }
    for (const auto& n : names) {
        const String trimmed = utf8_trim_copy(n);
        if (trimmed.empty()) { InvalidArgumentException e; e.initialize("Variable name is empty.", "FOR", 1, varspec->to_str(), line_of(varspec), pos_of(varspec)); throw e; }
    }
    std::shared_ptr<ListValue> loop;
    if (!multi && std::dynamic_pointer_cast<DecimalValue>(first)) { const Decimal count = _chk_minvalue(first, "FOR", 0, Value(0)); const Integer n = count.floor().to_integer(::cw::lib::DecimalRounding::Down); std::vector<Value> values; for (Integer i = 1; i <= n; ++i) values.push_back(boxed(decimal_result(i, line, pos))); loop = list_result(std::move(values), line, pos); }
    else loop = _chk_list(first, "FOR", 0);
    struct Target { String name; std::shared_ptr<::cw::data::Variant> original; bool event_store{}; };
    std::vector<Target> targets; auto event = current_event(); auto data = current_sdata();
    for (const auto& name : names) {
        if (event) { auto it = event->m_variants.find(name); if (it != event->m_variants.end() && it->second) { targets.push_back({name, it->second, true}); continue; } }
        if (data) { auto it = data->m_variants.find(name); if (it != data->m_variants.end() && it->second) { _chk_diffsc(option, line, pos); targets.push_back({name, it->second, false}); continue; } }
        targets.push_back({name, nullptr, false});
    }
    auto put_variant = [&](const Target& t, std::shared_ptr<::cw::data::Variant> variant) {
        if (!t.original) return;
        if (t.event_store) event->set_variant_binding(t.name, std::move(variant));
        else data->m_variants[t.name] = std::move(variant);
    };
    auto make_temp = [&](const Value& value) {
        auto temp = std::make_shared<::cw::data::Variant>();
        temp->initialize(std::nullopt, std::nullopt, value, "", Value());
        return temp;
    };
    auto restore_all = [&]() { for (auto& t : targets) put_variant(t, t.original); };
    std::vector<Value> out;
    try {
        const auto count = static_cast<Integer>(list_values(loop->m_value).size());
        for (Integer i = 0; i < count; ++i) {
            if (!multi) { if (targets[0].original) put_variant(targets[0], make_temp(to_variantvalue(loop->eval(i)))); }
            else { auto row = _chk_list(loop->eval(i), "FOR", 0); const Integer assign = std::min<Integer>(static_cast<Integer>(list_values(row->m_value).size()), static_cast<Integer>(targets.size())); for (Integer j = 0; j < assign; ++j) if (targets[static_cast<std::size_t>(j)].original) put_variant(targets[static_cast<std::size_t>(j)], make_temp(to_variantvalue(row->eval(j)))); }
            out.push_back(boxed(value_type(expr({}, {}))));
        }
    } catch (...) { restore_all(); throw; }
    restore_all();
    return list_result(std::move(out), line, pos);
}
std::shared_ptr<::cw::calculator::ValueType> _func_switch(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 3, "SWITCH", line, pos); Integer index = 0; try { auto n = value_type(args[0]({}, {})); const Decimal value = _chk_decimal(n, "SWITCH", 0); if (value == value.floor() && value.compare(Decimal(1)) >= 0) index = value.to_integer(::cw::lib::DecimalRounding::Down); } catch (...) { index = 0; }
    if (index <= 0) return value_type(args[1]({}, {}));
    std::shared_ptr<ListValue> list;
    try { list = std::dynamic_pointer_cast<ListValue>(value_type(args[2]({}, {}))); }
    catch (...) { return value_type(args[1]({}, {})); }
    if (!list || index > static_cast<Integer>(list_values(list->m_value).size())) return value_type(args[1]({}, {}));
    return list->eval(index - 1);
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_sum(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption> option, Integer line, Integer pos) {
    _chk_argscount2(args, 1, 2, "SUM", line, pos); auto values = _all_eval(std::move(args)); auto list = _chk_list(values[0], "SUM", 0); const bool convert = values.size() == 2 && _chk_boolean(values[1], "SUM", 1); Decimal total(0);
    for (Integer i = 0; i < static_cast<Integer>(list_values(list->m_value).size()); ++i) {
        auto e = list->eval(i);
        // Python版と同様、捕捉するのは変換失敗だけ。 数値の集
        // 計・要素の遅延評価は、それぞれ自身の例外・診断を維持する。
        if (auto d = std::dynamic_pointer_cast<DecimalValue>(e)) {
            total = total.add(d->m_decimal);
        } else if (convert) {
            try {
                std::vector<::cw::lib::Callback> one{[e](const ::cw::lib::Arguments&, const ::cw::lib::KeywordArguments&) { return Value(boxed(e)); }};
                total = total.add(_func_value(std::move(one), option, line_of(e), pos_of(e))->m_decimal);
            } catch (...) {
                const String index = std::to_string(i + 1), text = e->to_str();
                InvalidArgumentException ex; ex.initialize("Cannot convert list element at index " + index + " to number: " + text, "SUM", 0, "list[" + index + "] = " + text, line_of(e), pos_of(e)); throw ex;
            }
        } else {
            const String index = std::to_string(i + 1), text = e->to_str();
            InvalidArgumentException ex; ex.initialize("List element at index " + index + " is not a number: " + text, "SUM", 0, "list[" + index + "] = " + text, line_of(e), pos_of(e)); throw ex;
        }
    }
    return decimal_result(total, line, pos);
}
std::shared_ptr<::cw::calculator::DecimalValue> _func_floor(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "FLOOR", line, pos); auto v = _all_eval(std::move(args)); return decimal_result(_chk_decimal(v[0], "FLOOR", 0).floor(), line, pos); }
std::shared_ptr<::cw::calculator::DecimalValue> _func_ceil(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "CEIL", line, pos); auto v = _all_eval(std::move(args)); return decimal_result(_chk_decimal(v[0], "CEIL", 0).ceil(), line, pos); }
// SIND/COSD/TAND はネイティブのdouble・libmで「度数×π/180」を評価し、decimal_result
// を使う。 Python版の math.radians(float(deg)) と Decimal(str(result))
// とは変換境界が異なり、 数値・文字列の末尾桁が違う場合がある。
std::shared_ptr<::cw::calculator::DecimalValue> _func_sind(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "SIND", line, pos); auto v = _all_eval(std::move(args)); return decimal_result(std::sin(as_real(_chk_decimal(v[0], "SIND", 0)) * 3.14159265358979323846 / 180.0), line, pos); }
std::shared_ptr<::cw::calculator::DecimalValue> _func_cosd(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "COSD", line, pos); auto v = _all_eval(std::move(args)); return decimal_result(std::cos(as_real(_chk_decimal(v[0], "COSD", 0)) * 3.14159265358979323846 / 180.0), line, pos); }
// PythonのTANDは Decimal(str(float_result)) を作るため、 汎用Real
// の max_digits10 表現ではなく、共通のPython互換の最短float表記を使う。
std::shared_ptr<::cw::calculator::DecimalValue> _func_tand(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "TAND", line, pos); auto v = _all_eval(std::move(args)); const Real degrees = as_real(_chk_decimal(v[0], "TAND", 0)); const Real rad = degrees * 3.14159265358979323846 / 180.0; if (std::abs(std::cos(rad)) < 1e-12) { InvalidArgumentException e; e.initialize("TAND is undefined at " + ::cw::lib::python_float_repr(degrees) + " degrees.", "TAND", 0, v[0]->to_str(), line_of(v[0]), pos_of(v[0])); throw e; } return decimal_result(Decimal::parse(::cw::lib::python_float_repr(std::tan(rad))), line, pos); }
// ネイティブの std::hypot と decimal_result はdoubleの結果を保持する。 Python
// の math.hypot は Decimal(str(dist)) を経由するため、末尾桁が異なる場合がある。
std::shared_ptr<::cw::calculator::DecimalValue> _func_distance(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 4, "DISTANCE", line, pos); auto v = _all_eval(std::move(args)); const Real x1 = as_real(_chk_decimal(v[0], "DISTANCE", 0)), y1 = as_real(_chk_decimal(v[1], "DISTANCE", 1)), x2 = as_real(_chk_decimal(v[2], "DISTANCE", 2)), y2 = as_real(_chk_decimal(v[3], "DISTANCE", 3)); return decimal_result(std::hypot(x2 - x1, y2 - y1), line, pos); }

namespace {
bool parse_strict_real(const String& s, Real& out) { static const std::regex number(R"(\s*-?([0-9]+(\.[0-9]*)?|([0-9]*\.)?[0-9]+)\s*)"); if (!std::regex_match(s, number)) return false; try { std::size_t end = 0; out = std::stod(s, &end); return end == s.size() || (end < s.size() && s.substr(end).find_first_not_of(" \t\r\n") == String::npos); } catch (...) { return false; } }
}
std::shared_ptr<::cw::calculator::ValueType> _func_parseint(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 2, "PARSEINT", line, pos); auto v = _all_eval(std::move(args)); try { Decimal value; if (auto d = std::dynamic_pointer_cast<DecimalValue>(v[0])) value = d->m_decimal; else if (auto s = std::dynamic_pointer_cast<StringValue>(v[0])) value = Decimal::parse(utf8_trim_copy(as_string(s->m_value))); else throw ::cw::lib::DecimalInvalidOperation("not a Decimal or String"); return decimal_result(value.trunc(), line, pos); } catch (const ::cw::lib::DecimalError&) { return v[1]; } }
std::shared_ptr<::cw::calculator::ValueType> _func_parsefloat(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 2, "PARSEFLOAT", line, pos); auto v = _all_eval(std::move(args)); try { Decimal value; if (auto d = std::dynamic_pointer_cast<DecimalValue>(v[0])) value = d->m_decimal; else if (auto s = std::dynamic_pointer_cast<StringValue>(v[0])) value = Decimal::parse(utf8_trim_copy(as_string(s->m_value))); else throw ::cw::lib::DecimalInvalidOperation("not a Decimal or String"); return decimal_result(value, line, pos); } catch (const ::cw::lib::DecimalError&) { return v[1]; } }

std::shared_ptr<::cw::calculator::ValueType> _func_eval(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption> option, Integer line, Integer pos) {
    _chk_argscount(args, 1, "EVAL", line, pos); auto root = value_type(args[0]({}, {}));
    std::function<std::shared_ptr<ValueType>(std::shared_ptr<ValueType>)> recurse = [&](std::shared_ptr<ValueType> v) -> std::shared_ptr<ValueType> {
        if (auto s = std::dynamic_pointer_cast<StringValue>(v)) return calculate(parse(as_string(s->m_value)), option);
        if (auto l = std::dynamic_pointer_cast<ListValue>(v)) { std::vector<Value> out; for (Integer i = 0; i < static_cast<Integer>(list_values(l->m_value).size()); ++i) out.push_back(boxed(recurse(l->eval(i)))); return list_result(std::move(out), line, pos); }
        if (auto st = std::dynamic_pointer_cast<StructureValue>(v)) { std::vector<Value> out; for (Integer i = 0; i < static_cast<Integer>(structure_values(*st).size()); ++i) out.push_back(boxed(recurse(st->eval(i)))); auto result = std::make_shared<StructureValue>(); result->initialize(structure_name(*st), std::move(out), line, pos); return result; }
        ArgumentIsNotStringException e; e.initialize(v->to_str() + " is not String.", "EVAL", 0, v->to_str(), line_of(v), pos_of(v)); throw e;
    };
    return recurse(root);
}

std::shared_ptr<::cw::calculator::StringValue> _func_scenarioname(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 0, "SCENARIONAME", line, pos); auto scenario = std::dynamic_pointer_cast<::cw::data::ScenarioData>(current_sdata()); return string_result(scenario ? as_string(scenario->m_name) : String{}, line, pos); }
std::shared_ptr<::cw::calculator::DecimalValue> _func_cardmaxuselimit(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "CARDMAXUSELIMIT", line, pos); auto values = _all_eval(std::move(args)); const auto h = _header_from(values[0], "CARDMAXUSELIMIT", 0); if (!h || !*h) return decimal_result(-1, line, pos); return decimal_result((*h)->m_type == "ActionCard" ? 0 : static_cast<Real>((*h)->m_maxuselimit), line, pos); }
std::shared_ptr<::cw::calculator::ListValue> _func_datetime(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 0, "DATETIME", line, pos); const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::vector<Value> out; for (Integer n : {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec}) out.push_back(boxed(decimal_result(static_cast<Real>(n), line, pos))); return list_result(std::move(out), line, pos); }
std::shared_ptr<::cw::calculator::StringValue> _func_format(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { if (args.empty()) { ArgumentsCountException e; e.initialize("Invalid arguments count: at least 1 required", "FORMAT", line, pos); throw e; } auto v = _all_eval(std::move(args)); String text = _chk_string(v[0], "FORMAT", 0); std::vector<String> replacements; if (text.find("{}") != String::npos) for (std::size_t i = 1; i < v.size(); ++i) replacements.push_back(v[i]->to_str()); String out; std::size_t at = 0, n = 0, replacement = 0; while ((at = text.find("{}", n)) != String::npos) { out += text.substr(n, at - n); out += replacement < replacements.size() ? replacements[replacement++] : "{}"; n = at + 2; } out += text.substr(n); return string_result(std::move(out), line, pos); }
std::shared_ptr<::cw::calculator::StringValue> _func_concatstrings(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "CONCATSTRING", line, pos); auto v = _all_eval(std::move(args)); auto l = _chk_list(v[0], "CONCATSTRING", 0); String out; for (Integer i = 0; i < static_cast<Integer>(list_values(l->m_value).size()); ++i) out += l->eval(i)->to_str(); return string_result(std::move(out), line, pos); }
std::shared_ptr<::cw::calculator::ListValue> _func_makelist(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 2, "MAKELIST", line, pos); auto v = _all_eval(std::move(args)); const Integer a = _chk_decimal(v[0], "MAKELIST", 0).floor().to_integer(::cw::lib::DecimalRounding::Down), b = _chk_decimal(v[1], "MAKELIST", 1).floor().to_integer(::cw::lib::DecimalRounding::Down); std::vector<Value> out; if (a <= b) for (Integer n = a;; ++n) { out.push_back(boxed(decimal_result(n, line, pos))); if (n == b) break; } return list_result(std::move(out), line, pos); }
namespace { ::cw::dice::Dice& calculator_dice() { if (::cw::g_cwpy && ::cw::g_cwpy->m_dice) return *::cw::g_cwpy->m_dice; static ::cw::dice::Dice fallback; return fallback; } }
std::shared_ptr<::cw::calculator::ListValue> _func_getrandomlist(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 3, "GETRANDOMLIST", line, pos); auto v = _all_eval(std::move(args)); const Integer a = _chk_decimal(v[0], "GETRANDOMLIST", 0).floor().to_integer(::cw::lib::DecimalRounding::Down), b = _chk_decimal(v[1], "GETRANDOMLIST", 1).floor().to_integer(::cw::lib::DecimalRounding::Down), count = std::max<Integer>(0, _chk_decimal(v[2], "GETRANDOMLIST", 2).floor().to_integer(::cw::lib::DecimalRounding::Down)); if (b < a) { InvalidArgumentException e; e.initialize("End value (" + std::to_string(b) + ") must be greater than or equal to start (" + std::to_string(a) + ").", "GETRANDOMLIST", 1, v[1]->to_str(), line_of(v[1]), pos_of(v[1])); throw e; } const Decimal length = Decimal(b).subtract(Decimal(a)).add(Decimal(1)); if (Decimal(count) > length) { InvalidArgumentException e; e.initialize("Requested count (" + std::to_string(count) + ") exceeds available range length (" + length.to_string(false) + ").", "GETRANDOMLIST", 2, v[2]->to_str(), line_of(v[2]), pos_of(v[2])); throw e; } std::vector<Value> pool; for (Integer n = a;; ++n) { pool.emplace_back(n); if (n == b) break; } pool = ::cw::dice::sample_values(std::move(pool), count); std::vector<Value> out; for (Integer i = 0; i < count; ++i) out.push_back(boxed(decimal_result(as_integer(pool[static_cast<std::size_t>(i)]), line, pos))); return list_result(std::move(out), line, pos); }
std::shared_ptr<::cw::calculator::DecimalValue> _func_lfindrand(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 2, "LFINDRAND", line, pos); auto v = _all_eval(std::move(args)); auto list = _chk_list(v[1], "LFINDRAND", 1); std::vector<Value> matches; for (Integer i = 0; i < static_cast<Integer>(list_values(list->m_value).size()); ++i) if (Operator::equals(v[0], list->eval(i), "=", true)) matches.emplace_back(i + 1); if (matches.empty()) return decimal_result(0, line, pos); return decimal_result(as_integer(calculator_dice().choice_exists(std::move(matches))), line, pos); }
std::shared_ptr<::cw::calculator::ListValue> _func_pickbyindices(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 2, "PICKBYINDICES", line, pos); auto v = _all_eval(std::move(args)); auto list = _chk_list(v[0], "PICKBYINDICES", 0); auto indices = _chk_list(v[1], "PICKBYINDICES", 1); const auto source = list_values(list->m_value); std::vector<Value> out; for (Integer i = 0; i < static_cast<Integer>(list_values(indices->m_value).size()); ++i) { auto n = indices->eval(i); const Integer index = check_minvalue_indexed(n, "PICKBYINDICES", Value("1[" + std::to_string(i) + "]"), Value(1)).floor().to_integer(::cw::lib::DecimalRounding::Down) - 1; if (index < 0 || index >= static_cast<Integer>(source.size())) { ListIndexOutOfRangeException e; e.initialize("List index is out of range.", "PICKBYINDICES", 1, n->to_str(), index + 1, static_cast<Integer>(source.size()), line_of(n), pos_of(n)); throw e; } out.push_back(source[static_cast<std::size_t>(index)]); } return list_result(std::move(out), line, pos); }
std::shared_ptr<::cw::calculator::ValueType> _func_lgetrandom(std::vector<::cw::lib::Callback> args, std::shared_ptr<::cw::calculator::CalcOption>, Integer line, Integer pos) { _chk_argscount(args, 1, "LGETRANDOM", line, pos); auto v = _all_eval(std::move(args)); auto list = _chk_list(v[0], "LGETRANDOM", 0); const auto size = list_values(list->m_value).size(); if (!size) { ListIndexOutOfRangeException e; e.initialize("List index is out of range.", "LGETRANDOM", 0, v[0]->to_str(), 1, 0, line_of(v[0]), pos_of(v[0])); throw e; } const auto index = static_cast<Integer>(std::floor(::cw::dice::random_fraction() * static_cast<Real>(size))); return list->eval(index); }

#if 0 // 第4段階: 生成された空本体を履歴として保持し、実装本体へ移行。
std::shared_ptr<::cw::calculator::BooleanValue> _func_lset(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_lsetmulti(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_lremove(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_lremovemulti(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_linsert(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_lunique(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_lremoveall(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_lcount(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_contain(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_inpolygon(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::tuple<::cw::lib::Real, ::cw::lib::Real> _func_inpolygon__to_xy(std::shared_ptr<::cw::calculator::ValueType> arg_val, ::cw::lib::String arg_func_name, ::cw::lib::Integer arg_arg_idx)
{
    (void)arg_val;
    (void)arg_func_name;
    (void)arg_arg_idx;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_mousex(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_mousey(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ValueType> _func_getvariantvalueofcard(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_getflagvalueofcard(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::DecimalValue> _func_getstepvalueofcard(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_has_variantvalue(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_has_step(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_has_flag(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_setvariantvalueofcard(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_setstepvalueofcard(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _func_setflagvalueofcard(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::ListValue> _func_get_allcards(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::StringValue> _get_propertytext(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}

std::shared_ptr<::cw::calculator::BooleanValue> _have_propertytext(std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    (void)arg_args;
    (void)arg_option;
    (void)arg_line;
    (void)arg_pos;
    return {};
}
#endif

// リスト変更、カード変数、位置と範囲の判定、プロパティ参照。Python の例外
// 境界を保ち、引数検査の失敗は送出し、変更時の変換失敗は False を返す。
namespace {
using DataVariant = ::cw::data::Variant;
using DataSequence = ::cw::data::VariantSequence;

[[noreturn]] void stage4_invalid(const String& message, const String& function, Integer index,
                                  const String& value, Integer line, Integer pos) {
    InvalidArgumentException e;
    e.initialize(message, function, index, value, line, pos);
    throw e;
}

std::optional<String> stage4_varname(const std::shared_ptr<ValueType>& value,
                                     const String& function, Integer index,
                                     bool reject_empty = true) {
    auto string_value = std::dynamic_pointer_cast<StringValue>(value);
    if (!string_value) return std::nullopt; // PythonのLSET系は型不正をFalseにする。
    const String name = as_string(string_value->m_value);
    if (reject_empty && utf8_trim_copy(name).empty()) {
        stage4_invalid("Variable name is empty.", function, index, value->to_str(), line_of(value), pos_of(value));
    }
    return name;
}

std::shared_ptr<DataVariant> stage4_variant(const String& name,
                                            const std::shared_ptr<CalcOption>& option,
                                            Integer line, Integer pos,
                                            const std::shared_ptr<ValueType>& name_value) {
    if (auto event = current_event()) {
        const auto it = event->m_variants.find(name);
        if (it != event->m_variants.end() && it->second) return it->second;
    }
    if (auto data = current_sdata()) {
        const auto it = data->m_variants.find(name);
        if (it != data->m_variants.end() && it->second) {
            _chk_diffsc(option, line, pos);
            return it->second;
        }
    }
    VariantNotFoundException e;
    e.initialize("Variant \"" + name + "\" is not found.", name, line_of(name_value), pos_of(name_value));
    throw e;
}

std::shared_ptr<DataSequence> stage4_sequence(const std::shared_ptr<DataVariant>& variant) {
    return variant ? object_as<DataSequence>(variant->m_value) : nullptr;
}

Integer stage4_index(const std::shared_ptr<ValueType>& value, const String& function, Integer arg_index) {
    const Decimal min_value = _chk_minvalue(value, function, arg_index, Value(1));
    return min_value.floor().to_integer(::cw::lib::DecimalRounding::Down);
}

std::vector<Integer> stage4_path(const std::shared_ptr<ValueType>& index_value,
                                 const String& function, Integer arg_index) {
    if (auto decimal = std::dynamic_pointer_cast<DecimalValue>(index_value)) {
        return {stage4_index(decimal, function, arg_index)};
    }
    if (auto list = std::dynamic_pointer_cast<ListValue>(index_value)) {
        const auto values = list_values(list->m_value);
        if (values.empty()) stage4_invalid("Index list is empty.", function, arg_index, index_value->to_str(), line_of(index_value), pos_of(index_value));
        std::vector<Integer> path;
        path.reserve(values.size());
        for (Integer i = 0; i < static_cast<Integer>(values.size()); ++i) path.push_back(check_minvalue_indexed(list->eval(i), function, Value(std::to_string(arg_index) + "[" + std::to_string(i) + "]"), Value(1)).floor().to_integer(::cw::lib::DecimalRounding::Down));
        return path;
    }
    stage4_invalid("Index must be number or list of numbers: " + index_value->to_str(), function, arg_index, index_value->to_str(), line_of(index_value), pos_of(index_value));
}

template<typename T>
std::shared_ptr<T> stage4_card_variable(const ::cw::lib::ValueMap& table, const String& name) {
    const auto it = table.find(name);
    return it == table.end() ? nullptr : object_as<T>(it->second);
}

void stage4_update_variant(const std::shared_ptr<DataVariant>& variant) {
    if (variant) variant->set(variant->m_value, true);
}

std::tuple<Real, Real> stage4_point(const std::shared_ptr<ValueType>& value,
                                    const String& function, const Value& arg_index, bool nested_coordinates = false) {
    auto list = check_list_indexed(value, function, arg_index);
    if (list_values(list->m_value).size() != 2) {
        InvalidArgumentException error;
        error.initialize("Point must be a two-element list.", function, 0, value->to_str(), line_of(value), pos_of(value));
        error.m_arg_index = arg_index;
        throw error;
    }
    // Python版と同様、どちらかの座標を検証する前に、両メンバーを評価する。
    const auto xv = list->eval(0);
    const auto yv = list->eval(1);
    const String prefix = as_string(arg_index);
    const Value xi = nested_coordinates ? Value(prefix + "[0]") : arg_index;
    const Value yi = nested_coordinates ? Value(prefix + "[1]") : arg_index;
    const auto x = check_decimal_indexed(xv, function, xi);
    const auto y = check_decimal_indexed(yv, function, yi);
    return {as_real(x), as_real(y)};
}
}

std::shared_ptr<BooleanValue> _func_lset(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption> option, Integer line, Integer pos) {
    _chk_argscount(args, 3, "LSET", line, pos);
    auto values = _all_eval(std::move(args));
    const auto name = stage4_varname(values[0], "LSET", 0);
    if (!name) return boolean_result(false, line, pos);
    const auto variant = stage4_variant(*name, option, line, pos, values[0]);
    const auto root = stage4_sequence(variant);
    if (!root) return boolean_result(false, line, pos);
    const auto path = stage4_path(values[1], "LSET", 1); // Pythonではこの検査はtryの外。
    try {
        const Value replacement = to_variantvalue(values[2]);
        // 遅延変換が、コモン値全体を置き換える場合がある。
        auto current = stage4_sequence(variant);
        if (!current) return boolean_result(false, line, pos);
        for (std::size_t depth = 0; depth < path.size(); ++depth) {
            const Integer one_based = path[depth];
            if (one_based < 1) return boolean_result(false, line, pos);
            const auto index = static_cast<std::size_t>(one_based - 1);
            if (index >= current->m_values.size()) return boolean_result(false, line, pos);
            if (depth + 1 == path.size()) current->m_values[index] = replacement;
            else {
                current = object_as<DataSequence>(current->m_values[index]);
                if (!current) return boolean_result(false, line, pos);
            }
        }
        stage4_update_variant(variant);
        return boolean_result(true, line, pos);
    } catch (...) {
        return boolean_result(false, line, pos);
    }
}

std::shared_ptr<BooleanValue> _func_lsetmulti(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption> option, Integer line, Integer pos) {
    _chk_argscount(args, 3, "LSETMULTI", line, pos);
    auto values = _all_eval(std::move(args));
    const auto name = stage4_varname(values[0], "LSETMULTI", 0);
    if (!name) return boolean_result(false, line, pos);
    const auto indexes = _chk_list(values[1], "LSETMULTI", 1);
    const auto replacements = _chk_list(values[2], "LSETMULTI", 2);
    const auto variant = stage4_variant(*name, option, line, pos, values[0]);
    const auto root = stage4_sequence(variant);
    if (!root) return boolean_result(false, line, pos);
    const auto index_values = list_values(indexes->m_value);
    const auto replacement_values = list_values(replacements->m_value);
    if (index_values.size() != replacement_values.size()) return boolean_result(false, line, pos);
    std::vector<std::size_t> positions;
    positions.reserve(index_values.size());
    for (Integer i = 0; i < static_cast<Integer>(index_values.size()); ++i) {
        const auto index = indexes->eval(i); // Python版と同様、評価エラーは外へ伝える。
        Integer n;
        try { n = stage4_index(index, "LSETMULTI", 1); }
        catch (...) { return boolean_result(false, line, pos); }
        if (n < 1) return boolean_result(false, line, pos);
        positions.push_back(static_cast<std::size_t>(n - 1));
    }
    for (const auto index : positions)
        if (index >= root->m_values.size()) return boolean_result(false, line, pos);
    std::vector<Value> converted;
    converted.reserve(replacement_values.size());
    for (Integer i = 0; i < static_cast<Integer>(replacement_values.size()); ++i) {
        std::shared_ptr<ValueType> value;
        try { value = replacements->eval(i); }
        catch (...) { return boolean_result(false, line, pos); }
        converted.push_back(to_variantvalue(value)); // Python版と同様、変換エラーは外へ伝える。
    }
    try {
        const auto current = stage4_sequence(variant);
        if (!current) return boolean_result(false, line, pos);
        for (std::size_t i = 0; i < positions.size(); ++i) {
            // 後の位置で失敗しても、Python版と同様、それ以前の代入を残す。
            if (positions[i] >= current->m_values.size()) return boolean_result(false, line, pos);
            current->m_values[positions[i]] = converted[i];
        }
        stage4_update_variant(variant);
        return boolean_result(true, line, pos);
    } catch (...) {
        return boolean_result(false, line, pos);
    }
}

std::shared_ptr<BooleanValue> _func_lremove(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption> option, Integer line, Integer pos) {
    _chk_argscount(args, 2, "LREMOVE", line, pos);
    auto values = _all_eval(std::move(args));
    const auto name = stage4_varname(values[0], "LREMOVE", 0);
    if (!name) return boolean_result(false, line, pos);
    const auto variant = stage4_variant(*name, option, line, pos, values[0]);
    const auto root = stage4_sequence(variant);
    if (!root) return boolean_result(false, line, pos);
    try {
        const Integer n = stage4_index(values[1], "LREMOVE", 1);
        if (n < 1 || static_cast<std::size_t>(n - 1) >= root->m_values.size()) return boolean_result(false, line, pos);
        root->m_values.erase(root->m_values.begin() + static_cast<std::ptrdiff_t>(n - 1));
        stage4_update_variant(variant);
        return boolean_result(true, line, pos);
    } catch (...) { return boolean_result(false, line, pos); }
}

std::shared_ptr<BooleanValue> _func_lremovemulti(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption> option, Integer line, Integer pos) {
    _chk_argscount(args, 2, "LREMOVEMULTI", line, pos);
    auto values = _all_eval(std::move(args));
    const auto name = stage4_varname(values[0], "LREMOVEMULTI", 0);
    if (!name) return boolean_result(false, line, pos);
    const auto index_list = _chk_list(values[1], "LREMOVEMULTI", 1);
    const auto variant = stage4_variant(*name, option, line, pos, values[0]);
    const auto root = stage4_sequence(variant);
    if (!root) return boolean_result(false, line, pos);
    std::vector<std::size_t> positions;
    const auto raw = list_values(index_list->m_value);
    for (Integer i = 0; i < static_cast<Integer>(raw.size()); ++i) {
        const auto value = index_list->eval(i);
        Integer n;
        try { n = stage4_index(value, "LREMOVEMULTI", 1); }
        catch (...) { return boolean_result(false, line, pos); }
        if (n < 1 || static_cast<std::size_t>(n - 1) >= root->m_values.size()) return boolean_result(false, line, pos);
        positions.push_back(static_cast<std::size_t>(n - 1));
    }
    std::sort(positions.rbegin(), positions.rend());
    for (const auto index : positions) {
        // Pythonの list.pop と同様、重複位置も含め、前の削除が
        // 成功した後で例外になる場合がある。 その部分変更は保つが、後の範囲
        // 外失敗はPythonのIndexErrorではなく、 評価側のSemanticsException
        // として公開する意図的な境界とする。
        if (index >= root->m_values.size()) {
            stage4_update_variant(variant);
            throw_semantics("List index out of range.", line, pos);
        }
        root->m_values.erase(root->m_values.begin() + static_cast<std::ptrdiff_t>(index));
    }
    stage4_update_variant(variant);
    return boolean_result(true, line, pos);
}

std::shared_ptr<BooleanValue> _func_linsert(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption> option, Integer line, Integer pos) {
    _chk_argscount(args, 3, "LINSERT", line, pos);
    auto values = _all_eval(std::move(args));
    // PythonのLINSERTは、LSET/LREMOVEと異なり、空・空白のキーも検索する。
    const auto name = stage4_varname(values[0], "LINSERT", 0, false);
    if (!name) return boolean_result(false, line, pos);
    const auto variant = stage4_variant(*name, option, line, pos, values[0]);
    const auto root = stage4_sequence(variant);
    if (!root) return boolean_result(false, line, pos);
    try {
        const Integer n = stage4_index(values[1], "LINSERT", 1);
        if (n < 1 || static_cast<std::size_t>(n - 1) > root->m_values.size()) return boolean_result(false, line, pos);
        const Value replacement = to_variantvalue(values[2]);
        const auto current = stage4_sequence(variant);
        if (!current) return boolean_result(false, line, pos);
        // Pythonの list.insert と同様、遅延評価の変更後は、以前有効だった位置を範囲内に収める。
        const auto index = std::min(static_cast<std::size_t>(n - 1), current->m_values.size());
        current->m_values.insert(current->m_values.begin() + static_cast<std::ptrdiff_t>(index), replacement);
        stage4_update_variant(variant);
        return boolean_result(true, line, pos);
    } catch (...) { return boolean_result(false, line, pos); }
}

std::shared_ptr<BooleanValue> _func_lunique(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption> option, Integer line, Integer pos) {
    _chk_argscount(args, 1, "LUNIQUE", line, pos);
    auto values = _all_eval(std::move(args));
    // Python版のLUNIQUEは非文字列だけFalseにし、空文字列は通常の
    // 変数名として検索してVariantNotFoundExceptionにする。
    const auto name = stage4_varname(values[0], "LUNIQUE", 0, false);
    if (!name) return boolean_result(false, line, pos);
    const auto variant = stage4_variant(*name, option, line, pos, values[0]);
    const auto root = stage4_sequence(variant);
    if (!root) return boolean_result(false, line, pos);
    std::vector<Value> unique;
    for (const auto& value : root->m_values) {
        const String text = DataVariant::value_to_str(value, false);
        if (std::none_of(unique.begin(), unique.end(), [&](const Value& prior) { return DataVariant::value_to_str(prior, false) == text; })) unique.push_back(value);
    }
    // Python版と同様、新しいリストを代入し、以前の値への別参照は保持する。
    auto replacement = std::make_shared<DataSequence>();
    replacement->m_values = std::move(unique);
    variant->m_value = boxed(replacement);
    stage4_update_variant(variant);
    return boolean_result(true, line, pos);
}

std::shared_ptr<BooleanValue> _func_lremoveall(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption> option, Integer line, Integer pos) {
    _chk_argscount(args, 2, "LREMOVEALL", line, pos);
    auto values = _all_eval(std::move(args));
    const auto name = stage4_varname(values[0], "LREMOVEALL", 0);
    if (!name) return boolean_result(false, line, pos);
    const auto variant = stage4_variant(*name, option, line, pos, values[0]);
    const auto root = stage4_sequence(variant);
    if (!root) return boolean_result(false, line, pos);
    try {
        const Value target = to_variantvalue(values[1]);
        const String target_text = DataVariant::value_to_str(target, false);
        const auto current = stage4_sequence(variant);
        if (!current) return boolean_result(false, line, pos);
        auto replacement = std::make_shared<DataSequence>();
        for (const auto& value : current->m_values)
            if (DataVariant::value_to_str(value, false) != target_text) replacement->m_values.push_back(value);
        // 共有コンテナーではなく、このコモンの現在の一覧だけを置き換える。
        variant->m_value = boxed(replacement);
        stage4_update_variant(variant);
        return boolean_result(true, line, pos);
    } catch (...) { return boolean_result(false, line, pos); }
}

std::shared_ptr<DecimalValue> _func_lcount(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "LCOUNT", line, pos);
    auto values = _all_eval(std::move(args));
    const auto list = _chk_list(values[0], "LCOUNT", 0);
    const Value target = to_variantvalue(values[1]);
    const String target_text = DataVariant::value_to_str(target, false);
    Integer count = 0;
    const auto raw = list_values(list->m_value);
    for (Integer i = 0; i < static_cast<Integer>(raw.size()); ++i) if (DataVariant::value_to_str(to_variantvalue(list->eval(i)), false) == target_text) ++count;
    return decimal_result(count, line, pos);
}

std::shared_ptr<BooleanValue> _func_contain(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "CONTAIN", line, pos);
    auto values = _all_eval(std::move(args));
    const auto lhs_list = std::dynamic_pointer_cast<ListValue>(values[0]);
    const auto rhs_list = std::dynamic_pointer_cast<ListValue>(values[1]);
    if (lhs_list && rhs_list) {
        const auto left_size = list_values(lhs_list->m_value).size(), right_size = list_values(rhs_list->m_value).size();
        if ((left_size == 4 && right_size == 2) || (left_size == 2 && right_size == 4)) {
            try {
                const auto diamond = left_size == 4 ? lhs_list : rhs_list;
                const auto point = left_size == 4 ? rhs_list : lhs_list;
                const Real x1 = as_real(_chk_decimal(diamond->eval(0), "CONTAIN", 0));
                const Real y1 = as_real(_chk_decimal(diamond->eval(1), "CONTAIN", 0));
                const Real x2 = as_real(_chk_decimal(diamond->eval(2), "CONTAIN", 0));
                const Real y2 = as_real(_chk_decimal(diamond->eval(3), "CONTAIN", 0));
                const Real px = as_real(_chk_decimal(point->eval(0), "CONTAIN", 1));
                const Real py = as_real(_chk_decimal(point->eval(1), "CONTAIN", 1));
                return boolean_result(x1 <= px && px <= x2 && y1 <= py && py <= y2, line, pos);
            } catch (...) {
                // Python版と同様、どちらかの形が数値で
                // なければ、通常のリスト内の包含判定へ進む。
            }
        }
    }
    const auto haystack = _chk_list(values[1], "CONTAIN", 1);
    const auto count = list_values(haystack->m_value).size();
    for (std::size_t i = 0; i < count; ++i)
        if (Operator::equals(values[0], haystack->eval(static_cast<Integer>(i)), "=", true)) return boolean_result(true, line, pos);
    return boolean_result(false, line, pos);
}

std::shared_ptr<BooleanValue> _func_inpolygon(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "INPOLYGON", line, pos);
    auto values = _all_eval(std::move(args));
    const auto polygon = _chk_list(values[0], "INPOLYGON", 0);
    // 型付きエラーの意図的な境界として、検索座標にはネイティブのDecimal
    // 検証の ArgumentIsNotDecimalException を
    // 保つ。 Python版は検索点の変換を InvalidArgumentException
    // で包み、多角形の頂点変換とは分ける。 有効な形状は同じ経
    // 路を使い、ネイティブのエラーはより具体的な引数分類を残す。
    const auto point = [&] {
        try { return stage4_point(values[1], "INPOLYGON", Value(Integer{1})); }
        catch (const ArgumentIsNotListException&) {
            InvalidArgumentException error;
            error.initialize("Point must be a two-element list.", "INPOLYGON", 1, values[1]->to_str(), line_of(values[1]), pos_of(values[1]));
            throw error;
        }
    }();
    const auto vertices = list_values(polygon->m_value);
    if (vertices.empty()) return boolean_result(false, line, pos);
    std::vector<std::tuple<Real, Real>> xy;
    xy.reserve(vertices.size());
    for (Integer i = 0; i < static_cast<Integer>(vertices.size()); ++i) xy.push_back(stage4_point(polygon->eval(i), "INPOLYGON", Value("0[" + std::to_string(i) + "]"), true));
    const Real px = std::get<0>(point), py = std::get<1>(point), eps = 1e-9;
    bool inside = false;
    for (std::size_t i = 0, j = xy.size() - 1; i < xy.size(); j = i++) {
        const Real xi = std::get<0>(xy[i]), yi = std::get<1>(xy[i]), xj = std::get<0>(xy[j]), yj = std::get<1>(xy[j]);
        if (std::abs(px - xi) <= eps && std::abs(py - yi) <= eps) return boolean_result(true, line, pos);
        const Real cross = (px - xi) * (yj - yi) - (py - yi) * (xj - xi);
        const Real minx = std::min(xi, xj) - eps, maxx = std::max(xi, xj) + eps, miny = std::min(yi, yj) - eps, maxy = std::max(yi, yj) + eps;
        if (std::abs(cross) <= eps && px >= minx && px <= maxx && py >= miny && py <= maxy) return boolean_result(true, line, pos);
        if (((yi > py) != (yj > py)) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) inside = !inside;
    }
    return boolean_result(inside, line, pos);
}

std::optional<Integer> action_mouse_coordinate(std::size_t index) {
    if (!::cw::g_cwpy || index > 1) return std::nullopt;
    const auto convert = [](const Value& value) -> std::optional<Integer> {
        try {
            if (value.is_none()) return std::nullopt;
            return as_integer(value);
        } catch (const ::cw::lib::DecimalError&) { return std::nullopt; }
    };
    const auto& input = ::cw::g_cwpy->m__actionevent_input;
    const auto flat = input.find(index == 0 ? "mouse.x" : "mouse.y");
    if (flat != input.end()) if (const auto value = convert(flat->second)) return value;
    for (const auto* key : {"mouse", "mouse.pos", "mouse_pos"}) {
        const auto it = input.find(key); if (it == input.end()) continue;
        const auto sequence = object_as<::cw::data::VariantSequence>(it->second);
        if (sequence && index < sequence->m_values.size()) if (const auto value = convert(sequence->m_values[index])) return value;
    }
    return std::nullopt;
}

std::shared_ptr<DecimalValue> _func_mousex(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 0, "MOUSEX", line, pos);
    return decimal_result(action_mouse_coordinate(0).value_or(-1), line, pos);
}
std::shared_ptr<DecimalValue> _func_mousey(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 0, "MOUSEY", line, pos);
    return decimal_result(action_mouse_coordinate(1).value_or(-1), line, pos);
}

std::shared_ptr<ValueType> _func_getvariantvalueofcard(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "GETVARIANTVALUEOFCARD", line, pos);
    auto values = _all_eval(std::move(args));
    const auto header = _header_from(values[0], "GETVARIANTVALUEOFCARD", 0);
    const String name = _chk_string(values[1], "GETVARIANTVALUEOFCARD", 1);
    if (!header || !*header) stage4_invalid("Invalid card reference: " + values[0]->to_str(), "GETVARIANTVALUEOFCARD", 0, values[0]->to_str(), line_of(values[0]), pos_of(values[0]));
    const auto it = (*header)->m_variants.find(name);
    if (it == (*header)->m_variants.end()) { VariantNotFoundException e; e.initialize("Variant \"" + name + "\" is not found.", name, line_of(values[1]), pos_of(values[1])); throw e; }
    const auto variant = object_as<DataVariant>(it->second);
    return variant ? _variantvalue_to_valuetype(variant->m_value, line, pos) : _variantvalue_to_valuetype(it->second, line, pos);
}

std::shared_ptr<BooleanValue> _func_getflagvalueofcard(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "GETFLAGVALUEOFCARD", line, pos);
    auto values = _all_eval(std::move(args));
    const auto header = _header_from(values[0], "GETFLAGVALUEOFCARD", 0);
    const String name = _chk_string(values[1], "GETFLAGVALUEOFCARD", 1);
    if (!header || !*header) return boolean_result(false, line, pos);
    const auto flag = stage4_card_variable<::cw::data::Flag>((*header)->m_flags, name);
    if (flag) return boolean_result(flag->m_value, line, pos);
    stage4_invalid("Flag \"" + name + "\" does not exist in the card header.", "GETFLAGVALUEOFCARD", 1, name, line, pos);
}

std::shared_ptr<DecimalValue> _func_getstepvalueofcard(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "GETSTEPVALUEOFCARD", line, pos);
    auto values = _all_eval(std::move(args));
    const auto header = _header_from(values[0], "GETSTEPVALUEOFCARD", 0);
    const String name = _chk_string(values[1], "GETSTEPVALUEOFCARD", 1);
    if (!header || !*header) return decimal_result(0, line, pos);
    const auto step = stage4_card_variable<::cw::data::Step>((*header)->m_steps, name);
    return decimal_result(step ? static_cast<Real>(step->m_value) : 0, line, pos);
}

std::shared_ptr<BooleanValue> _func_has_variantvalue(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "HAS_VARIANTVALUE", line, pos); auto values = _all_eval(std::move(args)); const auto header = _header_from(values[0], "HAS_VARIANTVALUE", 0); const String name = _chk_string(values[1], "HAS_VARIANTVALUE", 1); return boolean_result(header && *header && (*header)->m_variants.find(name) != (*header)->m_variants.end(), line, pos);
}
std::shared_ptr<BooleanValue> _func_has_step(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "HAS_STEP", line, pos); auto values = _all_eval(std::move(args)); const auto header = _header_from(values[0], "HAS_STEP", 0); const String name = _chk_string(values[1], "HAS_STEP", 1); return boolean_result(header && *header && (*header)->m_steps.find(name) != (*header)->m_steps.end(), line, pos);
}
std::shared_ptr<BooleanValue> _func_has_flag(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "HAS_FLAG", line, pos); auto values = _all_eval(std::move(args)); const auto header = _header_from(values[0], "HAS_FLAG", 0); const String name = _chk_string(values[1], "HAS_FLAG", 1); return boolean_result(header && *header && (*header)->m_flags.find(name) != (*header)->m_flags.end(), line, pos);
}

namespace {
// Python版との意図的な違い。SET*VALUEOFCARD
// は can_write_variables() を呼び、
// その contain_xml() がカードXMLを読み込
// むと同時に削除予定にする。 未読の荷物袋・カード置場のカー
// ドでは、その後XMLを書き戻さず、次の保存でカードが消える
// 場合がある。 ここでは削除予定にせず読み込み、変更成功時は
// 同じカードファイルへ保存し、carddataを未読状態へ戻
// す。 write_xmlは他の宿XMLと同様にTempへ
// 向ける。読み込み済みのPCカードなどはPython版の経
// 路を維持する。 変数マップは残し、設定後のGET*VALUEOFCARD
// でも読み込んだ値を参照できるようにする。
class CardVariableWriteScope {
public:
    explicit CardVariableWriteScope(std::shared_ptr<::cw::header::CardHeader> header)
        : m_header(std::move(header))
    {
        if (!m_header || !m_header->m_carddata.is_none() || m_header->m_fpath.empty()) return;
        m_header->do_write(false);
        const auto tree = ::cw::data::yadoxml2etree(m_header->m_fpath, "", std::nullopt);
        if (!tree || !tree->getroot()) throw std::runtime_error("card XML cannot be read: " + m_header->m_fpath);
        m_header->m_carddata = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(tree->getroot()));
        m_header->m_flags.clear(); m_header->m_steps.clear(); m_header->m_variants.clear();
        for (const auto& [n, flag] : ::cw::data::init_flags(m_header->m_carddata, true)) m_header->m_flags[n] = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(flag));
        for (const auto& [n, step] : ::cw::data::init_steps(m_header->m_carddata, true)) m_header->m_steps[n] = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(step));
        for (const auto& [n, variant] : ::cw::data::init_variants(m_header->m_carddata, true)) m_header->m_variants[n] = ::cw::lib::Value(std::static_pointer_cast<::cw::lib::Object>(variant));
        active_loads().push_back(this);
        m_loaded = true;
    }
    ~CardVariableWriteScope() {
        if (m_loaded) {
            auto& loads = active_loads();
            loads.erase(std::remove(loads.begin(), loads.end(), this), loads.end());
            m_header->m_carddata = {};
        }
    }
    CardVariableWriteScope(const CardVariableWriteScope&) = delete;
    CardVariableWriteScope& operator=(const CardVariableWriteScope&) = delete;

    // ネイティブ版の意図的な保存拡張として、入れ子の成功した設定も
    // 、外側の未読カードの範囲を共有して自身の変数を保存する。
    // ここで書くことで、外側の変換が失敗しても内側の成功を残す。
    template<typename T>
    void store(const std::shared_ptr<T>& variable) {
        if (!variable || !m_header) return;
        const auto& loads = active_loads();
        if (std::none_of(loads.begin(), loads.end(), [&](const auto* scope) {
                return scope->m_header == m_header;
            })) return; // 元から読み込み済みのカードは、既存の書き込み処理を維持する。
        variable->write_value();
        const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_header->m_carddata.storage());
        const auto data = object && *object ? std::dynamic_pointer_cast<::cw::data::CWPyElement>(*object) : nullptr;
        if (!data) return;
        ::cw::data::CWPyElementTree tree(data, m_header->m_fpath);
        tree.write_xml(true);
        m_header->m_fpath = tree.m_fpath;
    }

private:
    static std::vector<CardVariableWriteScope*>& active_loads() {
        static thread_local std::vector<CardVariableWriteScope*> loads;
        return loads;
    }
    std::shared_ptr<::cw::header::CardHeader> m_header;
    bool m_loaded{};
};
}

std::shared_ptr<BooleanValue> _func_setvariantvalueofcard(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 3, "SETVARIANTVALUEOFCARD", line, pos); auto values = _all_eval(std::move(args)); const auto header = _header_from(values[0], "SETVARIANTVALUEOFCARD", 0); const String name = _chk_string(values[1], "SETVARIANTVALUEOFCARD", 1); if (!header || !*header) return boolean_result(false, line, pos); CardVariableWriteScope scope(*header); if (!(*header)->can_write_variables()) return boolean_result(false, line, pos); const auto variant = stage4_card_variable<DataVariant>((*header)->m_variants, name); if (!variant) return boolean_result(false, line, pos); { variant->set(to_variantvalue(values[2]), true); scope.store(variant); return boolean_result(true, line, pos); }}
std::shared_ptr<BooleanValue> _func_setstepvalueofcard(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 3, "SETSTEPVALUEOFCARD", line, pos); auto values = _all_eval(std::move(args)); const auto header = _header_from(values[0], "SETSTEPVALUEOFCARD", 0); const String name = _chk_string(values[1], "SETSTEPVALUEOFCARD", 1); const Decimal raw = _chk_decimal(values[2], "SETSTEPVALUEOFCARD", 2); if (raw != raw.floor() || !header || !*header) return boolean_result(false, line, pos); CardVariableWriteScope scope(*header); if (!(*header)->can_write_variables()) return boolean_result(false, line, pos); const auto step = stage4_card_variable<::cw::data::Step>((*header)->m_steps, name); if (!step) return boolean_result(false, line, pos); step->set(raw.to_integer(::cw::lib::DecimalRounding::Down), true); scope.store(step); return boolean_result(true, line, pos);
}
std::shared_ptr<BooleanValue> _func_setflagvalueofcard(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 3, "SETFLAGVALUEOFCARD", line, pos); auto values = _all_eval(std::move(args)); const auto header = _header_from(values[0], "SETFLAGVALUEOFCARD", 0); const String name = _chk_string(values[1], "SETFLAGVALUEOFCARD", 1); const bool value = _chk_boolean(values[2], "SETFLAGVALUEOFCARD", 2); if (!header || !*header) return boolean_result(false, line, pos); CardVariableWriteScope scope(*header); if (!(*header)->can_write_variables()) return boolean_result(false, line, pos); const auto flag = stage4_card_variable<::cw::data::Flag>((*header)->m_flags, name); if (!flag) return boolean_result(false, line, pos); flag->set(value, true); scope.store(flag); return boolean_result(true, line, pos);
}

std::shared_ptr<ListValue> _func_get_allcards(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount2(args, 1, 2, "GETALLCARDS", line, pos); auto values = _all_eval(std::move(args)); const auto character = _ccard_from(values[0], "GETALLCARDS");
    std::vector<String> types{"SKILL", "ITEM", "BEAST"};
    if (values.size() > 1) { String type = python_upper(utf8_trim_copy(_chk_string(values[1], "GETALLCARDS", 1))); if (!type.empty() && type != "ALL") { if (type != "SKILL" && type != "ITEM" && type != "BEAST") stage4_invalid("Invalid card type: " + type, "GETALLCARDS", 1, values[1]->to_str(), line_of(values[1]), pos_of(values[1])); types = {type}; } }
    if (!character || !*character) return list_result({}, line, pos);
    const auto& pockets = (*character)->m_cardpocket_typed; const Integer castindex = _ccard_index(*character); const std::array<std::size_t, 3> sizes{std::get<0>(pockets).size(), std::get<1>(pockets).size(), std::get<2>(pockets).size()}; std::vector<Value> out;
    for (const auto& type : types) { const std::size_t p = type == "SKILL" ? 0 : (type == "ITEM" ? 1 : 2); const Integer offset = static_cast<Integer>(p == 0 ? 0 : (p == 1 ? sizes[0] : sizes[0] + sizes[1])); for (std::size_t i = 0; i < sizes[p]; ++i) out.push_back(boxed(cardinfo_result(castindex, offset + static_cast<Integer>(i) + 1, -2, line, pos))); }
    return list_result(std::move(out), line, pos);
}

std::shared_ptr<StringValue> _get_propertytext(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "GETPROPERTYTEXT", line, pos); auto values = _all_eval(std::move(args)); const auto header = _header_from(values[0], "GETPROPERTYTEXT", 0); const String property = values[1]->to_str(); if (!header || !*header) return string_result({}, line, pos); const auto carddata = object_as<::cw::data::CWPyElement>((*header)->m_carddata); if (!carddata) return string_result({}, line, pos); const auto found = carddata->find("Property/" + property); return found && *found ? string_result(carddata->gettext("Property/" + property, String{}), line, pos) : string_result({}, line, pos);
}
std::shared_ptr<BooleanValue> _have_propertytext(std::vector<::cw::lib::Callback> args, std::shared_ptr<CalcOption>, Integer line, Integer pos) {
    _chk_argscount(args, 2, "HAVEPROPERTYTEXT", line, pos); auto values = _all_eval(std::move(args)); const auto header = _header_from(values[0], "HAVEPROPERTYTEXT", 0); const String property = values[1]->to_str(); if (!header || !*header) return boolean_result(false, line, pos); const auto carddata = object_as<::cw::data::CWPyElement>((*header)->m_carddata); const auto found = carddata ? carddata->find("Property/" + property) : std::nullopt; return boolean_result(found && *found, line, pos);
}

std::shared_ptr<::cw::calculator::StructureValue> _create_structure(std::shared_ptr<::cw::calculator::StructureInfo> arg_info, std::vector<::cw::lib::Callback> arg_args, std::shared_ptr<::cw::calculator::CalcOption> arg_option, ::cw::lib::Integer arg_line, ::cw::lib::Integer arg_pos)
{
    const String name = python_upper(as_string(arg_info->m_name)); const String evaltype = arg_option ? as_string(arg_option->m_evaltype) : String{}; const bool is_public = *std::get_if<bool>(&arg_info->m_is_public.storage()); if (!is_public && evaltype != "Debugger" && evaltype != "Test") { ExprPermissionError e; e.initialize("Structure " + name + " is not accesible.", arg_info, std::nullopt, arg_line, arg_pos); throw e; }
    const auto members = members_of(*arg_info); _chk_argscount2(arg_args, as_integer(arg_info->m_required_member_num), static_cast<Integer>(members.size()), name, arg_line, arg_pos);
    std::vector<Value> boxed_values;
    boxed_values.reserve(std::max(arg_args.size(), members.size()));
    for (auto& callback : arg_args) boxed_values.push_back(callback_value(std::move(callback)));
    while (boxed_values.size() < members.size())
        boxed_values.push_back(boxed(_variantvalue_to_valuetype(
            members[boxed_values.size()]->m_defvalue, arg_line, arg_pos)));
    auto result = std::make_shared<StructureValue>(); result->initialize(as_string(arg_info->m_name), std::move(boxed_values), arg_line, arg_pos); return result;
}

bool _assert_s(::cw::lib::String arg_expr, ::cw::lib::String arg_n)
{
    auto option = std::make_shared<CalcOption>(); option->initialize("Test", false);
    return _assert_s_val(calculate(parse(std::move(arg_expr)), option), std::move(arg_n));
}

bool _assert_s_val(std::shared_ptr<::cw::calculator::ValueType> arg_val, ::cw::lib::String arg_n)
{
    const auto value = std::dynamic_pointer_cast<StringValue>(arg_val);
    if (!value) throw std::logic_error("calculator assertion expected StringValue");
    return as_string(value->m_value) == arg_n;
}

bool _assert_d(::cw::lib::String arg_expr, ::cw::lib::Value arg_n)
{
    auto option = std::make_shared<CalcOption>(); option->initialize("Test", false);
    return _assert_d_val(calculate(parse(std::move(arg_expr)), option), std::move(arg_n));
}

bool _assert_d_val(std::shared_ptr<::cw::calculator::ValueType> arg_val, ::cw::lib::Value arg_n)
{
    const auto value = std::dynamic_pointer_cast<DecimalValue>(arg_val);
    if (!value) throw std::logic_error("calculator assertion expected DecimalValue");
    return as_real(value->m_value) == as_real(arg_n);
}

bool _assert_b(::cw::lib::String arg_expr, bool arg_n)
{
    auto option = std::make_shared<CalcOption>(); option->initialize("Test", false);
    return _assert_b_val(calculate(parse(std::move(arg_expr)), option), arg_n);
}

bool _assert_b_val(std::shared_ptr<::cw::calculator::ValueType> arg_val, bool arg_n)
{
    const auto value = std::dynamic_pointer_cast<BooleanValue>(arg_val);
    if (!value) throw std::logic_error("calculator assertion expected BooleanValue");
    return std::get<bool>(value->m_value.storage()) == arg_n;
}

void main()
{
    const std::array<std::tuple<String, Decimal>, 10> decimals{{
        {"--5", Decimal(5)}, {"---5", Decimal(-5)}, {"(5+8) % 3", Decimal(1)},
        {"9/3", Decimal(3)}, {"LEN(\"TEST\")", Decimal(4)},
        {"LEN(\"あいう\")", Decimal(3)}, {"FIND(\"い\", \"あいう\")", Decimal(2)},
        {"VALUE(\"42.123\")", Decimal::parse("42.123")},
        {"AT(LIST(1,2,3),2)", Decimal(2)}, {"PLAYER+ENEMY", Decimal(3)}
    }};
    for (const auto& [expression, expected] : decimals)
        if (!_assert_d(expression, decimal_to_compat(expected))) throw std::logic_error("calculator self-check failed: " + expression);
    const std::array<std::tuple<String, bool>, 10> booleans{{
        {"true or false", true}, {"true and false", false}, {"not false", true},
        {"4<=4", true}, {"LIST(1,2)=LIST(1,2)", true}, {"LIST(1,2)<>LIST(1,3)", true},
        {"NOT 1 = 1", false}, {"NOT 1 + 2 = 3", false},
        {"\"1\" = 1", true}, {"\"TRUE\" = TRUE", true}
    }};
    for (const auto& [expression, expected] : booleans)
        if (!_assert_b(expression, expected)) throw std::logic_error("calculator self-check failed: " + expression);
    const std::array<std::tuple<String, String>, 7> strings{{
        {"LEFT(\"abc\",2)", "ab"}, {"STR(42)", "42"}, {"\"A\"~NEWLINE~\"B\"", "A\nB"},
        {"LEFT(\"あいう\",1)", "あ"}, {"RIGHT(\"あいう\",1)", "う"}, {"MID(\"あいう\",2,1)", "い"},
        {"\"A\"\"B\"", "A\"B"}
    }};
    for (const auto& [expression, expected] : strings)
        if (!_assert_s(expression, expected)) throw std::logic_error("calculator self-check failed: " + expression);

    // CWSengokuのEVALは、LIST・構造値の内側の文字列
    // を受け付け、同じ形で返す。 実際のカスタムコンテントが使うPython
    // 参照式であり、 結果をATで参照することで、EVALが各要
    // 素を消費するまでLISTが遅延評価されることも確認する。
    const String nested_eval =
        "EVAL(LIST(\"AT(LIST(1,2,3,4), 3)\", \"AT(LIST(1,2,3,4), 4)\"))";
    if (!_assert_d("AT(" + nested_eval + ", 1)", Value(3)) ||
        !_assert_d("AT(" + nested_eval + ", 2)", Value(4)))
        throw std::logic_error("calculator self-check failed: " + nested_eval);

    // SUMの任意の変換フラグは、Python版と同じ厳密な既定値に
    // する。 第二引数がTRUEでなければ、一覧内の文字列は不正。
    bool rejected_unconverted_sum = false;
    try {
        (void)calculate(parse("SUM(LIST(\"2\", \"3\"), FALSE)"),
                        [] { auto option = std::make_shared<CalcOption>(); option->initialize("Test", false); return option; }());
    } catch (const InvalidArgumentException&) {
        rejected_unconverted_sum = true;
    }
    if (!rejected_unconverted_sum || !_assert_d("SUM(LIST(\"2\", \"3\"), TRUE)", Value(5)))
        throw std::logic_error("calculator self-check failed: SUM conversion parity");

    // Pythonの字句解析と同様、str.isspace() のUnicode空白を受け付ける。
    if (!_assert_d("1　+ 2", Value(3)))
        throw std::logic_error("calculator self-check failed: Unicode whitespace parity");

    // Python版と同様、コモンの一覧は再帰的に変換する。 行動
    // イベントの接触範囲の式が使う、座標一覧の一覧の形に対応する。
    auto row = std::make_shared<::cw::data::VariantSequence>();
    row->m_values = {Value(0), Value(0), Value(10), Value(10)};
    auto rows = std::make_shared<::cw::data::VariantSequence>();
    rows->m_values = {boxed(row)};
    auto converted = std::dynamic_pointer_cast<ListValue>(
        _variantvalue_to_valuetype(boxed(rows), 1, 1));
    if (!converted || !std::dynamic_pointer_cast<ListValue>(converted->eval(0)))
        throw std::logic_error("calculator self-check failed: recursive Variant list conversion");

    // 実際のカスタムコンテントが使う、改行・タブを含む文法を維持する。 リテ
    // ラルを使い、この解析・評価の確認を動作中のゲーム状態から独立させる。
    const String touch_expression =
        "lfind(\nTRUE,\nfor(list_e(list_e(0, 0, 10, 10)), \n\t\"unused\", \n"
        "contain(list_e(0, 0, 10, 10), list_e(mousex(), mousey()))\n)\n)";
    if (!_assert_d(touch_expression, Value(0)))
        throw std::logic_error("calculator self-check failed: multiline FOR/CONTAIN expression");

    if (!_assert_s("LOWER(\"ÄBCΣК\")", "äbcσк"))
        throw std::logic_error("calculator self-check failed: Unicode LOWER parity");
    if (!_assert_b("CONTAIN(LIST(0,0,\"x\",10), LIST(1,1))", false))
        throw std::logic_error("calculator self-check failed: CONTAIN fallback parity");

    auto option = std::make_shared<CalcOption>();
    option->initialize("Test", false);
    const auto quoted_list = calculate(parse("LIST(\"A\"\"B\", \"\")"), option);
    for (const auto& expression : {"LIST(1,)", "LIST(1, 2,)", "LIST(LIST(1,),)", "LIST()"}) {
        const auto result = calculate(parse(expression), option);
        if (!result) throw std::logic_error("calculator self-check failed: trailing comma parity");
    }
    if (calculate(parse("LIST(1,)"), option)->to_str() != "LIST(1)" ||
        calculate(parse("LIST(LIST(1,),)"), option)->to_str() != "LIST(LIST(1))")
        throw std::logic_error("calculator self-check failed: trailing comma list values");
    // Pythonの parse_arguments と同様、末尾
    // コンマの後は閉じ括弧だけでなく入力終端も受け付ける。 共通の
    // 解析処理が許す形式を拒否せず、自己検査もその規則に合わせる。
    if (calculate(parse("LIST(1,"), option)->to_str() != "LIST(1)")
        throw std::logic_error("calculator self-check failed: EOF after trailing comma parity");
    for (const auto& expression : {"LIST(,)", "LIST(1,,2)"}) {
        bool rejected = false;
        try { (void)parse(expression); }
        catch (const SemanticsException&) { rejected = true; }
        if (!rejected) throw std::logic_error("calculator self-check failed: empty argument rejection");
    }
    if (!quoted_list || quoted_list->to_str() != "LIST(\"A\"\"B\", \"\")")
        throw std::logic_error("calculator self-check failed: list string quoting parity");
    const auto cardinfo = calculate(parse("CARDINFO(1, 2)"), option);
    if (!cardinfo || cardinfo->to_str() != "CARDINFO(1, 2)")
        throw std::logic_error("calculator self-check failed: structure string/default parity");
    if (is_valid_structure("unknown", {}) ||
        !is_valid_structure("CARDINFO", {{"CASTINDEX", Value(1)}, {"CARDINDEX", Value(2)}}) ||
        is_valid_structure("CARDINFO", {{"CASTINDEX", Value(String("1"))}, {"CARDINDEX", Value(2)}}))
        throw std::logic_error("calculator self-check failed: VALIDATESTRUCT parity");
    const auto completed = complete_optionalmembers("CARDINFO", {Value(1), Value(2)});
    if (completed.size() != 3 || ::cw::data::Variant::value_to_str(completed[2], false) != "-2")
        throw std::logic_error("calculator self-check failed: optional structure member parity");
    bool rejected_missing_nested_type = false;
    try {
        auto list_element = ::cw::data::make_element("Value", "", std::map<String, String>{{"type", "List"}});
        list_element->append(::cw::data::make_element("Value", "1"));
        (void)::cw::data::Variant::value_from_element(list_element, "type", std::optional<String>("value"));
    } catch (const std::invalid_argument&) { rejected_missing_nested_type = true; }
    if (!rejected_missing_nested_type)
        throw std::logic_error("calculator self-check failed: missing nested variant type parity");
}

}

