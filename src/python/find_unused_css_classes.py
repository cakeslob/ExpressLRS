#!/usr/bin/env python3
"""List CSS classes under html/ and report which are never referenced."""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import defaultdict
from pathlib import Path


DEFAULT_EXCLUDED_DIRS = {"__pycache__", "dist", "node_modules"}
CSS_SUFFIXES = {".css"}
REFERENCE_SUFFIXES = {".html", ".js"}

CSS_COMMENT_RE = re.compile(r"/\*.*?\*/", re.S)
CSS_STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
CSS_CLASS_RE = re.compile(r"(?<![A-Za-z0-9_-])\.(-?[_a-zA-Z][-_a-zA-Z0-9]*)")
JS_STRING_RE = re.compile(
    r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|`(?:\\.|[^`\\])*`',
    re.S,
)
CLASS_TOKEN_RE = re.compile(r"(?<![A-Za-z0-9_-])(-?[_a-zA-Z][-_a-zA-Z0-9]*)(?![A-Za-z0-9_-])")
JS_IDENTIFIER_RE = re.compile(r"[A-Za-z_$][A-Za-z0-9_$]*")
TEMPLATE_EXPR_RE = re.compile(r"\$\{([^}]+)\}")
CLASS_FRAGMENT_RE = re.compile(r"[A-Za-z0-9_${}-]+")
JS_STRING_ASSIGNMENT_PATTERNS = (
    re.compile(
        r"\b(?:const|let|var)\s+([A-Za-z_$][A-Za-z0-9_$]*)\s*=\s*'((?:\\.|[^'\\])*)'",
        re.S,
    ),
    re.compile(
        r'\b(?:const|let|var)\s+([A-Za-z_$][A-Za-z0-9_$]*)\s*=\s*"((?:\\.|[^"\\])*)"',
        re.S,
    ),
    re.compile(
        r"(?m)^\s*([A-Za-z_$][A-Za-z0-9_$]*)\s*=\s*'((?:\\.|[^'\\])*)'",
        re.S,
    ),
    re.compile(
        r'(?m)^\s*([A-Za-z_$][A-Za-z0-9_$]*)\s*=\s*"((?:\\.|[^"\\])*)"',
        re.S,
    ),
)


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parent.parent
    default_html_root = repo_root / "html"

    parser = argparse.ArgumentParser(
        description=(
            "Read CSS classes from html/ recursively and report which ones are "
            "never referenced by .js or .html files."
        )
    )
    parser.add_argument(
        "--html-root",
        type=Path,
        default=default_html_root,
        help=f"Root folder to scan (default: {default_html_root})",
    )
    parser.add_argument(
        "--exclude-dir",
        action="append",
        default=[],
        help="Directory name to skip while scanning. Can be passed multiple times.",
    )
    parser.add_argument(
        "--no-default-excludes",
        action="store_true",
        help="Do not skip default generated/dependency directories.",
    )
    return parser.parse_args()


def iter_files(root: Path, suffixes: set[str], excluded_dirs: set[str]):
    for current_root, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(
            dirname
            for dirname in dirnames
            if dirname.lower() not in excluded_dirs
        )

        for filename in sorted(filenames):
            path = Path(current_root) / filename
            if path.suffix.lower() in suffixes:
                yield path


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="ignore")


def extract_css_classes(css_text: str) -> set[str]:
    stripped = CSS_COMMENT_RE.sub(" ", css_text)
    stripped = CSS_STRING_RE.sub(" ", stripped)
    return {match.group(1) for match in CSS_CLASS_RE.finditer(stripped)}


def collect_string_assignments(js_text: str) -> dict[str, set[str]]:
    assignments: dict[str, set[str]] = defaultdict(set)
    for pattern in JS_STRING_ASSIGNMENT_PATTERNS:
        for match in pattern.finditer(js_text):
            assignments[match.group(1)].add(match.group(2))
    return assignments


def expand_template_literal(template_body: str, assignments: dict[str, set[str]]) -> list[str] | None:
    parts: list[str] = []
    variants: list[list[str]] = []
    last_index = 0

    for match in TEMPLATE_EXPR_RE.finditer(template_body):
        parts.append(template_body[last_index:match.start()])
        expression = match.group(1).strip()
        if not JS_IDENTIFIER_RE.fullmatch(expression):
            return None

        values = sorted(assignments.get(expression, ()))
        if not values:
            return None

        variants.append(values)
        last_index = match.end()

    parts.append(template_body[last_index:])

    expanded = [parts[0]]
    for suffix, values in zip(parts[1:], variants):
        next_expanded: list[str] = []
        for base in expanded:
            for value in values:
                next_expanded.append(f"{base}{value}{suffix}")
        expanded = next_expanded
        if len(expanded) > 256:
            return None

    return expanded


def match_template_wildcards(template_body: str, known_classes: set[str]) -> set[str]:
    placeholder = "__EXPR__"
    wildcarded = TEMPLATE_EXPR_RE.sub(placeholder, template_body)
    matches = set()

    for fragment in CLASS_FRAGMENT_RE.findall(wildcarded):
        if placeholder not in fragment:
            continue
        if fragment == placeholder:
            continue

        literal_fragment = fragment.replace(placeholder, "")
        if not literal_fragment or literal_fragment == "-":
            continue

        pattern = re.escape(fragment).replace(re.escape(placeholder), r"[-_a-zA-Z0-9]+")
        full_pattern = re.compile(rf"^{pattern}$")
        for css_class in known_classes:
            if full_pattern.fullmatch(css_class):
                matches.add(css_class)

    return matches


def extract_reference_tokens(path: Path, text: str, known_classes: set[str]) -> set[str]:
    if path.suffix.lower() != ".js":
        return {match.group(1) for match in CLASS_TOKEN_RE.finditer(text)}

    assignments = collect_string_assignments(text)
    tokens = set()

    for source in JS_STRING_RE.findall(text):
        if source.startswith(("'", '"')):
            content = source[1:-1]
            tokens.update(match.group(1) for match in CLASS_TOKEN_RE.finditer(content))
            continue

        template_body = source[1:-1]
        expanded = expand_template_literal(template_body, assignments)
        if expanded is not None:
            for variant in expanded:
                tokens.update(match.group(1) for match in CLASS_TOKEN_RE.finditer(variant))
            continue

        literal_only = TEMPLATE_EXPR_RE.sub(" ", template_body)
        tokens.update(match.group(1) for match in CLASS_TOKEN_RE.finditer(literal_only))
        tokens.update(match_template_wildcards(template_body, known_classes))

    return tokens


def main() -> int:
    args = parse_args()
    html_root = args.html_root.resolve()

    if not html_root.is_dir():
        print(f"html root not found: {html_root}", file=sys.stderr)
        return 1

    excluded_dirs = set()
    if not args.no_default_excludes:
        excluded_dirs.update(dirname.lower() for dirname in DEFAULT_EXCLUDED_DIRS)
    excluded_dirs.update(dirname.lower() for dirname in args.exclude_dir)

    css_files = list(iter_files(html_root, CSS_SUFFIXES, excluded_dirs))
    reference_files = list(iter_files(html_root, REFERENCE_SUFFIXES, excluded_dirs))

    class_definitions: dict[str, set[str]] = defaultdict(set)
    for css_file in css_files:
        relative_path = css_file.relative_to(html_root).as_posix()
        for css_class in extract_css_classes(read_text(css_file)):
            class_definitions[css_class].add(relative_path)

    all_classes = sorted(class_definitions)

    referenced_classes = set()
    known_classes = set(class_definitions)
    for reference_file in reference_files:
        tokens = extract_reference_tokens(reference_file, read_text(reference_file), known_classes)
        referenced_classes.update(token for token in tokens if token in class_definitions)

    unused_classes = sorted(set(all_classes) - referenced_classes)

    excluded_display = ", ".join(sorted(excluded_dirs)) if excluded_dirs else "(none)"
    print(f"HTML root: {html_root}")
    print(f"Excluded directories: {excluded_display}")
    print(f"CSS files scanned: {len(css_files)}")
    print(f"HTML/JS files scanned: {len(reference_files)}")
    print()

    print(f"All CSS classes ({len(all_classes)}):")
    if all_classes:
        for css_class in all_classes:
            print(css_class)
    else:
        print("(none)")
    print()

    print(f"Classes never referenced in .js/.html ({len(unused_classes)}):")
    if unused_classes:
        for css_class in unused_classes:
            locations = ", ".join(sorted(class_definitions[css_class]))
            print(f"{css_class}  [{locations}]")
    else:
        print("(none)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
