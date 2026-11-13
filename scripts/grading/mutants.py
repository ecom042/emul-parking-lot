"""Deterministic source mutants for C files.

Operators (each mutant changes one place):

- ROR  relational: < <=, > >=, == !=, each to its neighbour or negation
- LCR  logical connector: && <-> ||
- AOR  arithmetic: + <-> -, / -> *, % -> /
- NEG  negate an if/while condition: if (c) -> if (!(c))
- CRP  integer constant: 0 -> 1, 1 -> 0, n -> n + 1 (not inside [ ] or in a
       `= { ... }` initializer: data tables such as fonts are not logic)
- SDL  delete a call statement inside a function body: f(...); -> ;

Comments, strings, character literals and preprocessor lines are skipped,
except the value of an object-like `#define NAME value`. A mutant's id is
stable while its line keeps its text: "<file>:<line>:<column>:<op>:<to>".
"""
from dataclasses import dataclass
import re

TOKEN = re.compile(r"""
    (?P<space>\s+)
  | (?P<comment>//[^\n]*|/\*.*?\*/)
  | (?P<string>"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')
  | (?P<number>0[xX][0-9a-fA-F]+[uUlL]*|\d+(?:\.\d*)?(?:[eE][-+]?\d+)?[uUlLfF]*)
  | (?P<ident>[A-Za-z_]\w*)
  | (?P<op>->|\+\+|--|<<=|>>=|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^]=|[-+*/%<>=!&|^~?:;,.(){}\[\]#])
  | (?P<other>.)
""", re.S | re.X)

ROR = {"<": ["<=", ">="], "<=": ["<", ">"], ">": [">=", "<="], ">=": [">", "<"],
       "==": ["!="], "!=": ["=="]}
LCR = {"&&": ["||"], "||": ["&&"]}
AOR = {"+": ["-"], "-": ["+"], "/": ["*"], "%": ["/"]}
STATEMENT_START = {";", "{", "}", "else"}
CONTROL = {"if", "while", "for", "switch"}


@dataclass(frozen=True)
class Token:
    kind: str
    text: str
    start: int
    end: int
    line: int
    column: int


@dataclass(frozen=True)
class Mutant:
    id: str
    file: str
    line: int
    op: str
    edits: tuple  # ((start, end, replacement), ...) applied right to left
    original: str
    replacement: str

    def apply(self, source):
        for start, end, text in sorted(self.edits, reverse=True):
            source = source[:start] + text + source[end:]
        return source


def tokens(source):
    """Code tokens, without comments/strings/space and outside the preprocessor
    except the value tokens of `#define NAME value` lines."""
    result, line, line_start = [], 1, 0
    directive = None  # None, or [tokens seen so far on a preprocessor line]
    for match in TOKEN.finditer(source):
        kind, text, start = match.lastgroup, match.group(), match.start()
        if kind in ("space", "comment", "string"):
            if directive is not None and "\n" in text and not source[start - 1:start] == "\\":
                directive = None
        else:
            token = Token(kind, text, start, match.end(), line, start - line_start + 1)
            at_line_start = source[line_start:start].strip() == ""
            if text == "#" and at_line_start:
                directive = []
            elif directive is not None:
                directive.append(token)
                # #define NAME value  (not a function-like macro NAME(...))
                if (len(directive) >= 3 and directive[0].text == "define"
                        and not (directive[1].end == directive[2].start and directive[2].text == "(")):
                    result.append(token)
            else:
                result.append(token)
        newlines = text.count("\n")
        if newlines:
            line += newlines
            line_start = start + text.rindex("\n") + 1
    return result


def _matching(code, index):
    """Index of the token closing the bracket at `index`."""
    pairs = {"(": ")", "[": "]", "{": "}"}
    opening, closing, depth = code[index].text, pairs[code[index].text], 0
    for position in range(index, len(code)):
        if code[position].text == opening:
            depth += 1
        elif code[position].text == closing:
            depth -= 1
            if depth == 0:
                return position
    return None


def _binary(code, index):
    """The operator has an operand on both sides (not unary/prefix use)."""
    if index == 0:
        return False
    before = code[index - 1]
    return before.kind in ("ident", "number") or before.text in (")", "]")


def mutants(name, source):
    code = tokens(source)
    found = []

    def add(op, token, edits, replacement):
        mutant_id = f"{name}:{token.line}:{token.column}:{op}:{replacement}"
        found.append(Mutant(mutant_id, name, token.line, op, tuple(edits), token.text, replacement))

    brackets, depth, conditions = 0, 0, set()
    initializer_end = -1
    for index, token in enumerate(code):
        if token.text == "{" and index and code[index - 1].text == "=" and index > initializer_end:
            initializer_end = _matching(code, index) or len(code)
        if token.text == "[":
            brackets += 1
        elif token.text == "]":
            brackets = max(0, brackets - 1)
        elif token.text == "{":
            depth += 1
        elif token.text == "}":
            depth = max(0, depth - 1)
        if token.kind == "ident" and token.text in CONTROL and index + 1 < len(code) \
                and code[index + 1].text == "(":
            closing = _matching(code, index + 1)
            if closing is not None:
                conditions.add(closing)
                # Not the `while` of do { } while (c): negating it never ends.
                do_while = token.text == "while" and index and code[index - 1].text == "}"
                if token.text in ("if", "while") and not do_while:
                    add("NEG", token, [(code[index + 1].end, code[index + 1].end, "!("),
                                       (code[closing].start, code[closing].start, ")")],
                        f"{token.text}!")
        if token.kind == "op":
            for to in ROR.get(token.text, []):
                add("ROR", token, [(token.start, token.end, to)], to)
            for to in LCR.get(token.text, []):
                add("LCR", token, [(token.start, token.end, to)], to)
            if token.text in AOR and (token.text in "+-" or _binary(code, index)):
                for to in AOR[token.text]:
                    add("AOR", token, [(token.start, token.end, to)], to)
        elif token.kind == "number" and not brackets and index > initializer_end and re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", token.text.rstrip("uUlL")):
            digits = token.text.rstrip("uUlL")
            suffix = token.text[len(digits):]
            value = int(digits, 0)
            to = "1" if value == 0 else "0" if value == 1 else str(value + 1)
            add("CRP", token, [(token.start, token.end, to + suffix)], to)
        elif token.kind == "ident" and token.text not in CONTROL and index + 1 < len(code) \
                and code[index + 1].text == "(":
            previous = code[index - 1] if index else None
            starts = depth > 0 and previous is not None and (
                previous.text in STATEMENT_START or
                (previous.text == ")" and index - 1 in conditions))
            closing = _matching(code, index + 1)
            if starts and closing is not None and closing + 1 < len(code) \
                    and code[closing + 1].text == ";":
                add("SDL", token, [(token.start, code[closing].end, "")], "deleted")
    return found
