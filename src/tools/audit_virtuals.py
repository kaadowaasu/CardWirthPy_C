"""Report suspicious C++ virtual-function declarations in CardWirthPy_C headers.

This is intentionally a conservative source audit, not a C++ parser.  It finds
single-line/multiline member declarations at class scope, resolves bases by
their unqualified class name, and reports same-name members that either hide a
non-virtual base member or override a virtual member without ``override``.
Compiler warnings C4263/C4264/C4266 remain the authoritative signature check.
"""

from __future__ import annotations

import pathlib
import re
import sys
from dataclasses import dataclass, field


ROOT = pathlib.Path(__file__).resolve().parents[1] / "cw"


@dataclass
class Method:
    name: str
    params: str
    declaration: str
    virtual: bool
    override: bool
    line: int


@dataclass
class ClassInfo:
    name: str
    bases: list[str]
    path: pathlib.Path
    line: int
    methods: list[Method] = field(default_factory=list)


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def matching_brace(text: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return -1


def normalize_params(params: str) -> str:
    value = re.sub(r"\s+", " ", params.strip())
    value = re.sub(r"\s*([<>,*&:])\s*", r"\1", value)
    # 引数名と既定値は意図的に残す。厳密なシグネチャー検証は
    # MSVC が行い、このスクリプトは主に名前の隠蔽を探す。
    return value


def parse_methods(body: str, body_line: int, class_name: str) -> list[Method]:
    methods: list[Method] = []
    depth = 0
    statement_start = 0
    for index, character in enumerate(body):
        if character == "{":
            depth += 1
        elif character == "}":
            depth = max(0, depth - 1)
        elif character == ";" and depth == 0:
            declaration = body[statement_start : index + 1].strip()
            statement_start = index + 1
            if not declaration or "(" not in declaration or declaration.startswith(("using ", "typedef ")):
                continue
            declaration = re.sub(r"\s+", " ", declaration)
            match = re.search(r"(?:^|\s)(~?[A-Za-z_]\w*|operator\s*[^\s(]+)\s*\((.*)\)\s*(?:const\s*)?(?:noexcept(?:\([^)]*\))?\s*)?(?:override\s*)?(?:final\s*)?(?:=\s*0\s*)?;$", declaration)
            if not match:
                continue
            name = match.group(1).replace(" ", "")
            if name in {class_name, "~" + class_name} or name.startswith("operator"):
                continue
            methods.append(
                Method(
                    name=name,
                    params=normalize_params(match.group(2)),
                    declaration=declaration,
                    virtual=bool(re.search(r"\bvirtual\b", declaration)),
                    override=bool(re.search(r"\boverride\b", declaration)),
                    line=body_line + body[: statement_start].count("\n"),
                )
            )
    return methods


def parse_headers() -> dict[str, ClassInfo]:
    classes: dict[str, ClassInfo] = {}
    pattern = re.compile(r"\bclass\s+(\w+)\s*(?:final\s*)?(?::\s*([^\{]+))?\s*\{")
    for path in ROOT.rglob("*.hpp"):
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for match in pattern.finditer(text):
            opening = match.end() - 1
            closing = matching_brace(text, opening)
            if closing < 0:
                continue
            name = match.group(1)
            bases_text = match.group(2) or ""
            bases = []
            for base in bases_text.split(","):
                base = re.sub(r"\b(public|protected|private|virtual)\b", "", base).strip()
                if base:
                    bases.append(base.split("::")[-1])
            line = text[: match.start()].count("\n") + 1
            body_line = text[: opening + 1].count("\n") + 1
            info = ClassInfo(name=name, bases=bases, path=path, line=line)
            info.methods = parse_methods(text[opening + 1 : closing], body_line, name)
            classes[name] = info
    return classes


def inherited_methods(classes: dict[str, ClassInfo], info: ClassInfo, seen: set[str] | None = None) -> list[Method]:
    seen = set() if seen is None else seen
    result: list[Method] = []
    for base_name in info.bases:
        if base_name in seen or base_name not in classes:
            continue
        seen.add(base_name)
        base = classes[base_name]
        result.extend(base.methods)
        result.extend(inherited_methods(classes, base, seen))
    return result


def main() -> int:
    classes = parse_headers()
    findings = 0
    for info in sorted(classes.values(), key=lambda item: (str(item.path), item.line)):
        inherited = inherited_methods(classes, info)
        if not inherited:
            continue
        by_name: dict[str, list[Method]] = {}
        for method in inherited:
            by_name.setdefault(method.name, []).append(method)
        for method in info.methods:
            bases = by_name.get(method.name, [])
            if not bases:
                continue
            same_params = [base for base in bases if base.params == method.params]
            virtual_bases = [base for base in bases if base.virtual or base.override]
            issue = ""
            if same_params and any(base.virtual or base.override for base in same_params) and not method.override:
                issue = "missing override"
            elif same_params and not any(base.virtual or base.override for base in same_params):
                issue = "hides non-virtual base"
            elif virtual_bases and not method.override:
                issue = "overload hides virtual base"
            if issue:
                findings += 1
                relative = info.path.relative_to(ROOT.parent)
                print(f"{relative}:{method.line}: {info.name}::{method.name}: {issue}")
    print(f"findings={findings}")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
