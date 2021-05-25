from pyparsing import Group, Optional as Optional_, Suppress, delimitedList, restOfLine, stringEnd, pyparsing_common, Char, ParserElement, QuotedString
from typing import Union, Dict, Tuple, Set, Sequence, cast, List, Iterator, Type, Optional, TypeVar, Generic, Callable, Protocol, Final, overload, TypeVar, Literal

from .base import Config, Object, Value
from .walker import Walker, TypedWalker

def _grammar():
    string = QuotedString('"', unquoteResults=True)
    numeric = pyparsing_common.fnumber.copy()
    identifier = pyparsing_common.identifier.copy()


    def list_of(expr: ParserElement) -> str:
        return (Suppress('[') + delimitedList(expr) + Suppress(']')).setParseAction(list)


    key = (Optional_(identifier + Suppress('.'), '') + identifier).setParseAction(tuple)
    value = string | numeric | list_of(string) | list_of(numeric)
    assignment = Group(Suppress('"') + key + Suppress('"') + Suppress(':') + value)
    #return (assignment + Suppress(';'))[1, ...].ignore('//' + restOfLine)
    return delimitedList(assignment).ignore('//' + restOfLine)

GRAMMAR = _grammar()

def unmarshall_assignments(data: str) -> Iterator[Tuple[Tuple[str, str], Value]]:
    return iter(GRAMMAR.parseString(data))


def marshall_assignments(assignments: Iterator[Tuple[Tuple[str, str], Value]]) -> str:
    def format_value(value: Value):
        return f'"{value}"' if isinstance(value, str) else f'[{",".join(format_value(item) for item in value)}]' if isinstance(value, list) else str(value)

    return ',\n'.join(f'"{name}.{field}" : {format_value(value)}' for (name, field), value in assignments)


def unmarshall_config(data: str) -> Config:
    result: Final[Config] = Config()
    for (name, field), value in unmarshall_assignments(data):
        result.setdefault(name, Object())[field] = value
    return result


def marshall_config(config: Config) -> str:
    def assignments():
        for name, object_ in sorted(config.items()):
            for field, value in sorted(object_.items()):
                yield (name, field), value
    return marshall_assignments(assignments())

TypedWalkerT = TypeVar('TypedWalkerT', bound=TypedWalker)

def unmarshall_walker(data: str, name: str, cls = TypedWalkerT) -> TypedWalkerT:
    walker: Final[Walker] = Walker(unmarshall_config(data), name)
    return cls.of_walker(walker) if cls != Walker else walker

def marshall_walker(walker: Walker) -> str:
    return marshall_assignments(((subwalker.name, field), value) for subwalker, field, value in walker.walk())
