import typing
from typing import Union, Dict, Tuple, Set, Sequence, cast, List, Iterator, Type, Optional, TypeVar, Generic, Callable, Protocol, Final
from functools import singledispatchmethod, reduce
from contextlib import suppress
from abc import ABC

from .base import Config, Object, Value

class Unresolved:
    pass

UNRESOLVED = Unresolved()

WalkerResult = Union['Walker', 'WalkerSequence', float, List[float]]


def make_walker(config: Config, value: Value, defer_resolution=False) -> WalkerResult:
    if isinstance(value, str):
        return Walker(config, value, defer_resolution=defer_resolution)
    if isinstance(value, list) and (len(value) > 1) and isinstance(value[0], str):
        return WalkerSequence(config, cast(List[str], value))
    return cast(WalkerResult, value)


class ConstWalker(Protocol):
    @property
    def _config(self) -> Config: ...

    @property
    def name(self) -> str: ...

    @property
    def value(self) -> Union[Object, str, None]: ...

    @property
    def fields(self) -> Iterator[Tuple[str, WalkerResult]]: ...

    def get(self, item: str) -> Optional[WalkerResult]: ...

    def walk_objects(self) -> Iterator['Walker']: ...

    def walk(self) -> Iterator[Tuple['Walker', str, Value]]: ...


class Walker(ConstWalker):
    __slots__ = ('__config', '__name', '__value')

    def __init__(self, config: Config, name: str, defer_resolution=False):
        self.__config: Config = config
        self.__name: str = name
        self.__value: Union[Object, str, None, Unresolved] = UNRESOLVED if defer_resolution else self.__config.get(name)

    @property
    def _config(self) -> Config:
        return self.__config

    @property
    def name(self) -> str:
        return self.__name

    @property
    def value(self) -> Union[Object, str, None]:
        if self.__value is UNRESOLVED:
            self.__value = self.__config.get(self.__name, self.__name)
        return cast(Union[Object, str, None], self.__value)

    @property
    def fields(self) -> Iterator[Tuple[str, WalkerResult]]:
        if isinstance(self.value, Object):
            for key, value in self.value.items():
                yield key, make_walker(self.__config, value, defer_resolution=True)

    @singledispatchmethod
    def __field__(self, value, field) -> WalkerResult:
        raise ValueError(value)

    @__field__.register
    def _(self, value_as_object: Object, field: str):
        return make_walker(self.__config, value_as_object[field], defer_resolution=True)

    def __getitem__(self, item: str) -> WalkerResult:
        return self.__field__(self.value, item)

    def __setitem__(self, item: str, value: Union[Value, Object]) -> None:
        ...

    def __str__(self):
        return self.name

    def get(self, item: str) -> Optional[WalkerResult]:
        with suppress(ValueError, KeyError):
            return self[item]
        return None

    def walk_objects(self) -> Iterator['Walker']:
        yield self
        for field, value in self.fields:
            if isinstance(value, str):
                yield from Walker(self.__config, value).walk_objects()
            if isinstance(value, List) and (len(value) > 1) and isinstance(value[0], str):
                yield from WalkerSequence(self.__config, value).walk_objects()

    def walk(self) -> Iterator[Tuple['Walker', str, Value]]:
        for object_ in self.walk_objects():
            for field, value in self.fields:
                yield self, field, as_value(value)


class WalkerSequence(Sequence[Walker]):
    __slots__ = ('__config', '__names')

    def __init__(self, config: Config, names: Sequence[str]):
        self.__config: Config = config
        self.__names: List[str] = list(names)

    @singledispatchmethod
    def __getitem__(self, index: int) -> Walker:
        return Walker(self.__config, self.__names[index])

    '''
    @__getitem__.register
    def _(self, slice: slice) -> 'WalkerSequence':
        return type(self)(self.__config, self.__names[slice])
    '''

    def __len__(self) -> int:
        return len(self.__names)

    def walk_objects(self) -> Iterator[Walker]:
        for name in self.__names:
            yield from Walker(self.__config, name).walk_objects()

    def walk(self) -> Iterator[Tuple[Walker, str, Value]]:
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
    assert(isinstance(result, (float, int)))
    return cast(float, result)

def as_numeric_seq(result: WalkerResult) -> List[float]:
    assert(isinstance(result, list))
    return cast(List[float], result)

def as_str(result: WalkerResult) -> str:
    assert(isinstance(result, Walker))
    return str(result)

def as_value(result: WalkerResult) -> Value:
    if isinstance(result, Walker):
        return result.name
    if isinstance(result, WalkerSequence):
        return [item.name for item in result]
    return cast(Value, result)

def as_int(result: WalkerResult) -> int:
    value:float = as_numeric(result)
    assert(value.is_integer())
    return int(value)


class ConfigSerializable(Protocol):
    @classmethod
    def of_value(cls, value: Value) -> 'ConfigSerializable': ...

    def as_value(self) -> Value: ...


T = TypeVar('T')
FieldT = TypeVar('FieldT')
KeyT = TypeVar('KeyT')
ValueT = TypeVar('ValueT')


class TypedWalker(Walker, Generic[T]):
    @classmethod
    def of_walker(cls, walker: Walker) -> 'TypedWalker': ...

_WALKER_TYPE_REGISTRY: Dict[str, Type[TypedWalker]] = {}

TypedWalkerT = TypeVar('TypedWalkerT', bound=TypedWalker)

def walker_type(cls: Type[T], /, typename: Optional[str] = None) -> Type[TypedWalker]:
    def make_field(name: str, type_: Type[FieldT]) -> property:
        is_optional: Final[bool] = typing.get_origin(type_) is Union and type(None) in typing.get_args(type_)
        optional_types: Final[Set[Type]] = set(typing.get_args(type_)) - {type(None)}
        decayed: Final[Type] = next(iter(optional_types)) if len(optional_types) == 1 else type_

        of_walker: Callable[[WalkerResult], FieldT]
        if of_value := getattr(decayed, 'of_value', None):
            of_walker = lambda walker_result: of_value(as_value(walker_result))
        else:
            of_walker = getattr(decayed, 'of_walker', decayed)

        if is_optional:
            def getter(self: Walker):
                if (walker_result := self.get(name)) is not None:
                    return of_walker(walker_result)
                return None
        else:
            def getter(self: Walker):
                return of_walker(self[name])

        as_value_: Final[Union[Callable[[T], Object], Callable[[T], Value]]] = getattr(decayed, 'as_value', as_value)

        if(issubclass(decayed, TypedWalker)):
            def setter(self: Walker, value: T):
                name: Final[str] = value.name
                if name in self._config:
                    raise ValueError()
                self[name] = name
                self._config[name] = cast(Object, value)
        else:
            def setter(self: Walker, value: T):
                self[name] = as_value_(value)

        return property(getter, setter)

    if not typename:
        typename = reduce(lambda acc, c: acc + (f'_{c}' if c.isupper() else c), cls.__name__).lower()

    def of_walker(cls_: Type[TypedWalker[T]], walker: Walker, *, recursive=False) -> TypedWalker[T]:
        walker_type: Final[str] = as_str(walker['type'])
        actual_type: Final[Type[TypedWalker]] = _WALKER_TYPE_REGISTRY[walker_type]
        assert(issubclass(actual_type, cls_))
        return actual_type(walker._config, walker.name, walker.value)

    def __init__(self: T, **kwargs): pass

    fields: Final[Dict[str, Type]] = dict(**cls.__dict__.get('__annotations__', {}), type=str)
    bases = cls.__bases__ if cls.__bases__ != (object,) else ()
    class_dict = dict(**{name: make_field(name, type_) for name, type_ in fields.items()}, of_walker=classmethod(of_walker), typename=typename, __init__=__init__)
    result = type(cls.__name__, (*bases, TypedWalker,), class_dict)
    _WALKER_TYPE_REGISTRY[typename] = result
    return result


'''
"""
@config_object
class MyConfObject:
    a: Map(str, OtherObject)  # -> Dict[str, OtherObject]
"""
class Mapping(Generic[KeyT, ValueT]):
    def __init__(key_type: KeyT, value_type: ValueT, keys_name: str, values_name: str):
        if not keys_name:
            keys_name = f'{field.name}_keys'
        if not values_name:
            values = f'{field.name}_values'
'''
