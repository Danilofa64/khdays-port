"""The type pass: every expression of a preprocessed translation unit given
its C type, for the emission (emit.py).

What it models is what the game's C uses: file, function and block scopes;
typedefs; struct, union and enum tags (anonymous members included);
prototypes, unprototyped and implicit declarations; arrays, functions and
their decay; pointer arithmetic; the conditional operator's pointer rules;
string and compound literals; initializers. Arithmetic types are kept only
as far as the rewrite needs them (an arithmetic value is never a pointer).
An expression it cannot type stops it (TypeFailure) naming the file, the line
and the construct: nothing is rewritten by a guess.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from pycparser import c_ast

# --- types ----------------------------------------------------------------------


class CType:
    quals: tuple = ()

    def unqualified(self) -> "CType":
        return self

    def is_pointer(self) -> bool:
        return isinstance(self, Pointer)

    def is_array(self) -> bool:
        return isinstance(self, Array)

    def is_function(self) -> bool:
        return isinstance(self, Function)

    def is_record(self) -> bool:
        return isinstance(self, Record)

    def is_arithmetic(self) -> bool:
        return isinstance(self, (Arithmetic, Enumeration))


@dataclass(eq=False)
class Arithmetic(CType):
    """An integer or floating type, by its C spelling (`unsigned int`...)."""
    spelling: str
    quals: tuple = ()
    floating: bool = False

    def qualified(self, quals) -> "Arithmetic":
        return Arithmetic(self.spelling, tuple(quals), self.floating)


@dataclass(eq=False)
class Void(CType):
    quals: tuple = ()

    def qualified(self, quals) -> "Void":
        return Void(tuple(quals))


@dataclass(eq=False)
class Pointer(CType):
    target: CType
    quals: tuple = ()

    def qualified(self, quals) -> "Pointer":
        return Pointer(self.target, tuple(quals))


@dataclass(eq=False)
class Array(CType):
    element: CType
    length: object = None  # the dimension's AST (or None)
    quals: tuple = ()

    def qualified(self, quals) -> "Array":
        return Array(self.element, self.length, tuple(quals))


@dataclass(eq=False)
class Function(CType):
    result: CType
    params: list | None  # None: unprototyped
    variadic: bool = False
    quals: tuple = ()

    def qualified(self, quals) -> "Function":
        return self


@dataclass(eq=False)
class RecordDefinition:
    """One struct or union tag: shared by every type that names it."""
    kind: str  # 'struct' or 'union'
    tag: str | None
    fields: dict[str, CType] | None = None  # None: incomplete
    order: list[str] = field(default_factory=list)
    spelling: str | None = None  # how C names it: `struct Tag`, or a typedef's name
    node: object = None  # its definition's syntax (an anonymous one is written out again where named)


@dataclass(eq=False)
class Record(CType):
    definition: RecordDefinition
    quals: tuple = ()

    def qualified(self, quals) -> "Record":
        return Record(self.definition, tuple(quals))

    def field_type(self, name: str) -> CType | None:
        fields = self.definition.fields
        return None if fields is None else fields.get(name)


@dataclass(eq=False)
class Enumeration(CType):
    tag: str | None
    quals: tuple = ()
    spelling: str | None = None

    def qualified(self, quals) -> "Enumeration":
        return Enumeration(self.tag, tuple(quals), self.spelling)


INT = Arithmetic("int")
UNSIGNED = Arithmetic("unsigned int")
DOUBLE = Arithmetic("double", floating=True)
CHAR = Arithmetic("char")

QUAL_WORDS = ("const", "volatile", "restrict")


def qualify(t: CType, quals) -> CType:
    quals = tuple(q for q in quals if q in QUAL_WORDS)
    if not quals:
        return t
    return t.qualified(tuple(dict.fromkeys(tuple(t.quals) + quals)))


def decayed(t: CType) -> CType:
    """An array's or a function's type as a value."""
    if isinstance(t, Array):
        return Pointer(t.element)
    if isinstance(t, Function):
        return Pointer(t)
    return t


ARITHMETIC_WORDS = {
    ("void",): None,
    ("char",): "char", ("signed", "char"): "signed char", ("unsigned", "char"): "unsigned char",
    ("short",): "short", ("short", "int"): "short", ("signed", "short"): "short",
    ("signed", "short", "int"): "short", ("unsigned", "short"): "unsigned short",
    ("unsigned", "short", "int"): "unsigned short",
    ("int",): "int", ("signed",): "int", ("signed", "int"): "int",
    ("unsigned",): "unsigned int", ("unsigned", "int"): "unsigned int",
    ("long",): "long", ("long", "int"): "long", ("signed", "long"): "long", ("signed", "long", "int"): "long",
    ("unsigned", "long"): "unsigned long", ("unsigned", "long", "int"): "unsigned long",
    ("long", "long"): "long long", ("long", "long", "int"): "long long", ("signed", "long", "long"): "long long",
    ("signed", "long", "long", "int"): "long long",
    ("unsigned", "long", "long"): "unsigned long long", ("unsigned", "long", "long", "int"): "unsigned long long",
    ("float",): "float", ("double",): "double", ("long", "double"): "long double",
    ("_Bool",): "_Bool",
}


# --- failures ---------------------------------------------------------------------

class TypeFailure(Exception):
    def __init__(self, node, message: str):
        coord = getattr(node, "coord", None)
        where = f"{coord.file}:{coord.line}" if coord is not None else "?"
        super().__init__(f"{where}: {message}")
        self.where = where
        self.message = message


# --- scopes -------------------------------------------------------------------------

@dataclass(eq=False)
class Symbol:
    name: str
    type: CType
    kind: str  # 'object', 'function', 'typedef', 'enumerator'
    storage: tuple = ()
    file_scope: bool = False
    node: object = None
    previous: "Symbol | None" = None  # an enumerator: the one before it (its value is that + 1 by default)


class Scope:
    def __init__(self, parent: "Scope | None"):
        self.parent = parent
        self.ordinary: dict[str, Symbol] = {}
        self.tags: dict[str, CType] = {}

    def find(self, name: str) -> Symbol | None:
        s = self
        while s is not None:
            if name in s.ordinary:
                return s.ordinary[name]
            s = s.parent
        return None

    def find_tag(self, name: str):
        s = self
        while s is not None:
            if name in s.tags:
                return s.tags[name]
            s = s.parent
        return None


# --- the pass ---------------------------------------------------------------------------

class TypePass(c_ast.NodeVisitor):
    """Gives every expression node its type (self.types: id(node) -> CType)
    and every identifier its symbol (self.symbols: id(ID node) -> Symbol)."""

    def __init__(self):
        self.types: dict[int, CType] = {}
        self.symbols: dict[int, Symbol] = {}
        self.file = Scope(None)
        self.scope = self.file
        self.function: Function | None = None
        self.implicit: list[str] = []  # functions called without a declaration
        self.nodes: dict[int, object] = {}  # keeps the typed nodes alive (their ids stable)
        self.init_targets: dict[int, CType] = {}  # an initializer element: the type it initializes
        self.declared: dict[int, Symbol] = {}  # a declaration (Decl node) -> the symbol it declares

    # --- declarations ---

    def type_of_declarator(self, node, base_quals=()) -> CType:
        """The type a declarator chain (TypeDecl/PtrDecl/ArrayDecl/FuncDecl/Typename) names."""
        if isinstance(node, c_ast.Typename):
            return self.type_of_declarator(node.type)
        if isinstance(node, c_ast.TypeDecl):
            return qualify(self.base_type(node.type, node), node.quals)
        if isinstance(node, c_ast.PtrDecl):
            return Pointer(self.type_of_declarator(node.type), tuple(q for q in node.quals if q in QUAL_WORDS))
        if isinstance(node, c_ast.ArrayDecl):
            element = self.type_of_declarator(node.type)
            if node.dim is not None:
                self.expression(node.dim)
            return Array(element, node.dim)
        if isinstance(node, c_ast.FuncDecl):
            result = self.type_of_declarator(node.type)
            params, variadic = self.parameters(node.args)
            return Function(result, params, variadic)
        if isinstance(node, (c_ast.Struct, c_ast.Union, c_ast.Enum)):
            # an anonymous member's or a bare tag's type, with no declarator
            return self.base_type(node, node)
        raise TypeFailure(node, f"a declarator not modelled: {type(node).__name__}")

    def parameters(self, args) -> tuple[list | None, bool]:
        if args is None:
            return None, False
        params, variadic = [], False
        for p in args.params:
            if isinstance(p, c_ast.EllipsisParam):
                variadic = True
                continue
            if isinstance(p, c_ast.ID):  # K&R identifier list
                return None, False
            t = self.type_of_declarator(p.type)
            if isinstance(t, Void) and len(args.params) == 1:
                return [], False
            params.append(self.adjusted_parameter(t))
        return params, variadic

    @staticmethod
    def adjusted_parameter(t: CType) -> CType:
        """A parameter of array or function type is a pointer (C17 6.7.6.3)."""
        if isinstance(t, Array):
            return Pointer(t.element, t.quals)
        if isinstance(t, Function):
            return Pointer(t)
        return t

    def base_type(self, node, where) -> CType:
        if isinstance(node, c_ast.IdentifierType):
            names = tuple(n for n in node.names if n not in ("signed",) or len(node.names) > 1)
            words = tuple(node.names)
            if len(words) == 1 and words[0] not in ("void", "char", "short", "int", "long", "float", "double",
                                                    "signed", "unsigned", "_Bool"):
                s = self.scope.find(words[0])
                if s is None or s.kind != "typedef":
                    raise TypeFailure(where, f"an unknown type name {words[0]!r}")
                return s.type
            key = tuple(sorted(words, key=lambda w: ("signed", "unsigned", "short", "long", "char", "int",
                                                      "float", "double", "void", "_Bool").index(w)
                               if w in ("signed", "unsigned", "short", "long", "char", "int", "float", "double",
                                        "void", "_Bool") else 99))
            if words == ("void",):
                return Void()
            spelling = ARITHMETIC_WORDS.get(key) or ARITHMETIC_WORDS.get(words)
            if spelling is None:
                raise TypeFailure(where, f"a type not modelled: {' '.join(words)}")
            del names
            return Arithmetic(spelling, (), spelling in ("float", "double", "long double"))
        if isinstance(node, (c_ast.Struct, c_ast.Union)):
            return self.record(node)
        if isinstance(node, c_ast.Enum):
            return self.enumeration(node)
        raise TypeFailure(where, f"a base type not modelled: {type(node).__name__}")

    def record(self, node) -> Record:
        kind = "struct" if isinstance(node, c_ast.Struct) else "union"
        if node.name is not None:
            found = None
            if node.decls is None:
                found = self.scope.find_tag(node.name)
            else:
                found = self.scope.tags.get(node.name)
            if isinstance(found, Record) and found.definition.kind == kind:
                definition = found.definition
            else:
                definition = RecordDefinition(kind, node.name, spelling=f"{kind} {node.name}")
                self.scope.tags[node.name] = Record(definition)
        else:
            definition = RecordDefinition(kind, None)
        if node.decls is not None:
            fields: dict[str, CType] = {}
            order: list[str] = []
            for d in node.decls:
                if isinstance(d, c_ast.StaticAssert):
                    continue
                t = self.type_of_declarator(d.type)
                if d.bitsize is not None:
                    self.expression(d.bitsize)
                if d.name is None:
                    # an anonymous struct or union member: its fields are the record's
                    if isinstance(t, Record) and t.definition.fields is not None:
                        for n in t.definition.order:
                            fields[n] = t.definition.fields[n]
                            order.append(n)
                    continue
                fields[d.name] = t
                order.append(d.name)
            definition.fields = fields
            definition.order = order
            definition.node = node
        return Record(definition)

    def enumeration(self, node) -> Enumeration:
        if node.values is not None:
            previous = None
            for e in node.values.enumerators:
                if e.value is not None:
                    self.expression(e.value)
                previous = Symbol(e.name, INT, "enumerator", node=e, previous=previous)
                self.scope.ordinary[e.name] = previous
            if node.name is not None:
                self.scope.tags[node.name] = Enumeration(node.name, spelling=f"enum {node.name}")
        return Enumeration(node.name, spelling=f"enum {node.name}" if node.name else None)

    def declare(self, node: c_ast.Decl) -> Symbol | None:
        if node.name is None:  # a tag's declaration, `struct X { ... };`
            self.type_of_declarator(node.type) if not isinstance(node.type, (c_ast.Struct, c_ast.Union,
                                                                              c_ast.Enum)) \
                else self.base_type(node.type, node)
            return None
        t = self.type_of_declarator(node.type)
        kind = "function" if isinstance(t, Function) else "object"
        previous = self.scope.ordinary.get(node.name)
        if isinstance(t, Array) and t.length is None and previous is not None and isinstance(previous.type, Array):
            t = previous.type if previous.type.length is not None else t
        storage = tuple(node.storage)
        if previous is not None and previous.kind == kind and "static" in previous.storage and \
                "static" not in storage and (self.scope is self.file or "extern" in storage):
            storage += ("static",)  # a later declaration keeps the internal linkage (C11 6.2.2p4)
        symbol = Symbol(node.name, t, kind, storage, self.scope is self.file, node)
        if "extern" in node.storage and self.scope is not self.file:
            outer = self.file.ordinary.get(node.name)
            if outer is not None:
                symbol = outer
        self.scope.ordinary[node.name] = symbol
        self.declared[id(node)] = symbol
        self.nodes[id(node)] = node
        if node.init is not None:
            self.initializer(node.init, t)
        return symbol

    def visit_Typedef(self, node: c_ast.Typedef):
        t = self.type_of_declarator(node.type)
        if isinstance(t, Record) and t.definition.spelling is None:
            t.definition.spelling = node.name
        if isinstance(t, Enumeration) and t.spelling is None:
            t = Enumeration(t.tag, t.quals, node.name)
        self.scope.ordinary[node.name] = Symbol(node.name, t, "typedef")

    def visit_Decl(self, node: c_ast.Decl):
        self.declare(node)

    def visit_DeclList(self, node: c_ast.DeclList):
        for d in node.decls:
            self.visit(d)

    def visit_FuncDef(self, node: c_ast.FuncDef):
        decl = node.decl
        t = self.type_of_declarator(decl.type)
        if not isinstance(t, Function):
            raise TypeFailure(node, "a function definition of a type that is not a function")
        storage = tuple(decl.storage)
        previous = self.file.ordinary.get(decl.name)
        if previous is not None and "static" in previous.storage and "static" not in storage:
            storage += ("static",)  # defined after a static declaration: internal linkage
        outer = Symbol(decl.name, t, "function", storage, True, decl)
        self.file.ordinary[decl.name] = outer
        self.declared[id(decl)] = outer
        self.scope = Scope(self.file)
        saved, self.function = self.function, t
        # the parameters (prototype form, or K&R's declarations)
        args = decl.type.args if isinstance(decl.type, c_ast.FuncDecl) else None
        if args is not None:
            for p in args.params:
                if isinstance(p, c_ast.Decl) and p.name is not None:
                    pt = self.adjusted_parameter(self.type_of_declarator(p.type))
                    self.scope.ordinary[p.name] = Symbol(p.name, pt, "object", (), False, p)
                elif isinstance(p, c_ast.ID):
                    self.scope.ordinary[p.name] = Symbol(p.name, INT, "object")
        for p in node.param_decls or []:
            pt = self.adjusted_parameter(self.type_of_declarator(p.type))
            self.scope.ordinary[p.name] = Symbol(p.name, pt, "object", (), False, p)
        if t.params is None and node.param_decls:
            t.params = [self.scope.ordinary[p.name].type for p in node.param_decls]
        self.scope.ordinary["__func__"] = Symbol("__func__", Array(CHAR), "object")
        self.compound(node.body, new_scope=False)
        self.scope = self.file
        self.function = saved

    # --- statements ---

    def compound(self, node: c_ast.Compound, new_scope=True):
        if new_scope:
            self.scope = Scope(self.scope)
        for item in node.block_items or []:
            self.statement(item)
        if new_scope:
            self.scope = self.scope.parent

    def statement(self, node):
        if node is None:
            return
        if isinstance(node, c_ast.Compound):
            self.compound(node)
        elif isinstance(node, (c_ast.Decl, c_ast.Typedef, c_ast.DeclList)):
            self.visit(node)
        elif isinstance(node, c_ast.If):
            self.expression(node.cond)
            self.statement(node.iftrue)
            self.statement(node.iffalse)
        elif isinstance(node, c_ast.For):
            self.scope = Scope(self.scope)
            if isinstance(node.init, c_ast.DeclList):
                self.visit(node.init)
            elif node.init is not None:
                self.expression(node.init)
            if node.cond is not None:
                self.expression(node.cond)
            if node.next is not None:
                self.expression(node.next)
            self.statement(node.stmt)
            self.scope = self.scope.parent
        elif isinstance(node, (c_ast.While, c_ast.DoWhile)):
            self.expression(node.cond)
            self.statement(node.stmt)
        elif isinstance(node, c_ast.Switch):
            self.expression(node.cond)
            self.statement(node.stmt)
        elif isinstance(node, c_ast.Case):
            self.expression(node.expr)
            for s in node.stmts or []:
                self.statement(s)
        elif isinstance(node, c_ast.Default):
            for s in node.stmts or []:
                self.statement(s)
        elif isinstance(node, c_ast.Label):
            self.statement(node.stmt)
        elif isinstance(node, c_ast.Return):
            if node.expr is not None:
                self.expression(node.expr)
        elif isinstance(node, (c_ast.Goto, c_ast.Break, c_ast.Continue, c_ast.EmptyStatement, c_ast.Pragma,
                               c_ast.StaticAssert)):
            if isinstance(node, c_ast.StaticAssert):
                self.expression(node.cond)
        else:
            self.expression(node)

    # --- initializers ---

    def initializer(self, init, t: CType):
        self.init_targets[id(init)] = t
        self.nodes[id(init)] = init
        if isinstance(init, c_ast.InitList):
            self.types[id(init)] = t
            self.nodes[id(init)] = init
            if isinstance(t, Array):
                for e in init.exprs:
                    if isinstance(e, c_ast.NamedInitializer):
                        self.designated(e, t)
                    else:
                        self.initializer(e, t.element)
            elif isinstance(t, Record) and t.definition.fields is not None:
                order = t.definition.order
                i = 0
                for e in init.exprs:
                    if isinstance(e, c_ast.NamedInitializer):
                        last = self.designated(e, t)
                        i = order.index(last) + 1 if last in order else i
                        continue
                    if t.definition.kind == "union":
                        ft = t.definition.fields[order[0]] if order else INT
                    else:
                        ft = t.definition.fields[order[i]] if i < len(order) else INT
                    self.initializer(e, ft)
                    i += 1
            else:
                for e in init.exprs:
                    self.initializer(e, t)
            return
        self.expression(init)

    def designated(self, node: c_ast.NamedInitializer, t: CType) -> str | None:
        current = t
        last = None
        for d in node.name:
            if isinstance(d, c_ast.ID):
                if not isinstance(current, Record):
                    raise TypeFailure(node, f"a designator .{d.name} on a type with no fields")
                ft = current.field_type(d.name)
                if ft is None:
                    raise TypeFailure(node, f"no field {d.name!r} in {current.definition.spelling}")
                last = d.name if current is t else last
                current = ft
            else:  # [index]
                self.expression(d)
                if not isinstance(current, Array):
                    raise TypeFailure(node, "an index designator on a type that is not an array")
                current = current.element
        self.initializer(node.expr, current)
        return last

    # --- expressions ---

    def note(self, node, t: CType) -> CType:
        self.types[id(node)] = t
        self.nodes[id(node)] = node
        return t

    def expression(self, node) -> CType:
        method = getattr(self, "expr_" + type(node).__name__, None)
        if method is None:
            raise TypeFailure(node, f"an expression not modelled: {type(node).__name__}")
        return self.note(node, method(node))

    def value(self, node) -> CType:
        """The expression's type as a value (arrays and functions decayed)."""
        return decayed(self.expression(node))

    def expr_ID(self, node: c_ast.ID) -> CType:
        s = self.scope.find(node.name)
        if s is None:
            raise TypeFailure(node, f"an undeclared identifier {node.name!r}")
        self.symbols[id(node)] = s
        return s.type

    def expr_Constant(self, node: c_ast.Constant) -> CType:
        if node.type == "string":
            return Array(CHAR)
        if node.type in ("float", "double", "long double"):
            return DOUBLE
        if node.type == "char":
            return INT
        v = node.value.lower()
        if "u" in v and "ll" in v:
            return Arithmetic("unsigned long long")
        if "ll" in v:
            return Arithmetic("long long")
        if "u" in v:
            return UNSIGNED
        return INT

    def expr_UnaryOp(self, node: c_ast.UnaryOp) -> CType:
        op = node.op
        if op == "sizeof" or op == "_Alignof":
            if isinstance(node.expr, c_ast.Typename):
                self.type_of_declarator(node.expr)
            else:
                self.expression(node.expr)
            return UNSIGNED
        if op == "&":
            t = self.expression(node.expr)
            return Pointer(t)
        if op == "*":
            t = self.value(node.expr)
            if not isinstance(t, Pointer):
                raise TypeFailure(node, "a dereference of a value that is not a pointer")
            return t.target
        t = self.value(node.expr)
        if op in ("p++", "p--", "++", "--"):
            return t
        if op == "!":
            return INT
        if op in ("-", "+", "~"):
            if isinstance(t, Pointer):
                raise TypeFailure(node, f"unary {op} on a pointer")
            return t if isinstance(t, Arithmetic) and t.floating else promoted(t)
        raise TypeFailure(node, f"a unary operator not modelled: {op}")

    def expr_Cast(self, node: c_ast.Cast) -> CType:
        target = self.type_of_declarator(node.to_type)
        self.value(node.expr)
        return target

    def expr_BinaryOp(self, node: c_ast.BinaryOp) -> CType:
        op = node.op
        a, b = self.value(node.left), self.value(node.right)
        if op in ("&&", "||", "<", ">", "<=", ">=", "==", "!="):
            return INT
        if op == "+":
            if isinstance(a, Pointer):
                return a
            if isinstance(b, Pointer):
                return b
            return arithmetic_result(a, b)
        if op == "-":
            if isinstance(a, Pointer) and isinstance(b, Pointer):
                return INT
            if isinstance(a, Pointer):
                return a
            return arithmetic_result(a, b)
        if op in ("*", "/", "%", "&", "|", "^"):
            return arithmetic_result(a, b)
        if op in ("<<", ">>"):
            return promoted(a)
        raise TypeFailure(node, f"a binary operator not modelled: {op}")

    def expr_Assignment(self, node: c_ast.Assignment) -> CType:
        t = self.expression(node.lvalue)
        self.value(node.rvalue)
        return t

    def expr_TernaryOp(self, node: c_ast.TernaryOp) -> CType:
        self.value(node.cond)
        a, b = self.value(node.iftrue), self.value(node.iffalse)
        if isinstance(a, Pointer) and isinstance(b, Pointer):
            return a if not isinstance(a.target, Void) else b
        if isinstance(a, Pointer):
            return a
        if isinstance(b, Pointer):
            return b
        if isinstance(a, Void) or isinstance(b, Void):
            return Void()
        if isinstance(a, Record):
            return a
        return arithmetic_result(a, b)

    def expr_FuncCall(self, node: c_ast.FuncCall) -> CType:
        callee = node.name
        if isinstance(callee, c_ast.ID) and self.scope.find(callee.name) is None:
            # an implicit declaration: `int name()` at file scope (C89; mwcc accepts it)
            implicit = Function(INT, None)
            self.file.ordinary[callee.name] = Symbol(callee.name, implicit, "function", (), True)
            self.implicit.append(callee.name)
        t = self.value(callee)
        if isinstance(t, Pointer) and isinstance(t.target, Function):
            f = t.target
        else:
            raise TypeFailure(node, "a call of a value that is not a function")
        if node.args is not None:
            for a in node.args.exprs:
                self.value(a)
        return f.result

    def expr_StructRef(self, node: c_ast.StructRef) -> CType:
        if node.type == "->":
            t = self.value(node.name)
            if not isinstance(t, Pointer) or not isinstance(t.target.unqualified(), Record):
                raise TypeFailure(node, f"-> {node.field.name} on a value that is not a pointer to a struct")
            record = t.target
        else:
            record = self.expression(node.name)
            if not isinstance(record, Record):
                raise TypeFailure(node, f".{node.field.name} on a value that is not a struct")
        ft = record.field_type(node.field.name)
        if ft is None:
            spelling = record.definition.spelling or "an anonymous struct"
            raise TypeFailure(node, f"no field {node.field.name!r} in {spelling}")
        return qualify(ft, record.quals)

    def expr_ArrayRef(self, node: c_ast.ArrayRef) -> CType:
        a, b = self.value(node.name), self.value(node.subscript)
        if isinstance(a, Pointer):
            return a.target
        if isinstance(b, Pointer):
            return b.target
        raise TypeFailure(node, "a subscript of a value that is not a pointer or an array")

    def expr_ExprList(self, node: c_ast.ExprList) -> CType:
        t = INT
        for e in node.exprs:
            t = self.value(e)
        return t

    def expr_CompoundLiteral(self, node: c_ast.CompoundLiteral) -> CType:
        t = self.type_of_declarator(node.type)
        self.initializer(node.init, t)
        return t

    def expr_InitList(self, node: c_ast.InitList) -> CType:
        for e in node.exprs:
            self.value(e)
        return INT

    def expr_Typename(self, node: c_ast.Typename) -> CType:
        return self.type_of_declarator(node)

    # --- the unit ---

    def run(self, ast: c_ast.FileAST) -> "TypePass":
        for ext in ast.ext:
            if isinstance(ext, c_ast.FuncDef):
                self.visit_FuncDef(ext)
            elif isinstance(ext, (c_ast.Decl, c_ast.Typedef)):
                self.visit(ext)
            elif isinstance(ext, (c_ast.Pragma, c_ast.StaticAssert)):
                continue
            else:
                raise TypeFailure(ext, f"a file-scope construct not modelled: {type(ext).__name__}")
        return self


INTEGER_RANK = ["_Bool", "char", "signed char", "unsigned char", "short", "unsigned short", "int", "unsigned int",
                "long", "unsigned long", "long long", "unsigned long long"]


def promoted(t: CType) -> CType:
    if isinstance(t, Enumeration):
        return INT
    if isinstance(t, Arithmetic) and not t.floating:
        if INTEGER_RANK.index(t.spelling) < INTEGER_RANK.index("int"):
            return INT
        return Arithmetic(t.spelling)
    return t


def arithmetic_result(a: CType, b: CType) -> CType:
    if isinstance(a, Arithmetic) and a.floating:
        return Arithmetic(a.spelling, floating=True)
    if isinstance(b, Arithmetic) and b.floating:
        return Arithmetic(b.spelling, floating=True)
    a, b = promoted(a), promoted(b)
    if isinstance(a, Arithmetic) and isinstance(b, Arithmetic):
        return a if INTEGER_RANK.index(a.spelling) >= INTEGER_RANK.index(b.spelling) else b
    if isinstance(a, Arithmetic):
        return a
    if isinstance(b, Arithmetic):
        return b
    return INT
