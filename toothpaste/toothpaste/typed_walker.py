from abc import ABC, abstractmethod
import typing
from typing import Union, Dict, Tuple, Set, Sequence, cast, List, Iterator, Type, Optional, TypeVar, Generic, Callable, Protocol, Final, runtime_checkable, Any, Mapping
from functools import singledispatchmethod, reduce
from contextlib import suppress
from uuid import uuid4
from functools import partial
from itertools import chain

from .types import Config, Object, Value
from .walker import Walker, as_value, as_str, WalkerValue, as_object


@runtime_checkable
class ConfigSerializable(Protocol):
    @classmethod
    def of_value(cls, value: Value) -> 'ConfigSerializable': ...

    def as_value(self) -> Value: ...


T = TypeVar('T')
FieldT = TypeVar('FieldT', contravariant=True)
KeyT = TypeVar('KeyT')
ValueT = TypeVar('ValueT')


class TypedWalker(ABC, Walker, Generic[T]):
    def __init__(self, walker: Walker):
        super().__init__(walker._config, walker.name, _object=walker.object)

    @property
    def type(self) -> str:
        return as_str(self['type'])

    @abstractmethod
    def validate(self) -> bool:
        return True


class Field(Protocol[T, FieldT]):
    @property
    def name(self) -> str: ...

    def __get__(self, instance: Optional[Walker], owner: Optional[FieldT]) -> T: ...
    def __set__(self, instance: Walker, value: T): ...


class FieldBase(Field[T, FieldT]):
    def __init__(self, name: str):
        self.__name = name

    @property
    def name(self) -> str:
        return self.__name


class ValueField(FieldBase[T, Type['ValueField']]):
    def __init__(self, name: str, of_value: Callable[[Value], T], as_value: Callable[[T], Value]):
        super().__init__(name)
        self.__of_value, self.__as_value = of_value, as_value

    def __get__(self, instance: Optional[Walker], owner: Optional[Type['ValueField']]) -> T:
        if instance is None:
            raise AttributeError(self.name)
        return self.__of_value(as_value(instance[self.name]))

    def __set__(self, instance: Walker, value: T):
        instance[self.name] = self.__as_value(value)


class ObjectField(FieldBase[TypedWalker[T], Type['ObjectField']]):
    def __init__(self, name: str, type_: Type[TypedWalker[T]]):
        super().__init__(name)
        self.__type = type_

    def __get__(self, instance: Optional[Walker], owner: Optional[Type['ObjectField']]) -> TypedWalker[T]:
        if instance is None:
            raise AttributeError(self.name)
        return self.__type(as_object(instance[self.name]))

    def __set__(self, instance: Walker, value: TypedWalker[T]):
        instance[self.name] = value.name
        if value._config is not instance._config:
            for walker in value.walk_objects():
                instance._config.try_add(walker.name, walker.object)


class FieldDecorator(Generic[T, FieldT], Field[T, FieldT]):
    def __init__(self, field: Field[T, FieldT]):
        self._field = field

    @property
    def name(self) -> str:
        return self._field.name


class OptionalField(Generic[T], FieldDecorator[Optional[T], Type['OptionalField']]):
    def __get__(self, instance: Optional[Walker], owner: Optional[Type['OptionalField']]) -> Optional[T]:
        if instance is None:
            raise AttributeError(self.name)
        with suppress(KeyError):
            return self._field.__get__(instance, owner)
        return None

    def __set__(self, instance: Walker, value: Optional[T]):
        if value is None:
            with suppress(KeyError):
                del instance[self.name]
        else:
            self._field.__set__(instance, value)


class SequenceField(Generic[T], FieldDecorator[Sequence[T], Type['SequenceField']]):
    def __get__(self, instance: Optional[Walker], owner: Optional[Type['SequenceField']]) -> Sequence[T]:
        if instance is None:
            raise AttributeError(self.name)
        with suppress(KeyError):
            return self._field.__get__(instance, owner)
        return []

    def __set__(self, instance: Walker, value: Sequence[T]):
        for item in value:
            pass


class MappingField(Generic[T, FieldT], FieldDecorator[Mapping[str, T], Type['MappingField']]):
    def __init__(self, field: Field[T, FieldT], name_keys: Optional[str] = None):
        self.__values: Final[SequenceField[T]] = cast(SequenceField[T], SequenceField(field))
        self.__name_keys = name_keys or f'{field.name}_keys'

    def __get__(self, instance: Optional[Walker], owner: Optional[Type['MappingField']]) -> Mapping[str, T]:
        if instance is None:
            raise AttributeError(self.name)

        class Proxy(Mapping[str, T]):
            __getitem__ = partial(self.getitem, instance=instance, owner=owner)
        return Proxy(self, instance)

    def getitem(self, instance: Walker, index: str, owner) -> T:
        with suppress(ValueError):
            return self.__values.__get__(instance, owner)[instance[self.__name_keys].index(index)]
        raise KeyError(str)

    def __set__(self, instance: Walker, value: Mapping[str, T]):
        self.__values.__set__(value.values())
        instance[self.__name_keys] = list(value.keys())


_WALKER_TYPE_REGISTRY: Dict[str, Type[TypedWalker]] = {}



def walker_type(cls: Type[T], /, typename: Optional[str] = None) -> Type[TypedWalker]:
    def decay_optional(type_) -> Tuple[bool, Type]:
        is_optional: Final[bool] = typing.get_origin(type_) is Union and type(None) in typing.get_args(type_)
        optional_types: Final[Set[Type]] = set(typing.get_args(type_)) - {type(None)}
        return (True, next(iter(optional_types))) if len(optional_types) == 1 else (False, type_)

    def make_field(name: str, type_: Type[FieldT]) -> Field:
        with suppress(TypeError):
            if issubclass(type_, (FieldBase, FieldDecorator)):
                return type_(name)
        is_optional, decayed = decay_optional(type_)
        if type_ in typing.get_args(Value):
            return ValueField(name, type_, type_)
        elif type_ in (int, bool):
            return ValueField(name, type_, float)
        elif is_optional:
            field = make_field(name, decayed)
            return OptionalField(field)
        elif issubclass(type_, ConfigSerializable):
            return ValueField(name, type_.of_value, type_.as_value)
        elif issubclass(type_, TypedWalker):
            return ObjectField(name, type_)
        elif issubclass(type_, Sequence):
            assert(issubclass(cast(Type, typing.get_origin(type_)), Sequence))
            item_type, *_ = typing.get_args(type_)
            return SequenceField(make_field(name, item_type))
        else:
            raise TypeError(type_)

    if not typename:
        typename = reduce(lambda acc, c: acc + (f'_{c}' if c.isupper() else c), cls.__name__).lower()

    bases = cls.__bases__ if cls.__bases__ != (object,) else ()
    field_descriptors: Final[Dict[str, Field]] = {name: make_field(name, type_) for name, type_ in typing.get_type_hints(cls).items()}

    def init(self: TypedWalker[T], source: Union[Walker, str, None], /, *, fields: Mapping[str, Any] = {}, **kwargs):
        walker: Walker
        if isinstance(source, Walker):
            walker = source
        else:
            name: str = source if source else f'{type(self).__qualname__}_{uuid4().hex}'
            object_ = Object(type=self.type)
            config = Config(**{name: object_})
            walker = Walker(config, name, _object=object_)

        # TODO: should look for __init__ in the bases, but type(super(type(self), self)) == type(self)
        # -> have a classmethod to resolve the type ?
        TypedWalker.__init__(self, walker)

        for field_name, value in dict(fields, **kwargs).items():
            setattr(self, field_name, value)

    def validate(self) -> bool:
        actual_type: Final[Type[TypedWalker]] = _WALKER_TYPE_REGISTRY[as_str(self['_type'])]
        if not issubclass(actual_type, type(self)):
            return False
        with suppress(ValueError, KeyError):
            for field in self._fields.values():
                value = field.__get__(self, type(self))
                if isinstance(value, TypedWalker):
                    value.validate()
            return True
        return False

    class_dict = dict(**field_descriptors, _fields=field_descriptors, _type=typename, __init__=init, validate=validate)
    result = type(cls.__name__, (*bases, TypedWalker,), class_dict)
    _WALKER_TYPE_REGISTRY[typename] = result
    return result
