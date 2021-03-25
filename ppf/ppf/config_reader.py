from pyparsing import Group, Optional as Optional_, Suppress, delimitedList, restOfLine, pyparsing_common, Char, ParserElement
from typing import Union, Dict, Tuple, Set, Sequence, cast, List, Iterator, Type, Optional
from dataclasses import dataclass
from functools import singledispatchmethod
from contextlib import suppress

Value = Union[str, float, Tuple[str, ...], Tuple[float, ...]]
Object = Dict[str, Value]
Config = Dict[str, Object]

string = Suppress('\'') + ... + Suppress(~Char('\\') + '\'')
numeric = pyparsing_common.fnumber.copy()
identifier = pyparsing_common.identifier.copy()


def list_of(expr: ParserElement) -> str:
    return (Suppress('[') + delimitedList(expr) + Suppress(']')).setParseAction(tuple)


key = (Optional_(identifier + Suppress('.'), '') + identifier).setParseAction(tuple)
value = string | numeric | list_of(string) | list_of(numeric)
assignment = Group(key + Suppress('<-') + value + Suppress(';'))
grammar = assignment[1, ...].ignore('#' + restOfLine)


def parse(config: str) -> Config:
    result:Config  = {}
    for (name, field), value in grammar.parseString(config):
        result.setdefault(name, {})[field] = value
    return result


def format_assignment(name: str, field: str, value: Value) -> str:
    def format_value(value: Value):
        return f'\'{value}\'' if isinstance(value, str) else f'{value}' if isinstance(value, float) else f"[{','.join(str(value))}]"

    return f'{name}.{field} <- {format_value(value)};'


def write(config: Config) -> str:
    result = ""

    for name, object_ in sorted(config.items()):
        for field, value in sorted(object_.items()):
            result += format_assignment(name, field, value) + '\n'

    return result

class Unresolved:
    pass


UNRESOLVED = Unresolved()

WalkerResult = Union['Walker', 'WalkerSequence', float, Tuple[float, ...]]


def make_walker(config: Config, value: Value, defer_resolution=False) -> WalkerResult:
    if isinstance(value, str):
        return Walker(config, value, defer_resolution=defer_resolution)
    if isinstance(value, tuple) and (len(value) > 1) and isinstance(value[0], str):
        return WalkerSequence(config, cast(List[str], value))
    return cast(WalkerResult, value)


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
        return cast(Union[Object, str, None], self.__value)

    @property
    def fields(self) -> Iterator[Tuple[str, WalkerResult]]:
        if isinstance(self.value, dict):
            for key, value in self.value.items():
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
        return self.name

    def get(self, item: str) -> Optional[WalkerResult]:
        with suppress(ValueError, KeyError):
            return getattr(self, item)
        return None

    def walk(self) -> Iterator[Tuple['Walker', str, Value]]:
        for field, value in self.fields:
            yield self, field, as_value(value)
            if isinstance(value, str):
                yield from Walker(self.__config, value).walk()
            if isinstance(value, tuple) and (len(value) > 1) and isinstance(value[0], str):
                yield from WalkerSequence(self.__config, value).walk()


@dataclass
class WalkerSequence(Sequence[Walker]):
    __slots__ = ('__config', '__names')

    __config: Config
    __names: Tuple[str, ...]

    def __init__(self, config: Config, names: Sequence[str]):
        self.__config = config
        self.__names = tuple(names)

    def __getitem__(self, item: int) -> Walker:
        return Walker(self.__config, self.__names[item])

    def __len__(self) -> int:
        return len(self.__names)

    def walk(self) -> Iterator[Tuple['Walker', str, Value]]:
        for name in self.__names:
            yield from Walker(self.__config, name).walk()


def as_object(result: WalkerResult) -> Walker:
    assert(isinstance(result, Walker))
    return cast(Walker, result)

def as_object_opt(result: Optional[WalkerResult]) -> Optional[Walker]:
    return cast(Optional[Walker], result)

def as_sequence(result: WalkerResult) -> WalkerSequence:
    assert(isinstance(result, WalkerSequence))
    return cast(WalkerSequence, result)

def as_numeric(result: WalkerResult) -> float:
    assert(isinstance(result, float))
    return cast(float, result)

def as_numeric_seq(result: WalkerResult) -> Tuple[float, ...]:
    assert(isinstance(result, tuple))
    return cast(Tuple[float, ...], result)

def as_str(result: WalkerResult) -> str:
    assert(isinstance(result, Walker))
    return str(result)

def as_value(result: WalkerResult) -> Value:
    if isinstance(result, Walker):
        return result.name
    if isinstance(result, WalkerSequence):
        return tuple(item.name for item in result)
    return cast(Value, result)

def as_int(result: WalkerResult) -> int:
    value:float = as_numeric(result)
    assert(value.is_integer())
    return int(value)
