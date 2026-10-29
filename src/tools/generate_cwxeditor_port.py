#!/usr/bin/env python3
"""Generate one compile-tested C++ .hpp/.cpp pair per CWXEditor D module.

The DMD 2.097 JSON AST is the declaration source of truth.  Every selected D
class, interface, struct, enum, field, method, constructor, template, alias,
and module function/variable is represented in the generated skeleton.  D
function bodies are intentionally not translated in this phase.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


CPP_KEYWORDS = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand",
    "bitor", "bool", "break", "case", "catch", "char", "char8_t",
    "char16_t", "char32_t", "class", "compl", "concept", "const",
    "consteval", "constexpr", "constinit", "const_cast", "continue",
    "co_await", "co_return", "co_yield", "decltype", "default", "delete",
    "do", "double", "dynamic_cast", "else", "enum", "explicit", "export",
    "extern", "false", "float", "for", "friend", "goto", "if", "inline",
    "int", "long", "mutable", "namespace", "new", "noexcept", "not",
    "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected",
    "public", "register", "reinterpret_cast", "requires", "return", "short",
    "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
    "switch", "template", "this", "thread_local", "throw", "true", "try",
    "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual",
    "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
}

TYPE_KINDS = {"class", "interface", "struct", "enum"}
FUNCTION_KINDS = {
    "function", "generated function", "constructor", "destructor",
    "static constructor", "shared static constructor", "shared static destructor",
}
EMITTED_KINDS = TYPE_KINDS | FUNCTION_KINDS | {"variable", "alias", "template", "enum member"}


def cpp_name(value: str, fallback: str = "unnamed") -> str:
    value = re.sub(r"[^A-Za-z0-9_]", "_", value or fallback)
    if not value:
        value = fallback
    if value[0].isdigit():
        value = "n_" + value
    if value in CPP_KEYWORDS:
        value += "_"
    return value


def cxx_string(value: str) -> str:
    return json.dumps(value or "unknown", ensure_ascii=True)


def type_key(node: dict[str, Any]) -> str:
    return str(node.get("deco") or node.get("originalType") or node.get("type") or "unknown")


def protection(node: dict[str, Any]) -> str:
    value = node.get("protection", "public")
    if value in {"private", "protected", "public"}:
        return value
    return "public"


def parameters(node: dict[str, Any]) -> list[dict[str, Any]]:
    return [item for item in node.get("parameters", []) if isinstance(item, dict)]


def function_key(node: dict[str, Any]) -> tuple[Any, ...]:
    return (
        node.get("kind"), node.get("name"),
        tuple(type_key(item) for item in parameters(node)),
        tuple(node.get("storageClass", [])), node.get("deco"),
    )


def unique_members(members: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    # ここでコンパイラーのシンボルをまとめない。
    # 一覧は宣言数の一対一対応を保ち、C++ の
    # 名前衝突は明示的な生成接尾辞で処理する。
    return [member for member in members if member.get("kind", "") in EMITTED_KINDS]


def param_decls(node: dict[str, Any]) -> str:
    used: collections.Counter[str] = collections.Counter()
    parts: list[str] = []
    for index, param in enumerate(parameters(node)):
        base = cpp_name(str(param.get("name") or f"arg{index}"), f"arg{index}")
        used[base] += 1
        name = base if used[base] == 1 else f"{base}_{used[base]}"
        parts.append(f"::cwxeditor::port::Type<{cxx_string(type_key(param))}> {name}")
    return ", ".join(parts)


def function_cpp_name(node: dict[str, Any], ordinal: int) -> str:
    kind = node.get("kind")
    if kind in {"static constructor", "shared static constructor"}:
        return f"d_static_constructor_{ordinal}"
    if kind == "shared static destructor":
        return f"d_static_destructor_{ordinal}"
    return cpp_name(str(node.get("name") or f"function_{ordinal}"), f"function_{ordinal}")


def is_static(node: dict[str, Any]) -> bool:
    return "static" in node.get("storageClass", []) or "static " in str(node.get("kind", ""))


def is_const_method(node: dict[str, Any]) -> bool:
    storage = set(node.get("storageClass", []))
    deco = str(node.get("deco", ""))
    return bool(storage & {"const", "immutable"}) or deco.startswith(("xF", "yF"))


@dataclass
class Definition:
    text: str


class ModuleRenderer:
    def __init__(self, module: dict[str, Any], layer: str, relative: Path) -> None:
        self.module = module
        self.layer = layer
        self.relative = relative
        # C++ の名前空間は元の D モジュール名に合わせる。出力層の
        # core/gui はファイル配置の区分であり、名前空間ではない。
        self.namespace_parts = ["cwxeditor"] + [cpp_name(part) for part in module["name"].split(".")]
        self.namespace = "::".join(self.namespace_parts)
        self.definitions: list[Definition] = []
        self.emitted: collections.Counter[str] = collections.Counter()

    def render_function_decl(
        self,
        node: dict[str, Any],
        owner_path: list[str],
        ordinal: int,
        indent: str,
        inline: bool = False,
        name_override: str | None = None,
    ) -> list[str]:
        kind = node.get("kind")
        params = param_decls(node)
        owner_name = owner_path[-1] if owner_path else ""
        if kind == "constructor":
            name = owner_name
            declaration = f"{name}({params})"
            definition_head = f"{self.namespace}::{'::'.join(owner_path)}::{name}({params})"
        elif kind == "destructor":
            name = owner_name
            declaration = f"~{name}()"
            definition_head = f"{self.namespace}::{'::'.join(owner_path)}::~{name}()"
        else:
            name = name_override or function_cpp_name(node, ordinal)
            signature = type_key(node)
            prefix = "static " if owner_path and is_static(node) else ""
            return_type = f"::cwxeditor::port::Return<{cxx_string(signature)}>"
            declaration = f"{prefix}{return_type} {name}({params})"
            const_suffix = " const" if owner_path and not is_static(node) and is_const_method(node) else ""
            declaration += const_suffix
            qualified = f"{self.namespace}::"
            if owner_path:
                qualified += "::".join(owner_path) + "::"
            definition_head = f"{return_type} {qualified}{name}({params}){const_suffix}"

        self.emitted[str(kind)] += 1
        if inline:
            if kind in {"constructor", "destructor"}:
                return [indent + declaration + " {}"]
            return [indent + declaration + " { return {}; }"]
        self.definitions.append(Definition(definition_head + (" {}" if kind in {"constructor", "destructor"} else " { return {}; }")))
        return [indent + declaration + ";"]

    def render_template(
        self,
        node: dict[str, Any],
        owner_path: list[str],
        indent: str,
        ordinal: int,
        name_override: str | None = None,
    ) -> list[str]:
        members = unique_members(node.get("members", []))
        same = [item for item in members if item.get("name") == node.get("name")]
        self.emitted["template"] += 1
        if same and same[0].get("kind") in FUNCTION_KINDS:
            lines = [indent + "template <typename... DTemplateArgs>"]
            lines.extend(self.render_function_decl(same[0], owner_path, ordinal, indent, inline=True, name_override=name_override))
            return lines
        if same and same[0].get("kind") in TYPE_KINDS:
            lines = [indent + "template <typename... DTemplateArgs>"]
            type_node = dict(same[0])
            if name_override:
                type_node["name"] = name_override
            lines.extend(self.render_type(type_node, owner_path, indent, inline_context=True))
            return lines
        name = name_override or cpp_name(str(node.get("name") or f"template_{ordinal}"), f"template_{ordinal}")
        lines = [indent + "template <typename... DTemplateArgs>", indent + f"struct {name} {{", indent + "public:"]
        function_counts: collections.Counter[tuple[Any, ...]] = collections.Counter()
        occupied = {cpp_name(str(item.get("name") or "")) for item in members if item.get("kind") in FUNCTION_KINDS}
        for member_index, member in enumerate(members, 1):
            kind = member.get("kind")
            if kind in FUNCTION_KINDS:
                key = function_key(member)
                function_counts[key] += 1
                override = None
                if function_cpp_name(member, member_index) == name:
                    override = function_cpp_name(member, member_index) + "_function"
                if function_counts[key] > 1:
                    override = function_cpp_name(member, member_index) + f"_duplicate{function_counts[key]}"
                lines.extend(self.render_function_decl(member, owner_path + [name], member_index, indent + "    ", inline=True, name_override=override))
            elif kind == "variable":
                field = cpp_name(str(member.get("name") or f"field_{member_index}"), f"field_{member_index}")
                if field in occupied or field == name:
                    field += "_field"
                lines.append(indent + f"    ::cwxeditor::port::Type<{cxx_string(type_key(member))}> {field}{{}};")
                self.emitted["variable"] += 1
            elif kind == "alias":
                alias = cpp_name(str(member.get("name") or f"alias_{member_index}"), f"alias_{member_index}")
                if alias in occupied or alias == name:
                    alias += "_alias"
                lines.append(indent + f"    using {alias} = ::cwxeditor::port::Type<{cxx_string(type_key(member))}>;")
                self.emitted["alias"] += 1
            elif kind in TYPE_KINDS:
                if cpp_name(str(member.get("name") or "anonymous_type"), "anonymous_type") == name:
                    member = dict(member)
                    member["name"] = name + "_type"
                lines.extend(self.render_type(member, owner_path + [name], indent + "    ", inline_context=True))
            elif kind == "template":
                lines.extend(self.render_template(member, owner_path + [name], indent + "    ", member_index))
        lines.append(indent + "};")
        return lines

    def render_enum(self, node: dict[str, Any], indent: str) -> list[str]:
        name = cpp_name(str(node.get("name") or "anonymous_enum"), "anonymous_enum")
        members = [item for item in unique_members(node.get("members", [])) if item.get("kind") == "enum member"]
        used: collections.Counter[str] = collections.Counter()
        values: list[str] = []
        for index, member in enumerate(members):
            value = cpp_name(str(member.get("name") or f"value_{index}"), f"value_{index}")
            used[value] += 1
            if used[value] > 1:
                value += f"_{used[value]}"
            values.append(value)
            self.emitted["enum member"] += 1
        if not values:
            values = ["Unspecified"]
        self.emitted["enum"] += 1
        return [indent + f"enum class {name} {{", *(indent + "    " + value + "," for value in values), indent + "};"]

    def render_type(self, node: dict[str, Any], owner_path: list[str], indent: str, inline_context: bool = False) -> list[str]:
        kind = str(node.get("kind"))
        if kind == "enum":
            return self.render_enum(node, indent)
        name = cpp_name(str(node.get("name") or "anonymous_type"), "anonymous_type")
        class_word = "struct" if kind == "struct" else "class"
        bases = []
        if node.get("base"):
            bases.append(str(node["base"]))
        bases.extend(str(value) for value in node.get("interfaces", []))
        inheritance = ""
        if bases:
            inheritance = " : " + ", ".join(
                f"public ::cwxeditor::port::Base<{cxx_string(value)}>" for value in dict.fromkeys(bases)
            )
        lines = [indent + f"{class_word} {name}{inheritance} {{"]
        self.emitted[kind] += 1
        current_access = "public" if class_word == "struct" else "private"
        lines.append(indent + current_access + ":")
        member_name_counts: collections.Counter[str] = collections.Counter()
        function_key_counts: collections.Counter[tuple[Any, ...]] = collections.Counter()
        template_name_counts: collections.Counter[str] = collections.Counter()
        type_name_counts: collections.Counter[str] = collections.Counter()
        function_names = {
            cpp_name(str(item.get("name") or ""))
            for item in node.get("members", []) if item.get("kind") in FUNCTION_KINDS
        }
        function_ordinal = 0
        type_path = owner_path + [name]
        for member in unique_members(node.get("members", [])):
            member_kind = member.get("kind")
            member_access = protection(member)
            if member_access != current_access:
                current_access = member_access
                lines.append(indent + current_access + ":")
            if member_kind in TYPE_KINDS:
                lines.append("")
                raw_type = cpp_name(str(member.get("name") or "anonymous_type"), "anonymous_type")
                type_name_counts[raw_type] += 1
                if type_name_counts[raw_type] > 1:
                    member = dict(member)
                    member["name"] = raw_type + f"_duplicate{type_name_counts[raw_type]}"
                lines.extend(self.render_type(member, type_path, indent + "    ", inline_context=inline_context))
            elif member_kind == "template":
                function_ordinal += 1
                raw_template = cpp_name(str(member.get("name") or f"template_{function_ordinal}"), f"template_{function_ordinal}")
                template_name_counts[raw_template] += 1
                override = raw_template if template_name_counts[raw_template] == 1 else raw_template + f"_overload{template_name_counts[raw_template]}"
                lines.extend(self.render_template(member, type_path, indent + "    ", function_ordinal, name_override=override))
            elif member_kind == "variable":
                raw_name = str(member.get("name") or "field")
                field_name = cpp_name(raw_name, "field")
                member_name_counts[field_name] += 1
                if member_name_counts[field_name] > 1:
                    field_name += f"_{member_name_counts[field_name]}"
                if field_name in function_names:
                    field_name += "_field"
                static = "inline static " if is_static(member) else ""
                lines.append(indent + f"    {static}::cwxeditor::port::Type<{cxx_string(type_key(member))}> {field_name}{{}};")
                self.emitted["variable"] += 1
            elif member_kind == "alias":
                alias_name = cpp_name(str(member.get("name") or "alias_type"), "alias_type")
                if alias_name in function_names:
                    alias_name += "_alias"
                lines.append(indent + f"    using {alias_name} = ::cwxeditor::port::Type<{cxx_string(type_key(member))}>;")
                self.emitted["alias"] += 1
            elif member_kind in FUNCTION_KINDS:
                function_ordinal += 1
                key = function_key(member)
                function_key_counts[key] += 1
                override = None
                if function_key_counts[key] > 1:
                    override = function_cpp_name(member, function_ordinal) + f"_duplicate{function_key_counts[key]}"
                lines.extend(self.render_function_decl(member, type_path, function_ordinal, indent + "    ", inline=inline_context, name_override=override))
        lines.append(indent + "};")
        return lines

    def render(self) -> tuple[str, str, dict[str, int]]:
        header = [
            "#pragma once", "",
            "// Generated from the DMD semantic AST. Function bodies are phase-one stubs.",
            f"// D source: {self.relative.as_posix()}",
            f"// D module: {self.module['name']}", "",
            '#include "cwxeditor_src/lib/d_compat.hpp"', "",
            f"namespace {self.namespace} {{", "",
        ]
        module_function_ordinal = 0
        module_name_counts: collections.Counter[str] = collections.Counter()
        module_function_key_counts: collections.Counter[tuple[Any, ...]] = collections.Counter()
        module_template_counts: collections.Counter[str] = collections.Counter()
        module_function_names = {
            cpp_name(str(item.get("name") or ""))
            for item in self.module.get("members", []) if item.get("kind") in FUNCTION_KINDS
        }
        for member in unique_members(self.module.get("members", [])):
            kind = member.get("kind")
            if kind in TYPE_KINDS:
                header.extend(self.render_type(member, [], ""))
                header.append("")
            elif kind == "template":
                module_function_ordinal += 1
                raw_template = cpp_name(str(member.get("name") or f"template_{module_function_ordinal}"), f"template_{module_function_ordinal}")
                module_template_counts[raw_template] += 1
                override = raw_template if module_template_counts[raw_template] == 1 else raw_template + f"_overload{module_template_counts[raw_template]}"
                header.extend(self.render_template(member, [], "", module_function_ordinal, name_override=override))
                header.append("")
            elif kind == "variable":
                name = cpp_name(str(member.get("name") or "variable"), "variable")
                module_name_counts[name] += 1
                if module_name_counts[name] > 1:
                    name += f"_{module_name_counts[name]}"
                if name in module_function_names:
                    name += "_variable"
                header.append(f"inline ::cwxeditor::port::Type<{cxx_string(type_key(member))}> {name}{{}};")
                self.emitted["variable"] += 1
            elif kind == "alias":
                name = cpp_name(str(member.get("name") or "alias_type"), "alias_type")
                if name in module_function_names:
                    name += "_alias"
                header.append(f"using {name} = ::cwxeditor::port::Type<{cxx_string(type_key(member))}>;")
                self.emitted["alias"] += 1
            elif kind == "enum member":
                name = cpp_name(str(member.get("name") or "enum_value"), "enum_value")
                value = str(member.get("value") or "0")
                if not re.fullmatch(r"-?(?:0[xX][0-9A-Fa-f]+|[0-9]+)", value):
                    value = "0"
                header.append(f"inline constexpr int {name} = {value};")
                self.emitted["enum member"] += 1
            elif kind in FUNCTION_KINDS:
                module_function_ordinal += 1
                key = function_key(member)
                module_function_key_counts[key] += 1
                override = None
                if module_function_key_counts[key] > 1:
                    override = function_cpp_name(member, module_function_ordinal) + f"_duplicate{module_function_key_counts[key]}"
                header.extend(self.render_function_decl(member, [], module_function_ordinal, "", name_override=override))
        header.extend(["", f"}}  // namespace {self.namespace}", ""])

        include_path = "cwxeditor_src/" + self.layer + "/" + self.relative.with_suffix(".hpp").as_posix()
        source = [f'#include "{include_path}"', ""]
        source.extend(definition.text for definition in self.definitions)
        source.append("")
        return "\n".join(header), "\n\n".join(source), dict(self.emitted)


def run_lines(command: list[str], cwd: Path) -> list[str]:
    result = subprocess.run(command, cwd=cwd, text=True, encoding="utf-8", errors="replace", capture_output=True, check=True)
    return [line.strip() for line in result.stdout.splitlines() if line.strip() and not line.lstrip().startswith("Warning")]


def build_ast(source_root: Path, sources: list[Path], dmd: Path, dub: Path) -> list[dict[str, Any]]:
    import_paths = run_lines([str(dub), "describe", f"--root={source_root}", "--import-paths", "--data-list"], source_root)
    string_paths = run_lines([str(dub), "describe", f"--root={source_root}", "--string-import-paths", "--data-list"], source_root)
    versions = run_lines([str(dub), "describe", f"--root={source_root}", "--data=versions", "--data-list"], source_root)
    with tempfile.TemporaryDirectory(prefix="cwxeditor_ast_") as temporary:
        ast_path = Path(temporary) / "ast.json"
        command = [str(dmd), "-o-", "-X", f"-Xf={ast_path}"]
        command.extend(f"-version={value}" for value in versions)
        command.extend("-I" + value.rstrip("/\\") for value in import_paths)
        command.extend("-J" + value.rstrip("/\\") for value in string_paths)
        command.extend(str(path) for path in sources)
        result = subprocess.run(command, cwd=source_root, text=True, encoding="utf-8", errors="replace", capture_output=True)
        if result.returncode != 0 or not ast_path.exists():
            raise RuntimeError("DMD AST generation failed:\n" + result.stdout + result.stderr)
        return json.loads(ast_path.read_text(encoding="utf-8"))


def walk_symbols(node: dict[str, Any]) -> Iterable[dict[str, Any]]:
    if node.get("kind") in EMITTED_KINDS:
        yield node
    for child in node.get("members", []):
        yield from walk_symbols(child)


def write_if_changed(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.write_text(content, encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--dmd", type=Path, default=Path(r"C:\D\dmd2.097.0\windows\bin64\dmd.exe"))
    parser.add_argument("--dub", type=Path, default=Path(r"C:\D\dmd2\windows\bin64\dub.exe"))
    args = parser.parse_args()

    source_root = args.source_root.resolve()
    output_root = args.output_root.resolve()
    source_specs: list[tuple[str, Path, Path]] = []
    core_root = source_root / "core" / "cwx"
    gui_root = source_root / "gui" / "cwx" / "editor" / "gui"
    for path in sorted(core_root.glob("*.d")):
        source_specs.append(("core", path, Path("cwx") / path.relative_to(core_root)))
    for path in sorted((gui_root / "dwt").glob("*.d")):
        source_specs.append(("gui", path, Path("cwx/editor/gui/dwt") / path.relative_to(gui_root / "dwt")))
    source_specs.append(("gui", gui_root / "sound.d", Path("cwx/editor/gui/sound.d")))
    source_specs.append(("source", source_root / "source" / "cwxeditor.d", Path("cwxeditor.d")))
    sources = [path for _, path, _ in source_specs]
    ast_modules = build_ast(source_root, sources, args.dmd, args.dub)
    by_file = {Path(module["file"]).resolve(): module for module in ast_modules}
    if len(by_file) != len(sources):
        raise RuntimeError(f"AST module count mismatch: expected {len(sources)}, got {len(by_file)}")

    inventory: dict[str, Any] = {"source_files": len(sources), "layers": {}, "files": []}
    total_source: collections.Counter[str] = collections.Counter()
    total_emitted: collections.Counter[str] = collections.Counter()
    layers = dict.fromkeys(layer for layer, _, _ in source_specs)
    for layer in layers:
        layer_source: collections.Counter[str] = collections.Counter()
        layer_emitted: collections.Counter[str] = collections.Counter()
        layer_specs = [spec for spec in source_specs if spec[0] == layer]
        for _, source_path, relative in layer_specs:
            module = by_file[source_path.resolve()]
            renderer = ModuleRenderer(module, layer, relative)
            header, source, emitted = renderer.render()
            target_base = output_root / layer / relative
            header_path = target_base.with_suffix(".hpp")
            source_path = target_base.with_suffix(".cpp")
            generated_marker = "Generated from the DMD semantic AST"
            is_concrete = (
                header_path.exists()
                and generated_marker not in header_path.read_text(encoding="utf-8")
            )
            if not is_concrete:
                write_if_changed(header_path, header)
                write_if_changed(source_path, source)
            source_counts = collections.Counter(str(item.get("kind")) for item in walk_symbols(module))
            layer_source.update(source_counts)
            layer_emitted.update(emitted)
            inventory["files"].append({
                "layer": layer,
                "d_source": source_path.relative_to(source_root).as_posix(),
                "module": module["name"],
                "hpp": target_base.with_suffix(".hpp").relative_to(output_root).as_posix(),
                "cpp": target_base.with_suffix(".cpp").relative_to(output_root).as_posix(),
                "source_symbols": dict(source_counts),
                "emitted_symbols": emitted,
            })
        inventory["layers"][layer] = {
            "files": len(layer_specs),
            "source_symbols": dict(layer_source),
            "emitted_symbols": dict(layer_emitted),
        }
        total_source.update(layer_source)
        total_emitted.update(layer_emitted)
    inventory["source_symbols"] = dict(total_source)
    inventory["emitted_symbols"] = dict(total_emitted)
    write_if_changed(output_root / "inventory.json", json.dumps(inventory, ensure_ascii=False, indent=2) + "\n")
    print(f"Generated {len(sources)} C++ header/source pairs")
    print("Source symbols:", dict(total_source))
    print("Emitted symbols:", dict(total_emitted))


if __name__ == "__main__":
    main()
