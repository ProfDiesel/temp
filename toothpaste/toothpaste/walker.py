import typing
from contextlib import suppress
from functools import reduce, singledispatchmethod
from typing import (Callable, Dict, Final, Generic, Iterator, List, MutableSet,
                    Optional, Protocol, Sequence, Set, Tuple, Type, TypeVar,
                    Union, cast, overload)
from uuid import uuid4

from .types import Config, Object, ScalarValue, SequenceValue, Value

DeferredWalker = Union[str, 'Walker']
WalkerScalarValue = Union[DeferredWalker, float]
WalkerSequenceValue = Union['WalkerSequence', List[float]]
WalkerValue = Union[WalkerScalarValue, WalkerSequenceValue]


def make_walker(config: Config, value: Value) -> WalkerValue:
    if isinstance(value, str):
        with suppress(KeyError):
            return Walker(config, value)
        return value
    if isinstance(value, list) and (len(value) > 1) and isinstance(value[0], str):
        return WalkerSequence(config, cast(List[str], value))
    return cast(WalkerValue, value)


class ConstWalker(Protocol):
    @property
    def _config(self) -> Config: ...

    @property
    def name(self) -> str: ...

    @property
    def value(self) -> Union[Object, str, None]: ...

    @property
    def fields(self) -> Iterator[Tuple[str, WalkerValue]]: ...

    def __getitem__(self, item: str) -> WalkerValue: ...

    def get(self, item: str) -> Optional[WalkerValue]: ...

    def walk_objects(self) -> Iterator['Walker']: ...

    def walk(self) -> Iterator[Tuple['Walker', str, Value]]: ...


class MutableWalker(ConstWalker):
    def __setitem__(self, item: str, value: Union[None, Value, 'Walker']) -> None: ...
    def __delitem__(self, item: str) -> None: ...

    def set(self, item: str, value: Union[None, Value, 'Walker']) -> None: ...


class Walker(MutableWalker):
    __slots__ = ('__config', '__name', '__object')

    def __init__(self, config: Config, name: str, *, _object: Optional[Object] = None):
        self.__config: Final[Config] = config
        self.__name: Final[str] = name
        self.__object: Final[Object] = _object or self.__config[name]

    @property
    def _config(self) -> Config:
        return self.__config

    @property
    def name(self) -> str:
        return self.__name

    @property
    def object(self) -> Object:
        return self.__object

    @property
    def fields(self) -> Iterator[Tuple[str, WalkerValue]]:
        for key, value in self.object.items():
            yield key, make_walker(self._config, value)

    def __getitem__(self, item: str) -> WalkerValue:
        return make_walker(self._config, self.object[item])

    def __setitem__(self, item: str, value: Union[None, Value, 'Walker']) -> None:
        self.set(item, value)

    def __delitem__(self, item: str) -> None:
        del self.object[item]

    def __repr__(self):
        return self.name

    def get(self, item: str) -> Optional[WalkerValue]:
        with suppress(KeyError):
            return self[item]
        return None

    def set(self, item: str, value: Union[None, Value, 'Walker']) -> None:
        if value is None:
            with suppress(KeyError):
                del self[item]
        elif isinstance(value, typing.get_args(Value)):
            value = cast(Value, value)
            self.__object[item] = value
        else:
            value = cast(Walker, value)
            if value._config is not self._config:
                for walker in value.walk_objects():
                    self._config.try_add(walker.name, walker.object)
            self.__object[item] = value.name

    def walk_objects(self, *, skip_objects: Optional[MutableSet[str]] = None) -> Iterator['Walker']:
        if skip_objects is None:
            skip_objects = set()
        if self.name in skip_objects:
            return
        yield self
        skip_objects.add(self.name)
        for field, value in self.fields:
            if isinstance(value, (Walker, WalkerSequence)):
                yield from value.walk_objects(skip_objects=skip_objects)

    def walk(self) -> Iterator[Tuple['Walker', str, Value]]:
        for object_ in self.walk_objects():
            for field, value in object_.fields:
                yield object_, field, as_value(value)


class WalkerSequence(Sequence[WalkerScalarValue]):
    __slots__ = ('__config', '__names')

    def __init__(self, config: Config, names: Sequence[str]):
        self.__config: Config = config
        self.__names: List[str] = list(names)

    @overload
    def __getitem__(self, index: int) -> WalkerScalarValue:
        ...

    @overload
    def __getitem__(self, index: slice) -> Sequence[WalkerScalarValue]:
        ...

    def __getitem__(self, index: Union[int, slice]) -> Union[WalkerScalarValue, Sequence[WalkerScalarValue]]:
        return make_walker(self.__config, self.__names[index])

    def __len__(self) -> int:
        return len(self.__names)

    def walk_objects(self, *, skip_objects: Optional[MutableSet[str]] = None) -> Iterator[Walker]:
        for name in self.__names:
            yield from Walker(self.__config, name).walk_objects(skip_objects=skip_objects)

    def walk(self) -> Iterator[Tuple[Walker, str, Value]]:
        for name in self.__names:
            yield from Walker(self.__config, name).walk()


def as_object(result: WalkerValue) -> Walker:
    assert(isinstance(result, Walker))
    return cast(Walker, result)


def as_object_opt(result: Optional[WalkerValue]) -> Optional[Walker]:
    assert(isinstance(result, Walker) or result is None)
    return cast(Optional[Walker], result)


def as_sequence(result: WalkerValue) -> WalkerSequence:
    assert(isinstance(result, WalkerSequence))
    return cast(WalkerSequence, result)


def as_numeric(result: WalkerValue) -> float:
    assert(isinstance(result, (float, int)))
    return cast(float, result)


def as_numeric_seq(result: WalkerValue) -> List[float]:
    assert(isinstance(result, list))
    return cast(List[float], result)


def as_str(result: WalkerValue) -> str:
    assert(isinstance(result, Walker))
    return str(result)

@overload
def as_value(result: WalkerScalarValue) -> ScalarValue:
    ...

@overload
def as_value(result: WalkerSequenceValue) -> SequenceValue:
    ...

def as_value(result: WalkerValue) -> Value:
    if isinstance(result, Walker):
        return result.name
    if isinstance(result, WalkerSequence):
        return cast(Value, [as_value(item) for item in result])
    return cast(Value, result)


def as_int(result: WalkerValue) -> int:
    value: float = as_numeric(result)
    assert(value.is_integer())
    return int(value)
