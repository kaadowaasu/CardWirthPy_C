"""List C++ member functions that add try/catch absent from CardWirthPy.

This is deliberately a narrow audit: it matches ``Class::method`` definitions
to Python methods with the same class and method name.  Native adapters and
C++-only library code are not guessed at, and must be reviewed separately.
"""

from __future__ import annotations

import argparse
import ast
import json
import pathlib
import re
from typing import Dict, Iterator, List, Optional, Tuple


DEFINITION = re.compile(
    r"(?m)^[^#\n{};]*?\b(?P<class>[A-Za-z_]\w*)::(?P<method>~?[A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{"
)
CATCH = re.compile(r"\bcatch\s*\(")


class PythonProtectionVisitor(ast.NodeVisitor):
    def __init__(self) -> None:
        self.classes: List[str] = []
        self.result: Dict[Tuple[str, str], int] = {}

    def visit_ClassDef(self, node: ast.ClassDef) -> None:
        self.classes.append(node.name)
        for child in node.body:
            self.visit(child)
        self.classes.pop()

    def visit_FunctionDef(self, node: ast.FunctionDef) -> None:
        if self.classes:
            count = protection_count(node)
            key = (self.classes[-1], node.name)
            self.result[key] = max(self.result.get(key, 0), count)
        # 入れ子の関数は実装詳細であり、クラスメソッドではない。

    visit_AsyncFunctionDef = visit_FunctionDef


class ProtectionCounter(ast.NodeVisitor):
    def __init__(self) -> None:
        self.count = 0

    def visit_Try(self, node: ast.Try) -> None:
        self.count += len(node.handlers) + bool(node.finalbody)
        self.generic_visit(node)

    def visit_With(self, node: ast.With) -> None:
        self.count += 1
        self.generic_visit(node)

    visit_AsyncWith = visit_With

    def visit_FunctionDef(self, node: ast.FunctionDef) -> None:
        # 入れ子の関数は独立した C++ 関数として生成されるため
        # 、それを囲む関数が保護されていると見なしてはならない。
        return

    visit_AsyncFunctionDef = visit_FunctionDef


def protection_count(node: ast.FunctionDef) -> int:
    counter = ProtectionCounter()
    for statement in node.body:
        counter.visit(statement)
    return counter.count


def matching_brace(text: str, opening: int) -> int:
    depth = 0
    quote = ""
    escaped = False
    line_comment = False
    block_comment = False
    index = opening
    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if line_comment:
            if char == "\n":
                line_comment = False
        elif block_comment:
            if char == "*" and following == "/":
                block_comment = False
                index += 1
        elif quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
        elif char == "/" and following == "/":
            line_comment = True
            index += 1
        elif char == "/" and following == "*":
            block_comment = True
            index += 1
        elif char in ('"', "'"):
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
        index += 1
    return len(text)


def code_mask(text: str) -> str:
    """Blank comments and literals while retaining offsets and braces."""
    result = list(text)
    index = 0
    while index < len(text):
        if text.startswith("//", index):
            end = text.find("\n", index)
            if end < 0:
                end = len(text)
            for pos in range(index, end):
                result[pos] = " "
            index = end
        elif text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = len(text) if end < 0 else end + 2
            for pos in range(index, end):
                if result[pos] != "\n":
                    result[pos] = " "
            index = end
        elif text[index] in ('"', "'"):
            quote = text[index]
            pos = index + 1
            while pos < len(text):
                if text[pos] == "\\":
                    pos += 2
                    continue
                pos += 1
                if text[pos - 1] == quote:
                    break
            for blank in range(index, min(pos, len(text))):
                if result[blank] != "\n":
                    result[blank] = " "
            index = pos
        else:
            index += 1
    return "".join(result)


def matching(mask: str, opening: int, left: str, right: str) -> Optional[int]:
    depth = 0
    for index in range(opening, len(mask)):
        if mask[index] == left:
            depth += 1
        elif mask[index] == right:
            depth -= 1
            if depth == 0:
                return index
    return None


def try_catch_ranges(text: str, begin: int, end: int) -> List[Tuple[int, int, str]]:
    """Return non-overlapping outer try/catch ranges in one function."""
    mask = code_mask(text)
    candidates: List[Tuple[int, int, str]] = []
    for match in re.finditer(r"\btry\b", mask[begin:end]):
        start = begin + match.start()
        opening = start + len("try")
        while opening < end and mask[opening].isspace():
            opening += 1
        if opening >= end or mask[opening] != "{":
            continue
        body_end = matching(mask, opening, "{", "}")
        if body_end is None or body_end >= end:
            continue
        cursor = body_end + 1
        caught = False
        while True:
            while cursor < end and mask[cursor].isspace():
                cursor += 1
            caught_match = re.match(r"catch\b", mask[cursor:end])
            if not caught_match:
                break
            paren = cursor + caught_match.end()
            while paren < end and mask[paren].isspace():
                paren += 1
            if paren >= end or mask[paren] != "(":
                break
            paren_end = matching(mask, paren, "(", ")")
            if paren_end is None:
                break
            catch_open = paren_end + 1
            while catch_open < end and mask[catch_open].isspace():
                catch_open += 1
            if catch_open >= end or mask[catch_open] != "{":
                break
            catch_end = matching(mask, catch_open, "{", "}")
            if catch_end is None:
                break
            cursor = catch_end + 1
            caught = True
        if caught:
            candidates.append((start, cursor, text[opening:body_end + 1]))

    # 外側の try を置換しても、本体内の入れ子の try/catch
    # は残る。それらは位置を再計算した次の走査で除く。
    selected: List[Tuple[int, int, str]] = []
    for candidate in sorted(candidates, key=lambda item: (item[0], -item[1])):
        if not any(parent[0] <= candidate[0] and candidate[1] <= parent[1] for parent in selected):
            selected.append(candidate)
    return selected


def cpp_members(path: pathlib.Path) -> Iterator[Tuple[str, str, int, int]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    for match in DEFINITION.finditer(text):
        opening = match.end() - 1
        end = matching_brace(text, opening)
        catches = len(CATCH.findall(text, opening, end))
        if catches:
            line = text.count("\n", 0, match.start()) + 1
            yield match.group("class"), match.group("method"), line, catches


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--python-root", type=pathlib.Path, default=pathlib.Path("cw"))
    parser.add_argument("--cpp-root", type=pathlib.Path, default=pathlib.Path("src/cw"))
    parser.add_argument("--inventory", type=pathlib.Path,
                        default=pathlib.Path("src/cw/inventory.json"))
    parser.add_argument("--fix", action="store_true",
                        help="remove unmatched try/catch and keep the try body as a plain scope")
    args = parser.parse_args()

    findings = []
    fixes: Dict[pathlib.Path, List[Tuple[int, int, str]]] = {}
    for cpp_path in args.cpp_root.rglob("*.cpp"):
        relative = cpp_path.relative_to(args.cpp_root)
        python_path = args.python_root / relative.with_suffix(".py")
        if not python_path.exists():
            continue
        tree = ast.parse(python_path.read_text(encoding="utf-8-sig"), filename=str(python_path))
        visitor = PythonProtectionVisitor()
        visitor.visit(tree)
        for class_name, method_name, line, catches in cpp_members(cpp_path):
            python_count = visitor.result.get((class_name, method_name))
            if python_count == 0:
                findings.append((str(relative), line, class_name, method_name, catches))
                if args.fix:
                    text = cpp_path.read_text(encoding="utf-8")
                    match = next((item for item in DEFINITION.finditer(text)
                                  if item.group("class") == class_name and
                                  item.group("method") == method_name and
                                  text.count("\n", 0, item.start()) + 1 == line), None)
                    if match:
                        opening = match.end() - 1
                        end = matching_brace(text, opening)
                        fixes.setdefault(cpp_path, []).extend(try_catch_ranges(text, opening, end))

    # 入れ子・モジュールの関数は、名前空間内の C++ 関数として生成
    # する。inventory.json に正確な Python の
    # ソース行と生成 C++ 名を保持し、綴りから関係を推測しない。
    inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
    for module in inventory["modules"]:
        cpp_path = args.cpp_root / module["cpp_source"]
        python_path = pathlib.Path(module["source"])
        if not cpp_path.exists() or not python_path.exists():
            continue
        tree = ast.parse(python_path.read_text(encoding="utf-8-sig"), filename=str(python_path))
        functions_by_line = {
            node.lineno: node for node in ast.walk(tree)
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        }
        text = cpp_path.read_text(encoding="utf-8")
        for function in module["functions"]:
            node = functions_by_line.get(function["line"])
            if node is None or protection_count(node):
                continue
            name = function["cpp"]
            definition = re.compile(
                r"(?m)^[^#\n{};]*?\b" + re.escape(name) +
                r"\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{")
            for match in definition.finditer(text):
                opening = match.end() - 1
                end = matching_brace(text, opening)
                catches = len(CATCH.findall(text, opening, end))
                if not catches:
                    continue
                line = text.count("\n", 0, match.start()) + 1
                relative = cpp_path.relative_to(args.cpp_root)
                findings.append((str(relative), line, "<function>", name, catches))
                if args.fix:
                    fixes.setdefault(cpp_path, []).extend(try_catch_ranges(text, opening, end))

    if args.fix:
        changed = 0
        for path, replacements in fixes.items():
            text = path.read_text(encoding="utf-8")
            for begin, end, replacement in sorted(set(replacements), reverse=True):
                text = text[:begin] + replacement + text[end:]
                changed += 1
            path.write_text(text, encoding="utf-8")
        print(f"removed={changed}")
        return 0

    for relative, line, class_name, method_name, catches in sorted(findings):
        print(f"{relative}:{line}: {class_name}::{method_name} has {catches} catch block(s); Python has none")
    print(f"findings={len(findings)}")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
