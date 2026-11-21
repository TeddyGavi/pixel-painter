#!/usr/bin/env python3
"""validate_case.py -- static checker for bead_case.scad Rev B (no OpenSCAD needed).

  python3 validate_case.py [bead_case.scad] [J|D ...]     (default: both variants)

1. Tokenises and fully parses the file with an OpenSCAD grammar (statements,
   expressions, list comprehensions): catches unbalanced braces/parentheses,
   missing semicolons and other syntax errors.
2. Scope check: every identifier used must be a parameter, a module/function
   argument, a loop/let variable, a local assignment or an OpenSCAD builtin;
   every called module/function must exist; named arguments passed to user
   modules must exist; no variable is assigned twice in one scope; top-level
   values are never used before they are assigned.
3. Evaluates every top-level assignment (same semantics as OpenSCAD: degrees,
   vectors, ternaries, user functions), runs the file's top-level assert()s,
   then recomputes the key clearances from those values and prints a table.
   Steps 4-6 run once per control variant (ctrl = "J" and ctrl = "D").
Exit status 0 = no errors (warnings allowed), 1 = errors.
"""
import math
import re
import sys

# ---------------------------------------------------------------- tokeniser
TOK_RE = re.compile(r"""
    (?P<ws>\s+) |
    (?P<lc>//[^\n]*) |
    (?P<bc>/\*.*?\*/) |
    (?P<num>(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?) |
    (?P<str>"(?:\\.|[^"\\])*") |
    (?P<id>\$?[A-Za-z_][A-Za-z0-9_]*) |
    (?P<op><=|>=|==|!=|&&|\|\||[-+*/%^!<>?:=,;()\[\]{}.#])
""", re.S | re.X)


class Tok:
    __slots__ = ("kind", "val", "line")

    def __init__(self, kind, val, line):
        self.kind, self.val, self.line = kind, val, line

    def __repr__(self):
        return f"{self.kind}:{self.val!r}@{self.line}"


def tokenize(src):
    toks, pos, line = [], 0, 1
    while pos < len(src):
        m = TOK_RE.match(src, pos)
        if not m:
            raise SyntaxError(f"line {line}: unexpected character {src[pos]!r}")
        kind = m.lastgroup
        text = m.group()
        if kind == "num":
            toks.append(Tok("num", float(text), line))
        elif kind == "str":
            toks.append(Tok("str", bytes(text[1:-1], "utf-8").decode("unicode_escape"), line))
        elif kind == "id":
            toks.append(Tok("id", text, line))
        elif kind == "op":
            toks.append(Tok("op", text, line))
        line += text.count("\n")
        pos = m.end()
    toks.append(Tok("eof", None, line))
    return toks


def check_balance(src):
    """Bracket balance with line numbers (comments and strings stripped)."""
    errs, stack = [], []
    pairs = {")": "(", "]": "[", "}": "{"}
    for t in tokenize(src):
        if t.kind != "op":
            continue
        if t.val in "([{":
            stack.append(t)
        elif t.val in ")]}":
            if not stack or stack[-1].val != pairs[t.val]:
                errs.append(f"line {t.line}: unmatched '{t.val}'")
            else:
                stack.pop()
    for t in stack:
        errs.append(f"line {t.line}: unclosed '{t.val}'")
    return errs


# ---------------------------------------------------------------- parser
KEYWORDS = {"module", "function", "if", "else", "for", "let", "each", "true", "false", "undef",
            "intersection_for", "include", "use"}


class Parser:
    def __init__(self, toks):
        self.t, self.i = toks, 0

    def peek(self, k=0):
        return self.t[self.i + k]

    def at(self, val, k=0):
        tk = self.peek(k)
        return tk.kind in ("op", "id") and tk.val == val

    def next(self):
        tk = self.t[self.i]
        self.i += 1
        return tk

    def expect(self, val):
        tk = self.next()
        if tk.val != val or tk.kind not in ("op", "id"):
            raise SyntaxError(f"line {tk.line}: expected '{val}', got {tk.val!r}")
        return tk

    def ident(self):
        tk = self.next()
        if tk.kind != "id" or tk.val in KEYWORDS:
            raise SyntaxError(f"line {tk.line}: expected identifier, got {tk.val!r}")
        return tk

    # statements -------------------------------------------------------
    def program(self):
        out = []
        while self.peek().kind != "eof":
            out.append(self.statement())
        return out

    def statement(self):
        tk = self.peek()
        line = tk.line
        if self.at(";"):
            self.next()
            return ("empty", line)
        if self.at("{"):
            self.next()
            body = []
            while not self.at("}"):
                if self.peek().kind == "eof":
                    raise SyntaxError(f"line {line}: unclosed block")
                body.append(self.statement())
            self.next()
            return ("block", line, body)
        if self.at("module"):
            self.next()
            name = self.ident().val
            params = self.params()
            body = self.statement()
            return ("module", line, name, params, body)
        if self.at("function"):
            self.next()
            name = self.ident().val
            params = self.params()
            self.expect("=")
            e = self.expr()
            self.expect(";")
            return ("function", line, name, params, e)
        if tk.kind == "op" and tk.val in "!#%*":
            self.next()
            return ("modifier", line, tk.val, self.statement())
        if self.at("if"):
            self.next()
            self.expect("(")
            c = self.expr()
            self.expect(")")
            then = self.statement()
            els = None
            if self.at("else"):
                self.next()
                els = self.statement()
            return ("if", line, c, then, els)
        if self.at("for") or self.at("intersection_for"):
            self.next()
            assigns = self.assign_list()
            return ("for", line, assigns, self.statement())
        if self.at("let"):
            self.next()
            assigns = self.assign_list()
            return ("letstmt", line, assigns, self.statement())
        if tk.kind == "id" and self.at("=", 1):
            name = self.ident().val
            self.expect("=")
            e = self.expr()
            self.expect(";")
            return ("assign", line, name, e)
        if tk.kind == "id":
            name = self.ident().val
            args = self.args()
            child = self.statement()
            return ("inst", line, name, args, child)
        raise SyntaxError(f"line {line}: unexpected {tk.val!r}")

    def params(self):
        self.expect("(")
        out = []
        while not self.at(")"):
            name = self.ident().val
            default = None
            if self.at("="):
                self.next()
                default = self.expr()
            out.append((name, default))
            if not self.at(")"):
                self.expect(",")
        self.next()
        return out

    def args(self):
        self.expect("(")
        out = []
        while not self.at(")"):
            name = None
            if self.peek().kind == "id" and self.at("=", 1):
                name = self.next().val
                self.next()
            out.append((name, self.expr()))
            if not self.at(")"):
                self.expect(",")
        self.next()
        return out

    def assign_list(self):
        self.expect("(")
        out = []
        while not self.at(")"):
            name = self.ident().val
            self.expect("=")
            out.append((name, self.expr()))
            if not self.at(")"):
                self.expect(",")
        self.next()
        return out

    # expressions --------------------------------------------------------
    def expr(self):
        line = self.peek().line
        if self.at("function"):
            self.next()
            p = self.params()
            return ("lambda", line, p, self.expr())
        if self.at("let"):
            self.next()
            a = self.assign_list()
            return ("let", line, a, self.expr())
        c = self.logor()
        if self.at("?"):
            self.next()
            a = self.expr()
            self.expect(":")
            b = self.expr()
            return ("tern", line, c, a, b)
        return c

    def binlevel(self, ops, sub):
        left = sub()
        while self.peek().kind == "op" and self.peek().val in ops:
            op = self.next()
            right = sub()
            left = ("bin", op.line, op.val, left, right)
        return left

    def logor(self):
        return self.binlevel(("||",), self.logand)

    def logand(self):
        return self.binlevel(("&&",), self.equality)

    def equality(self):
        return self.binlevel(("==", "!="), self.compare)

    def compare(self):
        return self.binlevel(("<", "<=", ">", ">="), self.additive)

    def additive(self):
        return self.binlevel(("+", "-"), self.mult)

    def mult(self):
        return self.binlevel(("*", "/", "%"), self.unary)

    def unary(self):
        tk = self.peek()
        if tk.kind == "op" and tk.val in ("!", "-", "+"):
            self.next()
            return ("un", tk.line, tk.val, self.unary())
        return self.power()

    def power(self):
        base = self.postfix()
        if self.at("^"):
            op = self.next()
            return ("bin", op.line, "^", base, self.unary())
        return base

    def postfix(self):
        e = self.primary()
        while True:
            if self.at("("):
                e = ("call", self.peek().line, e, self.args())
            elif self.at("["):
                line = self.next().line
                idx = self.expr()
                self.expect("]")
                e = ("index", line, e, idx)
            elif self.at("."):
                line = self.next().line
                e = ("member", line, e, self.ident().val)
            else:
                return e

    def primary(self):
        tk = self.next()
        if tk.kind == "num":
            return ("num", tk.line, tk.val)
        if tk.kind == "str":
            return ("str", tk.line, tk.val)
        if tk.kind == "id":
            if tk.val == "true":
                return ("const", tk.line, True)
            if tk.val == "false":
                return ("const", tk.line, False)
            if tk.val == "undef":
                return ("const", tk.line, None)
            if tk.val in KEYWORDS:
                raise SyntaxError(f"line {tk.line}: unexpected keyword {tk.val!r}")
            return ("id", tk.line, tk.val)
        if tk.kind == "op" and tk.val == "(":
            e = self.expr()
            self.expect(")")
            return e
        if tk.kind == "op" and tk.val == "[":
            return self.vector(tk.line)
        raise SyntaxError(f"line {tk.line}: unexpected {tk.val!r} in expression")

    def vector(self, line):
        if self.at("]"):
            self.next()
            return ("vec", line, [])
        if self.at("for") or self.at("if") or self.at("let") or self.at("each"):
            elems = [self.comp_elem()]
        else:
            first = self.expr()
            if self.at(":"):
                self.next()
                second = self.expr()
                third = None
                if self.at(":"):
                    self.next()
                    third = self.expr()
                self.expect("]")
                return ("range", line, first, second, third)
            elems = [first]
        while self.at(","):
            self.next()
            if self.at("]"):
                break
            elems.append(self.comp_elem())
        self.expect("]")
        return ("vec", line, elems)

    def comp_elem(self):
        line = self.peek().line
        if self.at("for"):
            self.next()
            a = self.assign_list()
            return ("cfor", line, a, self.comp_elem())
        if self.at("if"):
            self.next()
            self.expect("(")
            c = self.expr()
            self.expect(")")
            a = self.comp_elem()
            b = None
            if self.at("else"):
                self.next()
                b = self.comp_elem()
            return ("cif", line, c, a, b)
        if self.at("let"):
            self.next()
            a = self.assign_list()
            return ("clet", line, a, self.comp_elem())
        if self.at("each"):
            self.next()
            return ("ceach", line, self.expr())
        return self.expr()


# ---------------------------------------------------------------- scope checker
BUILTIN_MODULES = {
    "cube", "sphere", "cylinder", "polyhedron", "square", "circle", "polygon", "text", "import",
    "translate", "rotate", "scale", "resize", "mirror", "multmatrix", "color", "offset", "hull",
    "minkowski", "union", "difference", "intersection", "linear_extrude", "rotate_extrude",
    "projection", "surface", "render", "children", "echo", "assert", "group", "roof",
}
BUILTIN_FUNCS = {
    "abs", "sign", "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "floor", "ceil", "round",
    "ln", "log", "exp", "pow", "sqrt", "min", "max", "norm", "cross", "len", "concat", "lookup",
    "str", "chr", "ord", "search", "rands", "version", "version_num", "is_undef", "is_num",
    "is_bool", "is_string", "is_list", "is_function", "echo", "assert", "parent_module",
}
SPECIAL_VARS = {"$fn", "$fa", "$fs", "$t", "$preview", "$children", "$vpr", "$vpt", "$vpd",
                "$vpf", "$parent_modules", "PI"}


class Checker:
    def __init__(self, prog):
        self.prog = prog
        self.errors, self.warnings = [], []
        self.modules = {s[2]: s for s in prog if s[0] == "module"}
        self.functions = {s[2]: s for s in prog if s[0] == "function"}
        self.used_modules, self.used_functions, self.used_vars = set(), set(), set()

    def err(self, line, msg):
        self.errors.append(f"line {line}: {msg}")

    def block_assigns(self, stmts, where):
        names = {}
        for s in stmts:
            if s[0] == "assign":
                if s[2] in names:
                    self.err(s[1], f"'{s[2]}' assigned twice in {where} (first at line {names[s[2]]})")
                names[s[2]] = s[1]
        return names

    def run(self):
        top = self.block_assigns(self.prog, "top level")
        env = set(top) | SPECIAL_VARS
        self.walk_block(self.prog, env, toplevel=True, top_order=top)
        for name in self.modules:
            if name not in self.used_modules:
                self.warnings.append(f"module '{name}' is never instantiated")
        for name in top:
            if name not in self.used_vars and not name.startswith("$"):
                self.warnings.append(f"top-level variable '{name}' is never used")

    def walk_block(self, stmts, env, toplevel=False, top_order=None):
        local = set(env) | set(self.block_assigns(stmts, "block") if not toplevel else top_order)
        for s in stmts:
            if toplevel and s[0] == "assign" and top_order is not None:
                # use-before-assign at top level
                self.expr(s[3], local, before=(s[1], top_order))
            else:
                self.stmt(s, local)

    def stmt(self, s, env):
        k = s[0]
        if k == "empty":
            return
        if k == "block":
            self.walk_block(s[2], env)
        elif k == "assign":
            self.expr(s[3], env)
        elif k == "module":
            _, line, name, params, body = s
            penv = set(env) | {p for p, _ in params} | {"$children"}
            for p, d in params:
                if d is not None:
                    self.expr(d, env)
            if body[0] == "block":
                self.walk_block(body[2], penv)
            else:
                self.stmt(body, penv)
        elif k == "function":
            _, line, name, params, e = s
            penv = set(env) | {p for p, _ in params}
            self.expr(e, penv)
        elif k == "modifier":
            self.stmt(s[3], env)
        elif k == "if":
            self.expr(s[2], env)
            self.stmt(s[3], env)
            if s[4]:
                self.stmt(s[4], env)
        elif k in ("for", "letstmt"):
            inner = set(env)
            for n, e in s[2]:
                self.expr(e, env if k == "for" else inner)
                inner.add(n)
            self.stmt(s[3], inner)
        elif k == "inst":
            _, line, name, args, child = s
            if name in self.modules:
                self.used_modules.add(name)
                pnames = {p for p, _ in self.modules[name][3]}
                for an, _ in args:
                    if an is not None and an not in pnames and not an.startswith("$"):
                        self.err(line, f"module '{name}' has no parameter '{an}'")
                npos = sum(1 for an, _ in args if an is None)
                if npos > len(pnames):
                    self.err(line, f"module '{name}' called with {npos} positional args, takes {len(pnames)}")
            elif name not in BUILTIN_MODULES:
                self.err(line, f"unknown module '{name}'")
            for an, e in args:
                self.expr(e, env)
            self.stmt(child, env)
        else:
            raise RuntimeError(k)

    def expr(self, e, env, before=None):
        k = e[0]
        line = e[1]
        if k in ("num", "str", "const"):
            return
        if k == "id":
            name = e[2]
            self.used_vars.add(name)
            if name not in env:
                self.err(line, f"undefined identifier '{name}'")
            elif before is not None and name in before[1] and before[1][name] >= before[0]:
                self.err(line, f"'{name}' used before it is assigned (line {before[1][name]})")
            return
        if k == "bin":
            self.expr(e[3], env, before)
            self.expr(e[4], env, before)
        elif k == "un":
            self.expr(e[3], env, before)
        elif k == "tern":
            for x in e[2:]:
                self.expr(x, env, before)
        elif k == "call":
            f = e[2]
            if f[0] == "id":
                n = f[2]
                if n in self.functions:
                    self.used_functions.add(n)
                    np = len(self.functions[n][3])
                    if len(e[3]) > np:
                        self.err(line, f"function '{n}' called with {len(e[3])} args, takes {np}")
                elif n in BUILTIN_FUNCS:
                    pass
                elif n in env:
                    self.used_vars.add(n)
                else:
                    self.err(line, f"unknown function '{n}'")
            else:
                self.expr(f, env, before)
            for _, a in e[3]:
                self.expr(a, env, before)
        elif k == "index":
            self.expr(e[2], env, before)
            self.expr(e[3], env, before)
        elif k == "member":
            self.expr(e[2], env, before)
        elif k == "vec":
            for x in e[2]:
                self.expr(x, env, before)
        elif k == "range":
            for x in e[2:]:
                if x is not None:
                    self.expr(x, env, before)
        elif k in ("let", "clet"):
            inner = set(env)
            for n, x in e[2]:
                self.expr(x, inner, before)
                inner.add(n)
            self.expr(e[3], inner, before)
        elif k == "cfor":
            inner = set(env)
            for n, x in e[2]:
                self.expr(x, env, before)
                inner.add(n)
            self.expr(e[3], inner, before)
        elif k == "cif":
            self.expr(e[2], env, before)
            self.expr(e[3], env, before)
            if e[4] is not None:
                self.expr(e[4], env, before)
        elif k == "ceach":
            self.expr(e[2], env, before)
        elif k == "lambda":
            inner = set(env) | {p for p, _ in e[2]}
            self.expr(e[3], inner, before)
        else:
            raise RuntimeError(k)


# ---------------------------------------------------------------- evaluator
class Undef:
    pass


def truthy(v):
    if v is None or v is False:
        return False
    if isinstance(v, (int, float)) and not isinstance(v, bool):
        return v != 0
    if isinstance(v, (str, list)):
        return len(v) > 0
    return True


def num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def vadd(a, b, op):
    if num(a) and num(b):
        return a + b if op == "+" else a - b
    if isinstance(a, list) and isinstance(b, list):
        return [vadd(x, y, op) for x, y in zip(a, b)]
    return None


def vmul(a, b):
    if num(a) and num(b):
        return a * b
    if num(a) and isinstance(b, list):
        return [vmul(a, x) for x in b]
    if isinstance(a, list) and num(b):
        return [vmul(x, b) for x in a]
    if isinstance(a, list) and isinstance(b, list) and all(num(x) for x in a + b):
        return sum(x * y for x, y in zip(a, b))
    return None


def rng(start, step, end):
    out, v = [], start
    if step == 0:
        return out
    while (step > 0 and v <= end + 1e-12) or (step < 0 and v >= end - 1e-12):
        out.append(v)
        v += step
    return out


class Evaluator:
    def __init__(self, prog):
        self.functions = {s[2]: s for s in prog if s[0] == "function"}
        self.glob = {"PI": math.pi, "$fn": 0, "$preview": False}

    def ev(self, e, env):
        k = e[0]
        if k in ("num", "str", "const"):
            return e[2]
        if k == "id":
            n = e[2]
            if n in env:
                return env[n]
            if n in self.glob:
                return self.glob[n]
            return None
        if k == "un":
            v = self.ev(e[3], env)
            if e[2] == "!":
                return not truthy(v)
            if e[2] == "-":
                return vmul(-1, v)
            return v
        if k == "bin":
            op = e[2]
            if op == "&&":
                return truthy(self.ev(e[3], env)) and truthy(self.ev(e[4], env))
            if op == "||":
                return truthy(self.ev(e[3], env)) or truthy(self.ev(e[4], env))
            a, b = self.ev(e[3], env), self.ev(e[4], env)
            if op in "+-":
                return vadd(a, b, op)
            if op == "*":
                return vmul(a, b)
            if op == "/":
                if num(a) and num(b):
                    return a / b if b != 0 else (math.inf if a >= 0 else -math.inf)
                if isinstance(a, list) and num(b):
                    return [x / b for x in a]
                return None
            if op == "%":
                return math.fmod(a, b) if num(a) and num(b) and b != 0 else None
            if op == "^":
                return a ** b if num(a) and num(b) else None
            if op == "==":
                return a == b
            if op == "!=":
                return a != b
            if num(a) and num(b) or (isinstance(a, str) and isinstance(b, str)):
                return {"<": a < b, "<=": a <= b, ">": a > b, ">=": a >= b}[op]
            return None
        if k == "tern":
            return self.ev(e[3], env) if truthy(self.ev(e[2], env)) else self.ev(e[4], env)
        if k == "vec":
            out = []
            for x in e[2]:
                self.comp(x, env, out)
            return out
        if k == "range":
            a = self.ev(e[2], env)
            b = self.ev(e[3], env)
            if e[4] is None:
                return rng(a, 1, b)
            return rng(a, b, self.ev(e[4], env))
        if k == "index":
            v, i = self.ev(e[2], env), self.ev(e[3], env)
            try:
                return v[int(i)]
            except Exception:
                return None
        if k == "member":
            v = self.ev(e[2], env)
            idx = {"x": 0, "y": 1, "z": 2}.get(e[3])
            try:
                return v[idx]
            except Exception:
                return None
        if k == "let":
            inner = dict(env)
            for n, x in e[2]:
                inner[n] = self.ev(x, inner)
            return self.ev(e[3], inner)
        if k == "lambda":
            return ("closure", e[2], e[3], dict(env))
        if k == "call":
            args = [self.ev(a, env) for _, a in e[3]]
            names = [n for n, _ in e[3]]
            f = e[2]
            if f[0] == "id" and f[2] in self.functions:
                _, _, _, params, body = self.functions[f[2]]
                inner = dict(self.glob)
                for (p, d), i in zip(params, range(len(params))):
                    inner[p] = args[i] if i < len(args) and names[i] is None else (
                        self.ev(d, inner) if d is not None else None)
                for n, v in zip(names, args):
                    if n is not None:
                        inner[n] = v
                return self.ev(body, inner)
            if f[0] == "id":
                return self.builtin(f[2], args)
            return None
        raise RuntimeError(k)

    def comp(self, x, env, out):
        k = x[0]
        if k == "cfor":
            def rec(i, inner):
                if i == len(x[2]):
                    self.comp(x[3], inner, out)
                    return
                n, ex = x[2][i]
                for v in self.ev(ex, inner) or []:
                    d = dict(inner)
                    d[n] = v
                    rec(i + 1, d)
            rec(0, dict(env))
        elif k == "cif":
            if truthy(self.ev(x[2], env)):
                self.comp(x[3], env, out)
            elif x[4] is not None:
                self.comp(x[4], env, out)
        elif k == "clet":
            inner = dict(env)
            for n, ex in x[2]:
                inner[n] = self.ev(ex, inner)
            self.comp(x[3], inner, out)
        elif k == "ceach":
            out.extend(self.ev(x[2], env) or [])
        else:
            out.append(self.ev(x, env))

    def builtin(self, n, a):
        d2r = math.pi / 180
        if n in ("min", "max"):
            vals = a[0] if len(a) == 1 and isinstance(a[0], list) else a
            try:
                return (min if n == "min" else max)(vals)
            except Exception:
                return None
        table = {
            "abs": lambda x: abs(x), "sign": lambda x: (x > 0) - (x < 0),
            "sin": lambda x: math.sin(x * d2r), "cos": lambda x: math.cos(x * d2r),
            "tan": lambda x: math.tan(x * d2r), "asin": lambda x: math.asin(x) / d2r,
            "acos": lambda x: math.acos(x) / d2r, "atan": lambda x: math.atan(x) / d2r,
            "atan2": lambda y, x: math.atan2(y, x) / d2r, "floor": math.floor, "ceil": math.ceil,
            "round": lambda x: math.floor(x + 0.5), "ln": math.log, "log": math.log10,
            "exp": math.exp, "pow": lambda x, y: x ** y, "sqrt": math.sqrt,
            "norm": lambda v: math.sqrt(sum(c * c for c in v)), "len": len,
            "concat": lambda *v: sum(([x] if not isinstance(x, list) else x for x in v), []),
            "str": lambda *v: "".join(str(x) for x in v),
            "is_undef": lambda x: x is None, "is_num": num,
            "is_bool": lambda x: isinstance(x, bool), "is_string": lambda x: isinstance(x, str),
            "is_list": lambda x: isinstance(x, list),
        }
        try:
            return table[n](*a)
        except Exception:
            return None

    def run(self, prog, overrides=None):
        overrides = overrides or {}
        env = dict(self.glob)
        for s in prog:
            if s[0] == "assign":
                env[s[2]] = overrides[s[2]] if s[2] in overrides else self.ev(s[3], env)
        self.glob.update(env)
        results = []
        for s in prog:
            if s[0] == "inst" and s[2] == "assert":
                ok = truthy(self.ev(s[3][0][1], env))
                msg = self.ev(s[3][1][1], env) if len(s[3]) > 1 else ""
                results.append((s[1], ok, msg))
        return env, results




# ---------------------------------------------------------------- clearances
def rrect_sdf(px, py, x0, x1, y0, y1, r):
    """signed distance to a rounded rectangle (negative inside)"""
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    qx, qy = abs(px - cx) - hx, abs(py - cy) - hy
    outside = math.hypot(max(qx, 0), max(qy, 0))
    return outside + min(max(qx, qy), 0) - r


def rect_dist(px, py, x0, x1, y0, y1):
    """distance from a point to an axis-aligned rectangle (0 inside)"""
    dx = max(x0 - px, 0, px - x1)
    dy = max(y0 - py, 0, py - y1)
    return math.hypot(dx, dy)


def rect_rect(a, b):
    """gap between two axis-aligned rectangles (x0, x1, y0, y1); <0 = overlap depth"""
    gx = max(b[0] - a[1], a[0] - b[1])
    gy = max(b[2] - a[3], a[2] - b[3])
    if gx < 0 and gy < 0:
        return max(gx, gy)
    return math.hypot(max(gx, 0), max(gy, 0))


def clearances(G):
    rows = []
    g = G.get
    d2r = math.pi / 180

    def add(name, val, lo=None, hi=None, note="", warn_only=False):
        st = "ok"
        if (lo is not None and val < lo - 1e-9) or (hi is not None and val > hi + 1e-9):
            st = "WARN" if warn_only else "FAIL"
        rows.append((name, val, lo, hi, st, note))

    ctrl = g("ctrl")
    MW = g("min_wall")
    # ------------------------------------------------------------ stack, depth, glass
    add("Total depth (front face .. back wall outer)", g("back_out_Z"), hi=23.0, note="target <= 23")
    add("Glass front Z set by the Waveshare bosses", g("ws_glass_front_Z"), -0.001, 0.001, "boss face Z 6.0 - 6.0 (drawing)")
    add("Glass front Z set by the pocket ledge", g("ledge_glass_front_Z"), -0.001, 0.001, "0.9 - gasket 0.2 - lens 0.7")
    add("Glass to pocket wall (per side)", g("lens_clr"), 0.1, note="parameter lens_clr")
    add("Glass resting on the ledge (min side)", g("ledge_min"), 1.0, note="adhesive land; left side notched at USB/part", warn_only=True)
    add("Ledge window over VA (min)", min(g("va_x0") - g("win_x0"), g("win_x1") - g("va_x1"),
                                          g("va_y0") - g("win_y0"), g("win_y1") - g("va_y1")), 0.3,
        note="ledge window clears the viewing area")
    t = g("ins_tilt") * d2r
    add("Left-edge part inside the pocket at insertion tilt", -g("nb_over") * math.cos(t) + g("nb_conn_Z0") * math.sin(t), 0.2,
        note=f"part {g('nb_over'):.2f} past the glass; tilt {g('ins_tilt')} deg (TBV part)")
    add("Waveshare screw head to adapter PCB (Z)", g("idc_Z0") + 2.5 - g("ws_bridge_Z1") - g("ws_screw_head_h"), 0.3)
    j8 = (g("j8_col_x")[0] - 1.27, g("j8_col_x")[1] + 1.27, g("j8_row_y0") - 1.27,
          g("j8_row_y0") + (g("j8_rows") - 1) * 2.54 + 1.27)
    add("Waveshare boss to J8 header plastic (XY)", min(rect_dist(h[0], h[1], *j8) for h in g("ws_holes")) - g("ws_boss_d") / 2, 0.3)
    add("Waveshare PCB to control PCB (Y gap)", g("ws_pcb_y0") - g("cp_y1"), 1.0)
    # ------------------------------------------------------------ walls
    add("Side / bottom wall", g("wall"), MW)
    add("Top wall", g("wall_top"), MW)
    add("Front panel", g("panel_t"), MW)
    add("Ledge behind the glass (panel - pocket)", g("panel_t") - g("pocket_Z1"), MW)
    add("Back wall", g("back_t"), MW)
    add("Skin outside the glass pocket (min X/Y)", min(g("pk_x0") - g("case_x0"), g("case_x1") - g("pk_x1"),
                                                       g("case_y1") - g("pk_y1")), MW)
    add("Key-plate rail ledge", g("wall_top") - g("kp_tongue_t") - g("kp_clr"), MW)
    add("USB frame / duct walls", min(g("usb_floor_t"), g("usb_duct_t")), MW)
    add("Top insert boss wall", g("insert_wall"), MW)
    add("Top insert hole to outer skin", min(-rrect_sdf(p[0], p[1], g("case_x0"), g("case_x1"), g("case_y0"), g("case_y1"), g("case_r"))
                                             for p in g("top_boss_xy")) - g("insert_hole_d") / 2, MW - 0.001)
    add("Control-PCB insert boss wall", g("cp_boss_wall"), MW)
    add("Back-shell screw column wall", g("back_column_wall"), MW)
    add("Right wall behind the SIDE flange", g("side_wall_left"), MW - 0.001)
    add("Panel behind control-PCB insert pilot", g("cp_top_Z") - g("insert_len") - g("pilot_len") - g("panel_t"), 0.5,
        note="pilot ends inside the boss, panel intact")
    add("MENU hole to glass pocket (skin)", g("pk_y0") - (g("menu_cap_xy")[1] + g("menu_hole_d") / 2), MW)
    # ------------------------------------------------------------ knob, encoder
    kx, ky = g("enc_xy")
    kr = g("knob_d") / 2
    add("Knob protrusion above the front face", g("knob_protrusion"), 14.0, 18.0, "brief ~16")
    add("Knob shaft engagement (<= bore 13)", g("knob_engage"), 10.0, g("knob_bore_depth"))
    add("Bushing thread beyond washer + nut", g("enc_bush_len") - g("panel_t") - g("enc_washer_t") - g("enc_nut_t"), 0.3)
    add("Nut top to counterbore ceiling (Z)", g("nut_top_Z") - g("knob_cb_top_Z"), 0.3)
    add("Bushing tip to counterbore ceiling (Z)", g("bush_tip_Z") - g("knob_cb_top_Z"), 0.3)
    add("Nut corners to counterbore wall (radial)", (g("knob_cb_d") - g("nut_ac")) / 2, 0.3)
    add("Washer to counterbore wall (radial)", (g("knob_cb_d") - g("enc_washer_d")) / 2, 0.3)
    add("Knob to case right face (X)", g("case_x1") - (kx + kr), 2.0)
    add("Knob to case bottom face (Y)", ky - kr - g("case_y0"), 2.0)
    add("Knob to MENU hole edge", math.dist(g("enc_xy"), g("menu_cap_xy")) - kr - g("menu_hole_d") / 2, 3.0)
    add("MENU hole to encoder hole (panel web)", math.dist(g("enc_xy"), g("menu_cap_xy")) - g("enc_hole_d") / 2 - g("menu_hole_d") / 2, 1.0)
    add("Set screw above the shaft tip (on the flat?)", (g("knob_top_Z") + g("knob_setscrew_from_top")) - g("shaft_tip_Z"),
        0.5, g("enc_flat_len") - 0.5, "TBV flat length", warn_only=True)
    # ------------------------------------------------------------ MENU, SIDE, keys
    mcx, mcy = g("menu_cap_xy")
    h3x, h3y = g("menu_h3")
    add("MENU cap axis offset from the B3F-1000 plunger", math.hypot(*g("menu_nub_off")), hi=2.0,
        note="cap moved +Y so its flange clears the H3 boss; nub stays on the plunger", warn_only=True)
    add("MENU nub length (flange rear .. plunger - gap)", -g("menu_contact_local"), 0.3, 3.0,
        note=f"plunger top Z {g('menu_plunger_Z'):.2f} (4.3 above the PCB)")
    add("MENU flange flat to H3 insert boss", math.hypot(mcx - h3x, mcy - h3y) - g("cp_boss_d") / 2 - g("menu_fl_flat"), 0.3)
    add("MENU flange retention beyond the hole (min, flat side)", g("menu_fl_flat") - g("menu_hole_d") / 2, 0.5)
    add("MENU hole cut to H3 insert boss (XY)", math.hypot(mcx - h3x, mcy - h3y) - g("cp_boss_d") / 2 - g("menu_hole_d") / 2, 0.3,
        note="boss starts behind the panel; the cut runs to Z 3.0, so it must not nick the boss")
    add("MENU flange to switch body (travel room)", (g("cp_top_Z") - g("menu_sw_body_h")) - (g("panel_t") + g("menu_flange_t")), 0.8)
    add("SIDE flange thickness", g("side_flange_t"), 0.6, note=f"tip X {g('side_tip_x'):.2f}; 0.8 needs tip <= "
        f"{g('case_x1') - MW - g('side_flange_max') - g('side_gap'):.2f}", warn_only=True)
    add("SIDE pre-travel gap", g("side_gap"), 0.2, 0.3)
    add("SIDE flange to panel / control PCB", min(g("side_Z") - g("side_flange_d") / 2 - g("panel_t"),
                                                  g("cp_top_Z") - g("side_Z") - g("side_flange_d") / 2), 0.1)
    add("Key pin inner extension", g("key_inner_ext"), 0.5)
    # ------------------------------------------------------------ USB
    add("USB duct to panel inner face (Z)", g("usb_Z") - g("usb_duct_hh") - g("panel_t"), g("usb_margin"))
    add("USB duct to parting plane (Z)", g("Z_split") - (g("usb_Z") + g("usb_duct_hh")), g("usb_margin"))
    add("USB frame/duct to left-edge part", min(g("usb_y") - g("usb_frame_hw") - g("nb_conn_y1"),
                                                g("nb_conn_x0") - g("usb_floor_x0")), g("usb_margin"))
    add("USB shell in the 9.5 x 3.6 cut (Y / Z, min)", min((g("usb_cut_w") - g("usb_shell_w")) / 2,
                                                           (g("usb_cut_h") - g("usb_shell_h")) / 2), 0.15, note="+-0.3 TBV on usb_Z")
    # ------------------------------------------------------------ control PCB and mount holes
    add("Control PCB to cavity wall (min)", min(g("cp_x0") - g("cav_x0"), g("cav_x1") - g("cp_x1"), g("cp_y0") - g("cav_y0")), 0.5)
    hdr = (g("cp_hdr_xc") - g("cp_hdr_len") / 2, g("cp_hdr_xc") + g("cp_hdr_len") / 2,
           g("cp_hdr_yc") - g("cp_hdr_w") / 2, g("cp_hdr_yc") + g("cp_hdr_w") / 2)
    add("Shrouded header to cavity wall (X)", min(hdr[0] - g("cav_x0"), g("cav_x1") - hdr[1]), 0.2,
        note=f"overhangs the PCB by {max(g('cp_x0') - hdr[0], hdr[1] - g('cp_x1')):.2f}")
    ex, ey = g("enc_xy")
    enc = (ex - g("enc_body_w") / 2, ex + g("enc_body_w") / 2, ey - g("enc_body_l") / 2, ey + g("enc_body_l") / 2)
    mx, my = g("menu_xy")
    menu = (mx - g("menu_sw") / 2, mx + g("menu_sw") / 2, my - g("menu_sw") / 2, my + g("menu_sw") / 2)
    side = (g("side_tip_x") - g("side_act_len") - g("side_body_d"), g("side_tip_x"),
            g("side_y") - g("side_body_w") / 2, g("side_y") + g("side_body_w") / 2)
    cx, cy = g("cur_xy")
    if ctrl == "J":
        cur = [(cx - g("thb_frame_w") / 2, cx + g("thb_frame_w") / 2, cy - g("thb_frame_l") / 2, cy + g("thb_frame_l") / 2)]
    else:
        L = g("dp_span") / 2 + g("dp_lip")
        A = g("dp_arm") / 2 + g("dp_lip")
        cur = [(cx - L, cx + L, cy - A, cy + A), (cx - A, cx + A, cy - L, cy + L)]
    rb = g("cp_boss_d") / 2
    for i, (hx, hy) in enumerate(g("mount_holes")):
        parts = [enc, menu, side] + cur
        d = min(rect_dist(hx, hy, *r) for r in parts) - rb
        add(f"Mount hole {i + 1} ({hx:g},{hy:g}) boss to parts", d, 0.3, note="encoder/MENU/SIDE/cursor envelopes")
        add(f"Mount hole {i + 1} to control PCB edge", -rrect_sdf(hx, hy, g("cp_x0"), g("cp_x1"), g("cp_y0"), g("cp_y1"), g("cp_r")), 2.0)
        if g("mount_shell")[i]:
            add(f"Mount hole {i + 1} back column to header", rect_dist(hx, hy, *hdr) - g("back_column_d") / 2, 0.3)
    add("Shell screw head recess below back face", g("back_out_Z") - g("shell_head_seat_Z") - g("screw_head_h"), 0.0)
    add("Top screw thread engagement", g("Z_split") - g("top_screw_tip_Z"), 2.5, g("insert_len") + g("pilot_len"))
    add("Third control screw engagement", g("cp_top_Z") - g("cp3_tip_Z"), 2.5, g("insert_len") + g("pilot_len"))
    add("Top boss to Waveshare PCB / keys (Y)", g("top_boss_y") - g("insert_boss_d") / 2 - g("win_y1"), 0.0,
        note="window line = PCB/keys + 0.3")
    add("Top boss to key plate (X)", min(g("kp_x0") - g("kp_tongue") - g("kp_clr") - (p[0] + g("insert_boss_d") / 2)
                                         for p in g("top_boss_xy")[:1]), 0.5)
    # ------------------------------------------------------------ battery, IDC, ribbon
    add("Cell front to Waveshare rear parts (Z)", g("batt_Z0") - (g("ws_pcb_rear_Z") + g("ws_rear_parts_h")), 0.2, note="0.3 foam tape")
    add("Battery bay to IDC socket (X)", g("idc_x0") - g("bay_x1"), 2.0)
    add("Battery bay to Waveshare bosses (XY)", min(rect_dist(h[0], h[1], g("bay_x0"), g("bay_x1"), g("bay_y0"), g("bay_y1"))
                                                    for h in g("ws_holes")) - g("ws_boss_d") / 2, 0.3)
    add("Battery bay to control header (Y)", g("bay_y0") - hdr[3], 1.0)
    add("Ribbon stack top to back wall inner (Z)", g("back_in_Z") - g("rib_Z1"), 0.0, note=f"{g('ribbon_layers')} x {g('ribbon_t')} behind the cell")
    add("IDC fold to back wall inner (Z)", g("back_in_Z") - g("idc_fold_Z"), 0.3)
    add("Control socket + ribbon to back wall (Z)", g("back_in_Z") - g("cp_hdr_Z1") - g("ribbon_t"), 0.3)
    add("Ribbon width vs control header length", g("cp_hdr_len") - g("ribbon_w"), 0.0, note="ribbon 43.2 fits the header span")
    # ------------------------------------------------------------ cursor control
    if ctrl == "J":
        hr = g("thb_hole_d") / 2
        add("J: panel opening Dia (computed)", g("thb_hole_d"), g("thb_dome_d") + 0.6, g("thb_hole_fallback"),
            note=f"fallback {g('thb_hole_fallback')}")
        add("J: shaft envelope at 26 deg to opening (radial)", hr - g("thb_env_r"), 0.3)
        add("J: dome to opening (radial)", hr - g("thb_dome_d") / 2, 0.3)
        add("J: THB frame top to panel inner face", (g("cp_top_Z") - g("thb_frame_h")) - g("panel_t"), 0.2, note="frame height TBV")
        add("J: stick top above the front face", -g("thb_top_Z"), note="info: no cap; <= 18 allowed", hi=18.0)
        add("J: opening to left case face (skin)", cx - hr - g("case_x0"), MW)
        add("J: MENU hole to stick opening (panel web)", math.dist((cx, cy), g("menu_cap_xy")) - hr - g("menu_hole_d") / 2, 1.0)
        tip = (g("thb_h_top") - g("thb_pivot_h")) * math.tan(g("thb_tilt") * d2r) + g("thb_shaft_d") / 2
        add("J: tilted stick tip to knob edge (X)", (kx - kr) - (cx + tip), 3.0)
    else:
        a_act = g("dp_act_deg") * d2r
        a_max = g("dp_max_deg") * d2r
        Zp = g("skqu_pivot_Z")
        add("D: actuation tilt (deg)", g("dp_act_deg"), note="Alps: 0.4 travel 2.9 below the top, pivot 1.23 above the PCB")
        gap = g("dp_fl_Z0") - g("panel_t")
        r = g("dp_fl_r")
        rise = lambda a: r * math.sin(a) - (Zp - g("panel_t")) * (1 - math.cos(a))
        add("D: flange gap - rise at actuation", gap - rise(a_act), 0.1, 0.3, "flange is the over-travel stop")
        add("D: flange stops tilt before +2 deg overtravel", rise(a_max) - gap, 0.0)
        add("D: flange gap vs centre push travel", gap - g("skqu_push"), 0.2)
        add("D: cross side shift at front face vs clearance", g("dp_clr") - g("dp_lat_front"), 0.0,
            note=f"shift {g('dp_lat_front'):.2f} at actuation vs dp_clr {g('dp_clr')} (TBV pivot)", warn_only=True)
        rc = g("skqu_body_w") / 2 * math.sqrt(2)
        def back_move(Zpt, rr, a):
            aa = Zp - Zpt
            return aa * (1 - math.cos(a)) + rr * math.sin(a)
        add("D: cap rear (r 7.07, body corners) to switch body at max tilt",
            g("skqu_body_Z0") - (g("dp_fl_Z0") + back_move(g("dp_fl_Z0"), rc, a_max)), 0.05)
        add("D: cap rear to collar top at max tilt + centre push",
            g("skqu_collar_Z") - (g("dp_sock_Z1") + back_move(g("dp_sock_Z1"), g("dp_sock_od") / 2, a_max) + g("skqu_push")), 0.05,
            note="assumes the collar is fixed; if it moves with the stem this is pure margin")
        add("D: socket depth over the stem", g("dp_sock_Z1") - g("skqu_top_Z"), 2.0, note="square 3.2 stem, press fit + glue")
        add("D: socket corner wall (arm half-width - socket half-diagonal)",
            g("dp_arm") / 2 - (g("skqu_stem_w") + g("dp_sock_fit")) / 2 * math.sqrt(2), 0.8)
        add("D: top-side part height limit under the flange", g("cp_top_Z") - (g("dp_fl_Z1") + back_move(g("dp_fl_Z1"), r, a_max)),
            note="keep-out height for the control PCB under the cross + lip")
        add("D: cross opening to left case face (skin)", (cx - g("dp_span") / 2 - g("dp_clr")) - g("case_x0"), MW)
        L2 = g("dp_span") / 2 + g("dp_clr")
        A2 = g("dp_arm") / 2 + g("dp_clr")
        add("D: MENU hole to cross opening (panel web)",
            min(rect_dist(mcx, mcy, cx - L2, cx + L2, cy - A2, cy + A2), rect_dist(mcx, mcy, cx - A2, cx + A2, cy - L2, cy + L2))
            - g("menu_hole_d") / 2, 1.0)
        add("D: flange lip left (clipped at the wall)", (cx - g("dp_span") / 2) - (g("cav_x0") + 0.3), 0.2, warn_only=True)
        add("D: pressed arm tip behind the front face", g("dp_sink"), note="info: tip sinks this far at actuation")
        add("D: cap length (top .. socket rear)", g("dp_cap_len"))
    return rows


# ---------------------------------------------------------------- main
def run_variant(prog, v):
    print(f"\n######## ctrl = \"{v}\"")
    n_err = 0
    ev = Evaluator(prog)
    try:
        G, asserts = ev.run(prog, {"ctrl": v})
    except Exception as ex:
        print(f"[4] evaluation: ABORTED ({type(ex).__name__}: {ex})")
        return 1, None
    undef = [n for s in prog if s[0] == "assign" for n in [s[2]] if G.get(n) is None]
    print(f"[4] evaluation: {len(G)} values, {len(undef)} undef {undef if undef else ''}")
    n_err += len(undef)
    bad = [a for a in asserts if not a[1]]
    print(f"[5] top-level assert(): {len(asserts) - len(bad)}/{len(asserts)} pass")
    for line, ok, msg in bad:
        print(f"     FAIL line {line}: {msg}")
    n_err += len(bad)
    print("[6] key clearances (mm)")
    rows = clearances(G)
    w = max(len(r[0]) for r in rows)
    for name, val, lo, hi, st, note in rows:
        lim = ""
        if lo is not None and hi is not None:
            lim = f"[{lo:g}..{hi:g}]"
        elif lo is not None:
            lim = f">= {lo:g}"
        elif hi is not None:
            lim = f"<= {hi:g}"
        print(f"    {name:<{w}}  {val:8.2f}  {lim:<12} {st:<4} {note}")
    fails = [r for r in rows if r[4] == "FAIL"]
    warns = [r for r in rows if r[4] == "WARN"]
    n_err += len(fails)
    print(f"== ctrl {v}: {'PASS' if n_err == 0 else 'FAIL'}: {n_err} error(s), {len(fails)} clearance fail(s), "
          f"{len(warns)} warning(s)")
    return n_err, G


def main():
    args = sys.argv[1:]
    path = next((a for a in args if a.endswith(".scad")), __file__.replace("validate_case.py", "bead_case.scad"))
    variants = [a for a in args if a in ("J", "D")] or ["J", "D"]
    src = open(path, encoding="utf-8").read()
    n_err = 0
    print(f"== {path}")
    bal = check_balance(src)
    print(f"[1] bracket balance: {'OK' if not bal else 'ERRORS'}")
    for b in bal:
        print("    ", b)
    n_err += len(bal)
    try:
        prog = Parser(tokenize(src)).program()
    except SyntaxError as ex:
        print(f"[2] parse: SYNTAX ERROR {ex}")
        return 1
    n_mod = sum(1 for s in prog if s[0] == "module")
    n_fun = sum(1 for s in prog if s[0] == "function")
    n_asg = sum(1 for s in prog if s[0] == "assign")
    print(f"[2] parse: OK ({n_asg} top-level assignments, {n_mod} modules, {n_fun} functions)")
    ch = Checker(prog)
    ch.run()
    print(f"[3] scope/identifier check: {'OK' if not ch.errors else 'ERRORS'} ({len(ch.warnings)} warnings)")
    for x in ch.errors:
        print("     ERROR", x)
    for x in ch.warnings:
        print("     warn ", x)
    n_err += len(ch.errors)
    for v in variants:
        e, _ = run_variant(prog, v)
        n_err += e
    print(f"\n== OVERALL {'PASS' if n_err == 0 else 'FAIL'}: {n_err} error(s) across {len(variants)} variant(s)")
    return 0 if n_err == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
