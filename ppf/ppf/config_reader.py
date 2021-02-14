from pyparsing import Group, Optional, Suppress, delimitedList, restOfLine, pyparsing_common, Dict, Char
from typing import Union, Dict, Tuple, Generator, Set, Sequence
from dataclasses import dataclass
from functools import singledispatchmethod

Value = Union[str, float, Tuple[str, ...], Tuple[float, ...]]
Object = Dict[str, Value]
Config = Dict[str, Object]

string = Suppress('\'') + ... + Suppress(~Char('\\') + '\'')
numeric = pyparsing_common.fnumber.copy()
identifier = pyparsing_common.identifier.copy()


def list_of(expr):
    return (Suppress('[') + delimitedList(expr) + Suppress(']')).setParseAction(tuple)


key = (Optional(identifier + Suppress('.'), '') + identifier).setParseAction(tuple)
value = string | numeric | list_of(string) | list_of(numeric)
assignment = Group(key + Suppress('<-') + value + Suppress(';'))
grammar = assignment[1, ...].ignore('#' + restOfLine)


def parse(config: str):
    result = {}
    for (name, field), value in grammar.parseString(config):
        result.setdefault(name, {})[field] = value
    return result


def write(config: Config):
    def format_value(value: Value):
        return f'\'{value}\'' if isinstance(value, str) else f'{value}' if isinstance(value, float) else f"[{','.join(value)}]"

    def walk():
        for name, object_ in sorted(config.items()):
            for field, value in sorted(object_.items()):
                yield name, field, value

    return '\n'.join(f'{name}.{field} <- {format_value(value)};' for name, field, value in walk())


class Unresolved:
    pass


UNRESOLVED = Unresolved()

WalkerResult = Union['Walker', 'WalkerSequence', float, Tuple[float, ...]]


def make_walker(config: Config, value: Value, defer_resolution=False) -> WalkerResult:
    if isinstance(value, str):
        return Walker(config, value, defer_resolution=defer_resolution)
    if isinstance(value, tuple) and (len(value) > 1) and isinstance(value[0], str):
        return WalkerSequence(config, value)
    return value


@dataclass
class Walker:
    __slots__ = ('__config', '__name', '__value')

    __config: Config
    __name: str
    __value: Union[Object, str, None, Unresolved]

    def __init__(self, config: Config, name: str, defer_resolution=False):
        self.__config = config
        self.__name = name
        self.__value = UNRESOLVED if defer_resolution else self.__config.get(name)

    @property
    def name(self):
        return self.__name

    @property
    def value(self) -> Union[Object, str, None]:
        if self.__value is UNRESOLVED:
            self.__value = self.__config.get(self.__name, self.__name)
        return self.__value

    @property
    def fields(self) -> Set[str]:
        if isinstance(self.value, dict):
            for key, value in self.value.keys():
                yield key, make_walker(self.__config, value, defer_resolution=True)

    @singledispatchmethod
    def __field__(self, value, field) -> WalkerResult:
        raise ValueError(value)

    @__field__.register
    def _(self, value_as_object: dict, field: str):
        return make_walker(self.__config, value_as_object[field], defer_resolution=True)

    def __getattr__(self, attribute: str) -> WalkerResult:
        return self[attribute]

    def __getitem__(self, item: str) -> WalkerResult:
        return self.__field__(self.value, item)

    def __str__(self):
        return self.value

    def walk(self) -> Generator[Tuple['Walker', str, Value], None, None]:
        for field, value in self.fields:
            yield self, field, value
            if isinstance(value, str):
                yield from Walker(self.__config, value).walk()
            if isinstance(value, tuple) and (len(value) > 1) and isinstance(value[0], str):
                yield from WalkerSequence(self.__config, value).walk()


@dataclass
class WalkerSequence:
    __slots__ = ('__config', '__names')

    __config: Config
    __names: Tuple[str, ...]

    def __init__(self, config: Config, names: Sequence[str]):
        self.__config = config
        self.__names = tuple(names)

    def __getitem__(self, item: int) -> Walker:
        return Walker(self.__config, self.__names[item])

    def walk(self) -> Generator[Tuple['Walker', str, Value], None, None]:
        for name in self.__names:
            yield from Walker(self.__config, name).walk()
