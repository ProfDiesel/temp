#!/usr/bin/env python3

from pyparsing import Group, Optional, Suppress, delimitedList, restOfLine, pyparsing_common, Dict, Char
from typing import Union, Dict, Tuple
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


def parse(config):
    result = {}
    for (name, field), value in grammar.parseString(config):
        result.setdefault(name, {})[field] = value
    return result


class Unresolved:
    pass
UNRESOLVED = Unresolved()


@dataclass
class walker:
    __slots__ = ('__config', '__name', '__value')

    __config: Config
    __name: Union[str, Tuple[str, ...]]
    __value: Union[Object, str, Tuple[str, ...], None, Unresolved]

    def __init__(self, config, name):
        self.__config = config
        self.__name = name
        self.__value = UNRESOLVED

    @singledispatchmethod
    def __do_getattr__(self, value, attribute) -> Union['walker', Value]:
        raise ValueError()

    @__do_getattr__.register
    def __do_getattr__(self, value_as_object: dict, attribute): # -> Union['walker', Value]:
        value = value_as_object[attribute]
        if isinstance(value, str) or (isinstance(value, tuple) and (len(value) > 1) and isinstance(value[0], str)):
            return walker(self.__config, value)
        return value

    def __getattr__(self, attribute) -> Union['walker', Value]:
        if self.__value is UNRESOLVED:
            self.__value = self.__config.get(self.__name, None)
        return self.__do_getattr__(self.__value, attribute)

    @singledispatchmethod
    def __do_getitem__(self, value, item) -> Union[str, float]:
        raise ValueError()

    @__do_getitem__.register
    def __do_getitem__(self, value_as_list: tuple, item): # -> Union['walker', float]:
        value = value_as_list[item]
        if isinstance(value, str):
            return walker(self.__config, value)
        return value

    def __getitem__(self, item) -> Union['walker', str, float]:
        if isinstance(self.__name, tuple):
            return walker(self.__config, self.__name[item])
        if self.__value is UNRESOLVED:
            self.__value = self.__config.get(self.__name, None)
        return self.__do_getitem__(self.__value, item)

    def __str__(self):
        return self.__name


if __name__ == '__main__':
    c = parse("""
a <- 3;
a.a <- ['c', 'a'];
a.b <- 3;
c.x <- 'pipo';
""")
    a = walker(c, 'a')
    print(a.a[0].x)
    print(a.a[1].b)
