"""
from .config_reader import Value, Object, Unresolved

from test.common_types import Address

from functools import singledispatch

from typing import Any, Union, TypeVar, Type

@singledispatch
def as_value(value: Any) -> Value: ...

@as_value.register
def _(value: Address) -> Value:
    pass

T = TypeVar('T')

@singledispatch
def of_value(value: Value, type_: Type[T]) -> T:
    ...

@of_value.register
def _(value: Value, type: Type[Address]) -> Address:
    return of_value(value, str).split(':', 1)



@dataclass
class ConfigObject:
    __slots__ = ('__config', '__name', '__value')

    __name: str
    __value: Union[Object, str, None, Unresolved]

    @property
    def _config_object_name(self) -> str:
        ...

    @property
    def _config_object_type(self) -> str:
        ...

    def __getitem__(self, attr: str) -> 'ConfigObject':
        ...

class Map:
    def __init__(key_type, value_type, keys_name, values_name):
        if not keys_name:
            keys_name = f'{field.name}_keys'
        if not values_name:
            values = f'{field.name}_values'

@config_object
class MyConfObject:
    a: Map(str, OtherObject)
    b: int

-> getattr, si interface connue -> interface, sinon ConfigObject qui se comporte comme un Walker
-> représentation homogène en mémoire. Les seuls contrôles sont de surface

def dataclass(cls=None, /, *, type_: Optional[str]=None):
    def wrap(cls):
        fields = {}

        for b in cls.__mro__[-1:0:-1]:
            for field in getattr(b, _FIELDS, ()):
                fields[field.name] = field

        for name, type in cls.__dict__.get('__annotations__', {}).items():
            field = Field(name, type)
            fields[field.name] = field

        setattr(cls, _FIELDS, fields)

        if type_ is None:
            type_ = reduce(lambda x, y: x + (f'_{y.lower()}' if y.isupper() else '') + y, cls.__name__)
        registry[type_] = cls

        return cls


    if cls is None:
        return wrap

    return wrap(cls)


def to_flatjson(object_: ConfigObject, recurse: bool = True) -> Object:
    return Object((field.name, to_value(getattr(object_, field.name), recurse)) for field in fields(object_))

from dataclasses import _create_fn

def marshall(serializable: Serializable, config: Optional[Config]) -> Config:
    result = config or Config()
    for field in self._serializable_fields:
        config[serializable._name] = Object({})


def _marshalling_fns(fields, globals):
    return (
    _create_property('serializable_fields')
    _create_fn('marshall',
                    ('self', 'recursive'),
                    ['return marshall(self__class__.__qualname__ + f"(' + ', '.join([f"{f.name}={{self.{f.name}!r}}" for f in fields]) + ')"'],
                     globals=globals),
    _create_fn('unmarshall',
                    ('cls',),
                    ['return self.__class__.__qualname__ + f"(' + ', '.join([f"{f.name}={{self.{f.name}!r}}" for f in fields]) + ')"'],
                     globals=globals),
    )


def serializable(cls=None):
    def wrap(cls):
        cls_dict = dict(cls.__dict__)
        for field in fields(cls):
            field.name
            field.type

        cls_dict['__name__']

        new_cls = type(cls)(cls.__name__, cls.__bases__, cls_dict)
        new_cls.__qualname__ = getattr(cls, '__qualname__')

    return wrap if cls is None else wrap(cls)
"""
