#!/usr/bin/env python3
"""Generate compileable C++ declarations from the CardWirthPy cw package.

This is intentionally a declaration-first porting tool. Function bodies are
dummy implementations, while annotations and simple assignments are mapped to
plausible C++ types. Re-run it whenever the Python source changes.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import keyword
import re
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Set, Tuple


CPP_KEYWORDS = {
    "alignas", "alignof", "and", "and_eq", "asm", "atomic_cancel", "atomic_commit",
    "atomic_noexcept", "auto", "bitand", "bitor", "bool", "break", "case", "catch",
    "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept", "const",
    "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await", "co_return",
    "co_yield", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else",
    "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
    "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not",
    "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected", "public",
    "reflexpr", "register", "reinterpret_cast", "requires", "return", "short", "signed",
    "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "synchronized",
    "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid",
    "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t",
    "while", "xor", "xor_eq",
}

MAGIC_NAMES = {
    "__init__": "initialize",
    "__call__": "call",
    "__str__": "to_string",
    "__repr__": "representation",
    "__len__": "size",
    "__iter__": "iterate",
    "__next__": "next",
    "__getitem__": "get_item",
    "__setitem__": "set_item",
    "__delitem__": "delete_item",
    "__contains__": "contains",
    "__eq__": "equals",
    "__ne__": "not_equals",
    "__lt__": "less_than",
    "__le__": "less_equal",
    "__gt__": "greater_than",
    "__ge__": "greater_equal",
    "__enter__": "enter",
    "__exit__": "exit",
    "__bool__": "to_bool",
    "__nonzero__": "to_bool",
    "__hash__": "hash_value",
}


def cpp_identifier(name: str) -> str:
    name = re.sub(r"[^A-Za-z0-9_]", "_", name)
    if not name:
        name = "unnamed"
    if name[0].isdigit():
        name = "n_" + name
    if name in CPP_KEYWORDS or name.startswith("__"):
        name = "py_" + name.strip("_")
    return name


def dotted(node: Optional[ast.AST]) -> str:
    if node is None:
        return ""
    if isinstance(node, ast.Name):
        return node.id
    if isinstance(node, ast.Attribute):
        prefix = dotted(node.value)
        return (prefix + "." if prefix else "") + node.attr
    if isinstance(node, ast.Constant) and isinstance(node.value, str):
        return node.value
    return ""


def annotation_text(node: Optional[ast.AST]) -> str:
    if node is None:
        return ""
    try:
        return ast.dump(node, annotate_fields=False, include_attributes=False)
    except TypeError:
        return ast.dump(node)


def module_name(root: Path, path: Path) -> str:
    relative = path.relative_to(root).with_suffix("")
    parts = list(relative.parts)
    if parts and parts[-1] == "__init__":
        parts.pop()
    return ".".join(["cw"] + parts)


def namespace_for(module: str) -> Tuple[str, ...]:
    return tuple(cpp_identifier(part) for part in module.split("."))


@dataclass
class FunctionInfo:
    module: str
    original_name: str
    generated_name: str
    node: ast.AST
    line: int
    class_key: Optional[str] = None
    is_static: bool = False
    is_property: bool = False
    is_setter: bool = False


@dataclass
class MemberInfo:
    original_name: str
    cpp_name: str
    annotation: Optional[ast.AST] = None
    values: List[ast.AST] = field(default_factory=list)


@dataclass
class ClassInfo:
    key: str
    module: str
    original_name: str
    cpp_name: str
    line: int
    base_nodes: List[ast.AST] = field(default_factory=list)
    methods: List[FunctionInfo] = field(default_factory=list)
    members: Dict[str, MemberInfo] = field(default_factory=dict)
    class_variables: Dict[str, MemberInfo] = field(default_factory=dict)


@dataclass
class GlobalInfo:
    original_name: str
    cpp_name: str
    annotation: Optional[ast.AST] = None
    values: List[ast.AST] = field(default_factory=list)


@dataclass
class ModuleInfo:
    name: str
    path: Path
    imports: Dict[str, str] = field(default_factory=dict)
    functions: List[FunctionInfo] = field(default_factory=list)
    globals: Dict[str, GlobalInfo] = field(default_factory=dict)
    lambda_count: int = 0


def assigned_names(target: ast.AST) -> Iterable[str]:
    if isinstance(target, ast.Name):
        yield target.id
    elif isinstance(target, (ast.Tuple, ast.List)):
        for child in target.elts:
            yield from assigned_names(child)


def self_member_names(target: ast.AST) -> Iterable[str]:
    if isinstance(target, ast.Attribute) and isinstance(target.value, ast.Name) and target.value.id == "self":
        yield target.attr
    elif isinstance(target, (ast.Tuple, ast.List)):
        for child in target.elts:
            yield from self_member_names(child)


class SourceCollector(ast.NodeVisitor):
    def __init__(self, module: ModuleInfo, classes: Dict[str, ClassInfo]) -> None:
        self.module = module
        self.classes = classes
        self.class_stack: List[str] = []
        self.function_stack: List[str] = []

    def visit_ClassDef(self, node: ast.ClassDef) -> None:
        flattened = "_".join(self.class_stack + [node.name])
        key = self.module.name + "." + ".".join(self.class_stack + [node.name])
        info = self.classes.get(key)
        if info is None:
            info = ClassInfo(key, self.module.name, node.name, cpp_identifier(flattened), node.lineno)
            self.classes[key] = info
        info.base_nodes.extend(node.bases)
        self._collect_class_variables(node, info)
        self.class_stack.append(node.name)
        for statement in node.body:
            self.visit(statement)
        self.class_stack.pop()

    def _collect_class_variables(self, node: ast.ClassDef, info: ClassInfo) -> None:
        for statement in node.body:
            if isinstance(statement, ast.AnnAssign):
                for name in assigned_names(statement.target):
                    item = info.class_variables.setdefault(name, MemberInfo(name, "s_" + cpp_identifier(name)))
                    item.annotation = statement.annotation
                    if statement.value:
                        item.values.append(statement.value)
            elif isinstance(statement, ast.Assign):
                for target in statement.targets:
                    for name in assigned_names(target):
                        item = info.class_variables.setdefault(name, MemberInfo(name, "s_" + cpp_identifier(name)))
                        item.values.append(statement.value)

    def _decorator_names(self, node: ast.AST) -> List[str]:
        return [dotted(item) for item in getattr(node, "decorator_list", [])]

    def _visit_function(self, node: ast.AST) -> None:
        name = getattr(node, "name")
        decorators = self._decorator_names(node)
        direct_method = bool(self.class_stack) and not self.function_stack
        is_setter = any(item.endswith(".setter") for item in decorators)
        is_property = "property" in decorators or is_setter
        is_static = "staticmethod" in decorators or "classmethod" in decorators

        if direct_method:
            class_key = self.module.name + "." + ".".join(self.class_stack)
            info = self.classes[class_key]
            generated = MAGIC_NAMES.get(name, cpp_identifier(name))
            if is_setter:
                generated = "set_" + cpp_identifier(name)
            elif is_property:
                generated = "get_" + cpp_identifier(name)
            function = FunctionInfo(self.module.name, name, generated, node, node.lineno,
                                    class_key, is_static, is_property, is_setter)
            info.methods.append(function)
            self._collect_members(node, info)
        else:
            prefix = self.class_stack + self.function_stack
            generated = cpp_identifier("_".join(prefix + [name])) if prefix else cpp_identifier(name)
            self.module.functions.append(FunctionInfo(self.module.name, name, generated, node, node.lineno))

        self.function_stack.append(name)
        function_args = getattr(node, "args")
        for expression in list(function_args.defaults) + [item for item in function_args.kw_defaults if item]:
            self.visit(expression)
        for expression in getattr(node, "decorator_list", []):
            self.visit(expression)
        for statement in getattr(node, "body", []):
            self.visit(statement)
        self.function_stack.pop()

    def visit_FunctionDef(self, node: ast.FunctionDef) -> None:
        self._visit_function(node)

    def visit_AsyncFunctionDef(self, node: ast.AsyncFunctionDef) -> None:
        self._visit_function(node)

    def visit_Lambda(self, node: ast.Lambda) -> None:
        self.module.lambda_count += 1
        name = "lambda_line_{}".format(node.lineno)
        if self.class_stack or self.function_stack:
            name = "_".join(self.class_stack + self.function_stack + [name])
        fake = ast.FunctionDef(name=name, args=node.args, body=[], decorator_list=[],
                               returns=None, type_comment=None)
        fake.lineno = node.lineno
        self.module.functions.append(FunctionInfo(self.module.name, "<lambda>", cpp_identifier(name), fake, node.lineno))
        self.generic_visit(node)

    def _collect_members(self, node: ast.AST, info: ClassInfo) -> None:
        for child in ast.walk(node):
            if isinstance(child, ast.AnnAssign):
                for name in self_member_names(child.target):
                    item = info.members.setdefault(name, MemberInfo(name, "m_" + cpp_identifier(name)))
                    item.annotation = child.annotation
                    if child.value:
                        item.values.append(child.value)
            elif isinstance(child, ast.Assign):
                for target in child.targets:
                    for name in self_member_names(target):
                        item = info.members.setdefault(name, MemberInfo(name, "m_" + cpp_identifier(name)))
                        item.values.append(child.value)
            elif isinstance(child, ast.AugAssign):
                for name in self_member_names(child.target):
                    item = info.members.setdefault(name, MemberInfo(name, "m_" + cpp_identifier(name)))
                    item.values.append(child.value)


def resolve_relative_import(current: str, level: int, imported_module: Optional[str]) -> str:
    package = current.split(".")[:-1]
    if level:
        keep = max(1, len(package) - (level - 1))
        package = package[:keep]
    elif imported_module:
        return imported_module
    if imported_module:
        package.extend(imported_module.split("."))
    return ".".join(package)


def collect_module_globals(tree: ast.Module, module: ModuleInfo) -> None:
    def walk_statements(statements: Sequence[ast.stmt]) -> None:
        for statement in statements:
            if isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                continue
            if isinstance(statement, ast.AnnAssign):
                for name in assigned_names(statement.target):
                    item = module.globals.setdefault(name, GlobalInfo(name, "g_" + cpp_identifier(name)))
                    item.annotation = statement.annotation
                    if statement.value:
                        item.values.append(statement.value)
            elif isinstance(statement, ast.Assign):
                for target in statement.targets:
                    for name in assigned_names(target):
                        item = module.globals.setdefault(name, GlobalInfo(name, "g_" + cpp_identifier(name)))
                        item.values.append(statement.value)
            for field_name in ("body", "orelse", "finalbody"):
                children = getattr(statement, field_name, None)
                if children:
                    walk_statements(children)
            for handler in getattr(statement, "handlers", []):
                walk_statements(handler.body)
    walk_statements(tree.body)


class TypeResolver:
    def __init__(self, modules: Dict[str, ModuleInfo], classes: Dict[str, ClassInfo]) -> None:
        self.modules = modules
        self.classes = classes
        self.simple_classes: Dict[str, List[str]] = defaultdict(list)
        for key, info in classes.items():
            self.simple_classes[info.original_name].append(key)

    def class_cpp_name(self, key: str) -> str:
        info = self.classes[key]
        ns = "::".join(namespace_for(info.module))
        return "::" + ns + "::" + info.cpp_name

    def resolve_class_key(self, name: str, module: str) -> Optional[str]:
        if not name:
            return None
        name = name.strip("'\"")
        if name in self.classes:
            return name
        candidate = module + "." + name
        if candidate in self.classes:
            return candidate
        imports = self.modules[module].imports
        first, dot, rest = name.partition(".")
        if first in imports:
            expanded = imports[first] + (dot + rest if dot else "")
            if expanded in self.classes:
                return expanded
        if name.startswith("cw.") and name in self.classes:
            return name
        matches = self.simple_classes.get(name, [])
        return matches[0] if len(matches) == 1 else None

    def external_type(self, name: str) -> Optional[str]:
        exact = {
            "pygame.Rect": "::cw::lib::Rect", "wx.Rect": "::cw::lib::Rect",
            "pygame.Color": "::cw::lib::Color", "wx.Colour": "::cw::lib::Color",
            "datetime.datetime": "::cw::lib::DateTime", "datetime.date": "::cw::lib::DateTime",
            "pathlib.Path": "::cw::lib::Path", "os.PathLike": "::cw::lib::Path",
            "sqlite3.Row": "::cw::lib::SqlRow",
        }
        if name in exact:
            return exact[name]
        pointer_prefixes = (
            ("pygame.Surface", "Surface"), ("pygame.surface.Surface", "Surface"),
            ("pygame.sprite.Sprite", "Sprite"), ("pygame.sprite.Group", "SpriteGroup"),
            ("pygame.font.Font", "Font"), ("pygame.mixer.Sound", "Sound"),
            ("PIL.Image.Image", "Image"), ("wx.Bitmap", "Bitmap"), ("wx.Image", "Image"),
            ("wx.Dialog", "Dialog"), ("wx.Frame", "Frame"), ("wx.Panel", "Panel"),
            ("wx.Window", "Window"), ("wx.Control", "Control"), ("wx.Event", "Event"),
            ("wx.DC", "DeviceContext"), ("wx.Timer", "Timer"), ("wx.Sizer", "Sizer"),
            ("wx.Menu", "Menu"), ("sqlite3.Connection", "SqlConnection"),
            ("sqlite3.Cursor", "SqlCursor"), ("threading.Thread", "Thread"),
            ("threading.Condition", "Condition"), ("threading.Lock", "Mutex"),
        )
        for external, replacement in pointer_prefixes:
            if name == external or name.startswith(external + "["):
                return "std::shared_ptr<::cw::lib::{}>".format(replacement)
        if name.startswith("wx."):
            return "std::shared_ptr<::cw::lib::Window>"
        if name.startswith("pygame.") or name.startswith("PIL."):
            return "std::shared_ptr<::cw::lib::Object>"
        return None

    def annotation(self, node: Optional[ast.AST], module: str, for_return: bool = False) -> str:
        if node is None:
            return "::cw::lib::Value"
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            try:
                return self.annotation(ast.parse(node.value, mode="eval").body, module, for_return)
            except (SyntaxError, ValueError):
                return "::cw::lib::Value"
        if isinstance(node, ast.Constant) and node.value is None:
            return "void" if for_return else "::cw::lib::Value"
        name = dotted(node)
        primitives = {
            "None": "void" if for_return else "::cw::lib::Value",
            "NoneType": "void" if for_return else "::cw::lib::Value",
            "bool": "bool", "int": "::cw::lib::Integer", "float": "::cw::lib::Real",
            "str": "::cw::lib::String", "bytes": "::cw::lib::Bytes",
            "bytearray": "::cw::lib::Bytes", "Any": "::cw::lib::Value",
            "typing.Any": "::cw::lib::Value", "object": "::cw::lib::Value",
            "NoReturn": "void", "typing.NoReturn": "void",
        }
        if name in primitives:
            return primitives[name]
        if isinstance(node, ast.Subscript):
            root = dotted(node.value).split(".")[-1]
            slice_node = node.slice
            if isinstance(slice_node, ast.Index):
                slice_node = slice_node.value
            items = list(slice_node.elts) if isinstance(slice_node, (ast.Tuple, ast.List)) else [slice_node]
            if root == "Optional" and items:
                inner = self.annotation(items[0], module)
                return "std::optional<{}>".format(inner)
            if root == "Union":
                non_none = [item for item in items if dotted(item) not in ("None", "NoneType") and not (isinstance(item, ast.Constant) and item.value is None)]
                if len(non_none) == 1 and len(non_none) != len(items):
                    return "std::optional<{}>".format(self.annotation(non_none[0], module))
                return "::cw::lib::Value"
            if root in ("List", "Sequence", "Iterable", "Iterator", "Collection", "Generator"):
                inner = self.annotation(items[0], module) if items else "::cw::lib::Value"
                return "std::vector<{}>".format(inner)
            if root in ("Set", "FrozenSet"):
                inner = self.annotation(items[0], module) if items else "::cw::lib::Value"
                if inner == "::cw::lib::Value":
                    return "::cw::lib::ValueSet"
                return "std::set<{}>".format(inner)
            if root in ("Dict", "Mapping", "MutableMapping", "DefaultDict"):
                if len(items) >= 2:
                    key = self.annotation(items[0], module)
                    value = self.annotation(items[1], module)
                    if key == "::cw::lib::Value":
                        return "::cw::lib::ValueMap"
                    return "std::map<{}, {}>".format(key, value)
                return "::cw::lib::ValueMap"
            if root in ("Tuple",):
                if len(items) == 2 and isinstance(items[1], ast.Constant) and items[1].value is Ellipsis:
                    return "std::vector<{}>".format(self.annotation(items[0], module))
                return "std::tuple<{}>".format(", ".join(self.annotation(item, module) for item in items))
            if root in ("Callable", "Awaitable", "Coroutine"):
                return "::cw::lib::Callback"
            if root in ("Type", "ClassVar") and items:
                return self.annotation(items[-1], module)
            if root in ("ReferenceType",):
                if items:
                    inner = self.annotation(items[0], module)
                    match = re.fullmatch(r"std::shared_ptr<(.+)>", inner)
                    return "std::weak_ptr<{}>".format(match.group(1)) if match else "::cw::lib::WeakObjectPtr"
                return "::cw::lib::WeakObjectPtr"
            # 未知のジェネリックの別名は、不正な C++ テンプレートではなく動的な値として扱う。
            return "::cw::lib::Value"
        external = self.external_type(name)
        if external:
            return external
        class_key = self.resolve_class_key(name, module)
        if class_key:
            return "std::shared_ptr<{}>".format(self.class_cpp_name(class_key))
        return "::cw::lib::Value"

    def inferred(self, node: Optional[ast.AST], module: str, parameter_types: Optional[Dict[str, str]] = None) -> str:
        if node is None:
            return "::cw::lib::Value"
        if isinstance(node, ast.Constant):
            if isinstance(node.value, bool): return "bool"
            if isinstance(node.value, int): return "::cw::lib::Integer"
            if isinstance(node.value, float): return "::cw::lib::Real"
            if isinstance(node.value, str): return "::cw::lib::String"
            if isinstance(node.value, bytes): return "::cw::lib::Bytes"
            return "::cw::lib::Value"
        if isinstance(node, ast.Name):
            if parameter_types and node.id in parameter_types:
                return parameter_types[node.id]
            if node.id in ("True", "False"): return "bool"
            return "::cw::lib::Value"
        if isinstance(node, ast.List):
            inner = self.merge_types([self.inferred(item, module, parameter_types) for item in node.elts])
            return "std::vector<{}>".format(inner)
        if isinstance(node, ast.Set):
            inner = self.merge_types([self.inferred(item, module, parameter_types) for item in node.elts])
            return "::cw::lib::ValueSet" if inner == "::cw::lib::Value" else "std::set<{}>".format(inner)
        if isinstance(node, ast.Dict):
            keys = self.merge_types([self.inferred(item, module, parameter_types) for item in node.keys if item])
            values = self.merge_types([self.inferred(item, module, parameter_types) for item in node.values])
            if keys == "::cw::lib::Value": return "::cw::lib::ValueMap"
            return "std::map<{}, {}>".format(keys, values)
        if isinstance(node, ast.Tuple):
            return "std::tuple<{}>".format(", ".join(self.inferred(item, module, parameter_types) for item in node.elts))
        if isinstance(node, (ast.Compare, ast.BoolOp)):
            return "bool"
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Not):
            return "bool"
        if isinstance(node, ast.BinOp):
            left = self.inferred(node.left, module, parameter_types)
            right = self.inferred(node.right, module, parameter_types)
            return self.merge_types([left, right])
        if isinstance(node, ast.Call):
            call_name = dotted(node.func)
            tail = call_name.split(".")[-1]
            if tail in ("bool", "getbool", "isfile", "isdir", "exists", "startswith", "endswith"):
                return "bool"
            if tail in ("int", "len", "getint", "find", "index", "count"):
                return "::cw::lib::Integer"
            if tail in ("float", "getfloat"):
                return "::cw::lib::Real"
            if tail in ("str", "gettext", "join", "basename", "dirname", "abspath", "normpath"):
                return "::cw::lib::String"
            if tail in ("bytes", "bytearray"):
                return "::cw::lib::Bytes"
            if tail in ("list", "sorted"):
                return "::cw::lib::ValueList"
            if tail == "dict": return "::cw::lib::ValueMap"
            if tail == "set": return "::cw::lib::ValueSet"
            class_key = self.resolve_class_key(call_name, module)
            if class_key:
                return "std::shared_ptr<{}>".format(self.class_cpp_name(class_key))
        return "::cw::lib::Value"

    @staticmethod
    def merge_types(types: Iterable[str]) -> str:
        unique = {item for item in types if item}
        if not unique:
            return "::cw::lib::Value"
        concrete = unique - {"::cw::lib::Value"}
        if len(concrete) == 1:
            return next(iter(concrete))
        if concrete == {"::cw::lib::Integer", "::cw::lib::Real"}:
            return "::cw::lib::Real"
        return "::cw::lib::Value"


def parameter_list(function: FunctionInfo, resolver: TypeResolver) -> Tuple[List[Tuple[str, str]], Dict[str, str]]:
    args = getattr(function.node, "args")
    positional = list(getattr(args, "posonlyargs", [])) + list(args.args)
    defaults = [None] * (len(positional) - len(args.defaults)) + list(args.defaults)
    result: List[Tuple[str, str]] = []
    types_by_original: Dict[str, str] = {}
    for index, (argument, default) in enumerate(zip(positional, defaults)):
        if index == 0 and argument.arg in ("self", "cls") and function.class_key:
            continue
        cpp_type = resolver.annotation(argument.annotation, function.module) if argument.annotation else resolver.inferred(default, function.module)
        if cpp_type == "void": cpp_type = "::cw::lib::Value"
        types_by_original[argument.arg] = cpp_type
        result.append((cpp_type, "arg_" + cpp_identifier(argument.arg)))
    if args.vararg:
        result.append(("const ::cw::lib::Arguments&", "arg_" + cpp_identifier(args.vararg.arg)))
    for argument, default in zip(args.kwonlyargs, args.kw_defaults):
        cpp_type = resolver.annotation(argument.annotation, function.module) if argument.annotation else resolver.inferred(default, function.module)
        if cpp_type == "void": cpp_type = "::cw::lib::Value"
        types_by_original[argument.arg] = cpp_type
        result.append((cpp_type, "arg_" + cpp_identifier(argument.arg)))
    if args.kwarg:
        result.append(("const ::cw::lib::KeywordArguments&", "arg_" + cpp_identifier(args.kwarg.arg)))
    return result, types_by_original


def unique_function_names(functions: List[FunctionInfo]) -> None:
    seen: Dict[str, int] = defaultdict(int)
    for function in sorted(functions, key=lambda item: item.line):
        base = function.generated_name
        seen[base] += 1
        if seen[base] > 1:
            function.generated_name = "{}_{}".format(base, seen[base])


def resolve_base(node: ast.AST, info: ClassInfo, resolver: TypeResolver) -> Tuple[str, Optional[str]]:
    name = dotted(node)
    if name in ("", "object", "abc.ABC", "ABC"):
        return "::cw::lib::Object", None
    key = resolver.resolve_class_key(name, info.module)
    if key and key != info.key:
        return resolver.class_cpp_name(key), key
    external = {
        "threading.Thread": "::cw::lib::Thread",
        "pygame.sprite.Sprite": "::cw::lib::Sprite",
        "pygame.sprite.DirtySprite": "::cw::lib::Sprite",
        "wx.Dialog": "::cw::lib::Dialog",
        "wx.Frame": "::cw::lib::Frame",
        "wx.Panel": "::cw::lib::Panel",
        "wx.Window": "::cw::lib::Window",
        "wx.Control": "::cw::lib::Control",
    }
    first, dot, rest = name.partition(".")
    if first in resolver.modules[info.module].imports:
        name = resolver.modules[info.module].imports[first] + (dot + rest if dot else "")
    return external.get(name, "::cw::lib::Object"), None


def topological_classes(classes: Dict[str, ClassInfo], resolver: TypeResolver) -> Tuple[List[ClassInfo], Dict[str, List[str]]]:
    bases: Dict[str, List[str]] = {}
    deps: Dict[str, Set[str]] = {}
    for key, info in classes.items():
        resolved: List[str] = []
        class_deps: Set[str] = set()
        for node in info.base_nodes:
            base_cpp, dep = resolve_base(node, info, resolver)
            if base_cpp not in resolved:
                resolved.append(base_cpp)
            if dep:
                class_deps.add(dep)
        if not resolved:
            resolved = ["::cw::lib::Object"]
        # 実際の Python の基底を解決できたら、Object を重複させない。
        if len(resolved) > 1 and "::cw::lib::Object" in resolved:
            resolved.remove("::cw::lib::Object")
        bases[key] = resolved
        deps[key] = class_deps

    ordered: List[ClassInfo] = []
    remaining = set(classes)
    while remaining:
        ready = sorted(key for key in remaining if not (deps[key] & remaining))
        if not ready:
            # 念のため循環を打ち切る。Python の継承自体は循環しない。
            ready = [sorted(remaining)[0]]
            bases[ready[0]] = ["::cw::lib::Object"]
        for key in ready:
            ordered.append(classes[key])
            remaining.remove(key)
    return ordered, bases


def field_type(item: MemberInfo, module: str, resolver: TypeResolver) -> str:
    if item.annotation:
        value = resolver.annotation(item.annotation, module)
        return "::cw::lib::Value" if value == "void" else value
    return resolver.merge_types(resolver.inferred(value, module) for value in item.values)


def return_type(function: FunctionInfo, resolver: TypeResolver) -> str:
    annotation = getattr(function.node, "returns", None)
    return resolver.annotation(annotation, function.module, for_return=True) if annotation else "::cw::lib::Value"


def emit_namespace_open(lines: List[str], module: str) -> None:
    lines.append("namespace {} {{".format("::".join(namespace_for(module))))


def module_relative_path(module: ModuleInfo, suffix: str) -> Path:
    """Return the C++ path below src/cw for one Python module."""
    parts = module.name.split(".")[1:]
    if module.path.name == "__init__.py":
        if not parts:
            return Path("cw" + suffix)
        return Path(*parts) / (parts[-1] + suffix)
    return Path(*parts).with_suffix(suffix)


def module_include(module: ModuleInfo) -> str:
    return "cw/" + module_relative_path(module, ".hpp").as_posix()


def emit_forward_header(classes: Dict[str, ClassInfo]) -> str:
    lines = [
        "#pragma once", "",
        "// Generated by src/tools/generate_cw_stubs.py. Do not edit by hand.", "",
    ]
    grouped_classes: Dict[str, List[ClassInfo]] = defaultdict(list)
    for info in classes.values():
        grouped_classes[info.module].append(info)
    for module_name_key in sorted(grouped_classes):
        emit_namespace_open(lines, module_name_key)
        for info in sorted(grouped_classes[module_name_key], key=lambda item: item.cpp_name):
            lines.append("class {};".format(info.cpp_name))
        lines.extend(["}", ""])
    return "\n".join(lines)


def module_class_groups(ordered: List[ClassInfo]) -> Dict[str, List[ClassInfo]]:
    result: Dict[str, List[ClassInfo]] = defaultdict(list)
    for info in ordered:
        result[info.module].append(info)
    return result


def emit_module_header(module: ModuleInfo, class_infos: List[ClassInfo], classes: Dict[str, ClassInfo],
                       modules: Dict[str, ModuleInfo], resolver: TypeResolver,
                       bases: Dict[str, List[str]]) -> str:
    source_relative = module.path.as_posix()
    dependency_modules: Set[str] = set()
    for info in class_infos:
        for node in info.base_nodes:
            _, dependency = resolve_base(node, info, resolver)
            if dependency and classes[dependency].module != module.name:
                dependency_modules.add(classes[dependency].module)

    lines = [
        "#pragma once", "",
        "// Generated declaration skeleton from {}.".format(source_relative),
        "// Function bodies are intentionally placeholders for the staged port.",
        "#include \"cw/lib/compat.hpp\"",
        "#include \"cw/fwd.hpp\"",
    ]
    for dependency_module in sorted(dependency_modules):
        lines.append("#include \"{}\"".format(module_include(modules[dependency_module])))
    lines.append("")

    emit_namespace_open(lines, module.name)
    for info in class_infos:
        inheritance = ", ".join("public " + item for item in bases[info.key])
        lines.append("class {} : {} {{".format(info.cpp_name, inheritance))
        lines.append("public:")
        lines.append("    {}() = default;".format(info.cpp_name))
        lines.append("    virtual ~{}() = default;".format(info.cpp_name))
        for item in sorted(info.class_variables.values(), key=lambda value: value.original_name):
            lines.append("    inline static {} {}{{}};".format(field_type(item, info.module, resolver), item.cpp_name))
        for item in sorted(info.members.values(), key=lambda value: value.original_name):
            lines.append("    {} {}{{}};".format(field_type(item, info.module, resolver), item.cpp_name))
        for function in info.methods:
            params, _ = parameter_list(function, resolver)
            args = ", ".join("{} {}".format(t, n) for t, n in params)
            static = "static " if function.is_static else ""
            lines.append("    {}{} {}({});".format(static, return_type(function, resolver), function.generated_name, args))
        lines.extend(["};", ""])

    for item in sorted(module.globals.values(), key=lambda value: value.original_name):
        member = MemberInfo(item.original_name, item.cpp_name, item.annotation, item.values)
        lines.append("extern {} {};".format(field_type(member, module.name, resolver), item.cpp_name))
    if module.globals and module.functions:
        lines.append("")
    for function in module.functions:
        params, _ = parameter_list(function, resolver)
        args = ", ".join("{} {}".format(t, n) for t, n in params)
        lines.append("{} {}({});".format(return_type(function, resolver), function.generated_name, args))
    lines.extend(["}", ""])
    return "\n".join(lines)


def function_body(return_value: str, params: List[Tuple[str, str]]) -> List[str]:
    lines = ["{"]
    for _, name in params:
        lines.append("    (void){};".format(name))
    if return_value != "void":
        lines.append("    return {};")
    lines.append("}")
    return lines


def emit_module_source(module: ModuleInfo, class_infos: List[ClassInfo], resolver: TypeResolver) -> str:
    lines = [
        "// Generated by src/tools/generate_cw_stubs.py. Do not edit by hand.",
        "// 絶対に一系統にする、２系統は死罪。",
        "// Keep exactly one UI implementation; a second implementation is strictly forbidden.",
        "#include \"{}\"".format(module_include(module)), "",
    ]
    emit_namespace_open(lines, module.name)
    for info in class_infos:
        for function in info.methods:
            params, _ = parameter_list(function, resolver)
            args = ", ".join("{} {}".format(t, n) for t, n in params)
            result = return_type(function, resolver)
            prefix = "{} {}::{}({})".format(result, info.cpp_name, function.generated_name, args)
            lines.append(prefix)
            lines.extend(function_body(result, params))
            lines.append("")
    for item in sorted(module.globals.values(), key=lambda value: value.original_name):
        member = MemberInfo(item.original_name, item.cpp_name, item.annotation, item.values)
        lines.append("{} {}{{}};".format(field_type(member, module.name, resolver), item.cpp_name))
    if module.globals:
        lines.append("")
    for function in module.functions:
        params, _ = parameter_list(function, resolver)
        args = ", ".join("{} {}".format(t, n) for t, n in params)
        result = return_type(function, resolver)
        lines.append("{} {}({})".format(result, function.generated_name, args))
        lines.extend(function_body(result, params))
        lines.append("")
    lines.extend(["}", ""])
    return "\n".join(lines)


def emit_all_header(modules: Dict[str, ModuleInfo]) -> str:
    lines = [
        "#pragma once", "",
        "// Convenience header for the complete declaration-first CardWirth port.",
    ]
    for module_name_key in sorted(modules):
        lines.append("#include \"{}\"".format(module_include(modules[module_name_key])))
    lines.append("")
    return "\n".join(lines)


GENERATED_FILE_MARKERS = (
    "Generated by src/tools/generate_cw_stubs.py",
    "Generated declaration skeleton from",
)


def write_module_skeleton(path: Path, content: str) -> bool:
    """Write a generated stub without overwriting a file already being ported."""
    if path.exists():
        existing_head = path.read_text(encoding="utf-8", errors="replace")[:512]
        if not any(marker in existing_head for marker in GENERATED_FILE_MARKERS):
            return False
    path.write_text(content + "\n", encoding="utf-8")
    return True


def inventory_data(modules: Dict[str, ModuleInfo], classes: Dict[str, ClassInfo], resolver: TypeResolver,
                   parse_errors: List[dict], raw_counts: Dict[str, int]) -> dict:
    def function_record(item: FunctionInfo) -> dict:
        parameters, _ = parameter_list(item, resolver)
        return {
            "python": item.original_name,
            "cpp": item.generated_name,
            "line": item.line,
            "return_type": return_type(item, resolver),
            "parameters": [{"type": parameter_type, "name": name} for parameter_type, name in parameters],
        }

    module_records = []
    for module_name_key in sorted(modules):
        module = modules[module_name_key]
        module_classes = sorted((item for item in classes.values() if item.module == module.name), key=lambda item: item.line)
        module_records.append({
            "module": module.name,
            "source": str(module.path),
            "cpp_header": module_relative_path(module, ".hpp").as_posix(),
            "cpp_source": module_relative_path(module, ".cpp").as_posix(),
            "sha256": hashlib.sha256(module.path.read_bytes()).hexdigest(),
            "imports": dict(sorted(module.imports.items())),
            "globals": [{
                "python": item.original_name,
                "cpp": item.cpp_name,
                "type": field_type(MemberInfo(item.original_name, item.cpp_name, item.annotation, item.values), module.name, resolver),
            } for item in module.globals.values()],
            "functions": [function_record(item) for item in module.functions],
            "lambdas": module.lambda_count,
            "classes": [{
                "python": item.original_name,
                "qualified_python": item.key,
                "cpp": item.cpp_name,
                "line": item.line,
                "members": [{
                    "python": member.original_name,
                    "cpp": member.cpp_name,
                    "type": field_type(member, item.module, resolver),
                } for member in item.members.values()],
                "class_variables": [{
                    "python": member.original_name,
                    "cpp": member.cpp_name,
                    "type": field_type(member, item.module, resolver),
                } for member in item.class_variables.values()],
                "methods": [function_record(method) for method in item.methods],
            } for item in module_classes],
        })
    return {
        "generator": "src/tools/generate_cw_stubs.py",
        "summary": {
            "modules": len(modules),
            "classes": len(classes),
            "methods": sum(len(item.methods) for item in classes.values()),
            "module_and_nested_functions": sum(len(item.functions) for item in modules.values()),
            "lambdas": sum(item.lambda_count for item in modules.values()),
            "instance_members": sum(len(item.members) for item in classes.values()),
            "class_variables": sum(len(item.class_variables) for item in classes.values()),
            "module_globals": sum(len(item.globals) for item in modules.values()),
            "parse_errors": len(parse_errors),
        },
        "coverage": {
            "python_class_definitions": raw_counts["classes"],
            "generated_unique_classes": len(classes),
            "python_function_definitions": raw_counts["functions"],
            "generated_function_declarations": sum(len(item.methods) for item in classes.values()) +
                                               sum(len(item.functions) for item in modules.values()) -
                                               sum(item.lambda_count for item in modules.values()),
            "python_lambdas": raw_counts["lambdas"],
            "generated_lambda_declarations": sum(item.lambda_count for item in modules.values()),
            "note": "Duplicate Python classes in platform conditionals are merged into one C++ type.",
        },
        "parse_errors": parse_errors,
        "modules": module_records,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--python-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    options = parser.parse_args()

    python_root = options.python_root.resolve()
    output = options.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    modules: Dict[str, ModuleInfo] = {}
    classes: Dict[str, ClassInfo] = {}
    parse_errors: List[dict] = []

    parsed: Dict[str, ast.Module] = {}
    for path in sorted(python_root.rglob("*.py")):
        name = module_name(python_root, path)
        module = ModuleInfo(name, path)
        modules[name] = module
        try:
            tree = ast.parse(path.read_text(encoding="utf-8-sig"), filename=str(path))
        except (SyntaxError, UnicodeError) as error:
            parse_errors.append({"source": str(path), "error": str(error)})
            continue
        parsed[name] = tree
        for statement in tree.body:
            if isinstance(statement, ast.Import):
                for alias in statement.names:
                    module.imports[alias.asname or alias.name.split(".")[0]] = alias.name
            elif isinstance(statement, ast.ImportFrom):
                base = resolve_relative_import(name, statement.level, statement.module)
                for alias in statement.names:
                    if alias.name != "*":
                        module.imports[alias.asname or alias.name] = base + "." + alias.name
        collect_module_globals(tree, module)
        SourceCollector(module, classes).visit(tree)

    resolver = TypeResolver(modules, classes)
    ordered, bases = topological_classes(classes, resolver)
    for info in classes.values():
        unique_function_names(info.methods)
    for module in modules.values():
        unique_function_names(module.functions)
    grouped_classes = module_class_groups(ordered)
    raw_counts = {
        "classes": sum(sum(isinstance(node, ast.ClassDef) for node in ast.walk(tree)) for tree in parsed.values()),
        "functions": sum(sum(isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) for node in ast.walk(tree)) for tree in parsed.values()),
        "lambdas": sum(sum(isinstance(node, ast.Lambda) for node in ast.walk(tree)) for tree in parsed.values()),
    }
    inventory = inventory_data(modules, classes, resolver, parse_errors, raw_counts)

    generated_files = ["fwd.hpp", "all.hpp", "inventory.json"]
    (output / "fwd.hpp").write_text(emit_forward_header(classes) + "\n", encoding="utf-8")
    (output / "all.hpp").write_text(emit_all_header(modules) + "\n", encoding="utf-8")
    for module_name_key in sorted(modules):
        module = modules[module_name_key]
        header_relative = module_relative_path(module, ".hpp")
        source_relative = module_relative_path(module, ".cpp")
        header_path = output / header_relative
        source_path = output / source_relative
        header_path.parent.mkdir(parents=True, exist_ok=True)
        source_path.parent.mkdir(parents=True, exist_ok=True)
        write_module_skeleton(
            header_path,
            emit_module_header(module, grouped_classes[module.name], classes, modules, resolver, bases),
        )
        write_module_skeleton(
            source_path,
            emit_module_source(module, grouped_classes[module.name], resolver),
        )
        generated_files.extend([header_relative.as_posix(), source_relative.as_posix()])
    inventory["generated_files"] = generated_files
    (output / "inventory.json").write_text(json.dumps(inventory, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(inventory["summary"], ensure_ascii=False, indent=2))
    return 1 if parse_errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
