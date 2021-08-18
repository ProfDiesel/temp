from typing import Any, Callable, Dict, List, Optional, Set, Tuple
from typing import Type as TypingType
from typing import Union

from dataclasses import dataclass

from mypy.errorcodes import ErrorCode
from mypy.nodes import (ARG_NAMED, ARG_NAMED_OPT, ARG_OPT, ARG_POS, ARG_STAR2,
                        MDEF, Argument, AssignmentStmt, Block, CallExpr,
                        ClassDef, Context, Decorator, EllipsisExpr, Expression,
                        FuncBase, FuncDef, JsonDict, MemberExpr, NameExpr,
                        PassStmt, PlaceholderNode, RefExpr, StrExpr,
                        SymbolNode, SymbolTableNode, TempNode, TypeInfo,
                        TypeVarExpr, Var)
from mypy.options import Options
from mypy.plugin import (CheckerPluginInterface, ClassDefContext,
                         MethodContext, Plugin,
                         SemanticAnalyzerPluginInterface)
from mypy.plugins import dataclasses
from mypy.plugins.common import (_get_decorator_bool_argument, add_method,
                                 deserialize_and_fixup_type)
from mypy.semanal import set_callable_name  # type: ignore
from mypy.server.trigger import make_wildcard_trigger
from mypy.typeops import map_type_from_supertype
from mypy.types import (AnyType, CallableType, Instance, NoneType, Type,
                        TypeOfAny, TypeType, TypeVarDef, TypeVarType,
                        UnionType, get_proper_type)
from mypy.typevars import fill_typevars
from mypy.util import get_unique_redefinition_name
from typing_extensions import Final


WALKER_TYPE_FULL_NAME = 'toothpaste.mypy.walker_kype'


@dataclass
class WalkerAttribute:
    name: str
    line: int
    column: int
    type: Optional[Type]
    info: TypeInfo

    def to_var(self) -> Var:
        return Var(self.name, self.type)

    def to_argument(self) -> Argument:
        return Argument(variable=self.to_var(), type_annotation=self.type, initializer=None, kind=ARG_OPT)

METADATA_TAG = WALKER_TYPE_FULL_NAME

class WalkerTransformer:
    def __init__(self, ctx: ClassDefContext) -> None:
        self._ctx = ctx

    def transform(self) -> None:
        ctx = self._ctx
        info = self._ctx.cls.info
        attributes = self.collect_attributes()
        if attributes is None:
            # Some definitions are not ready, defer() should be already called.
            return
        for attr in attributes:
            if attr.type is None:
                ctx.api.defer()
                return

        # __init__
        source_argument_type = UnionType([ctx.api.named_type('toothpaste.Walker'), ctx.api.named_type('__builtins__.str'), NoneType()])
        source_argument = Argument(Var('source', source_argument_type), type_annotation=source_argument_type, initializer=None, kind=ARG_POS)
        attribute_arguments = [attr.to_argument() for attr in attributes]
        kwargs_arguments = [Argument(Var('kwargs'), type_annotation=None, initializer=None, kind=ARG_STAR2)]
        add_method(ctx, '__init__', args=[source_argument] + attribute_arguments + kwargs_arguments, return_type=NoneType())

        # validate
        add_method(ctx, 'validate', args=[], return_type=ctx.api.named_type('__builtins__.bool'))

        info.metadata[METADATA_TAG] = {'attributes': [vars(attr) for attr in attributes]}


    def collect_attributes(self) -> Optional[List[WalkerAttribute]]:
        # First, collect attributes belonging to the current class.
        ctx = self._ctx
        cls = self._ctx.cls
        attrs: List[WalkerAttribute] = []
        known_attrs: Set[str] = set()
        for stmt in cls.defs.body:
            # Any assignment that doesn't use the new type declaration
            # syntax can be ignored out of hand.
            if not (isinstance(stmt, AssignmentStmt) and stmt.new_syntax):
                continue

            # a: int, b: str = 1, 'foo' is not supported syntax so we
            # don't have to worry about it.
            lhs = stmt.lvalues[0]
            if not isinstance(lhs, NameExpr):
                continue

            sym = cls.info.names.get(lhs.name)
            if sym is None:
                # This name is likely blocked by a star import. We don't need to defer because
                # defer() is already called by mark_incomplete().
                continue

            node = sym.node
            if isinstance(node, PlaceholderNode):
                # This node is not ready yet.
                return None
            assert isinstance(node, Var)

            known_attrs.add(lhs.name)
            attrs.append(WalkerAttribute(
                name=lhs.name,
                line=stmt.line,
                column=stmt.column,
                type=sym.type,
                info=cls.info,
            ))

        # Next, collect attributes belonging to any class in the MRO
        # as long as those attributes weren't already collected.  This
        # makes it possible to overwrite attributes in subclasses.
        # copy() because we potentially modify all_attrs below and if this code requires debugging
        # we'll have unmodified attrs laying around.
        all_attrs = attrs.copy()
        for info in cls.info.mro[1:-1]:
            if METADATA_TAG not in info.metadata:
                continue

            super_attrs = []
            # Each class depends on the set of attributes in its dataclass ancestors.
            ctx.api.add_plugin_dependency(make_wildcard_trigger(info.fullname))

            for data in info.metadata['typed_walker']['attributes']:
                name: str = data['name']
                if name not in known_attrs:
                    attr = WalkerAttribute(*data)
                    known_attrs.add(name)
                    super_attrs.append(attr)
                elif all_attrs:
                    # How early in the attribute list an attribute appears is determined by the
                    # reverse MRO, not simply MRO.
                    # See https://docs.python.org/3/library/dataclasses.html#inheritance for
                    # details.
                    for attr in all_attrs:
                        if attr.name == name:
                            all_attrs.remove(attr)
                            super_attrs.append(attr)
                            break
            all_attrs = super_attrs + all_attrs

        return all_attrs


def walker_maker_callback(ctx: ClassDefContext) -> None:
    transformer = WalkerTransformer(ctx)
    transformer.transform()


class CustomPlugin(Plugin):
    def get_class_decorator_hook(self, fullname: str) -> Optional[Callable[[ClassDefContext], None]]:
        if fullname == WALKER_TYPE_FULL_NAME:
            return walker_maker_callback
        return None


def plugin(version: str) -> 'TypingType[Plugin]':
    return CustomPlugin
