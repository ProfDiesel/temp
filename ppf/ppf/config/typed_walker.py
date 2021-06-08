import typing
from typing import Union, Dict, Tuple, Set, Sequence, cast, List, Iterator, Type, Optional, TypeVar, Generic, Callable, Protocol, Final, runtime_checkable, Any, Mapping
from functools import singledispatchmethod, reduce
from contextlib import suppress
from uuid import uuid4
from functools import partial
from itertools import chain

from .base import Config, Object, Value
from .walker import Walker, as_value, as_str


class ConfigSerializable(Protocol):
    @classmethod
    def of_value(cls, value: Value) -> 'ConfigSerializable': ...

    def as_value(self) -> Value: ...


T = TypeVar('T')
FieldT = TypeVar('FieldT')
KeyT = TypeVar('KeyT')
ValueT = TypeVar('ValueT')


class TypedWalker(Walker, Generic[T]):
    def __init__(self, walker: Walker):
        super().__init__(walker._config, walker.name, _object=walker.object)

    @property
    def type(self) -> str:
        return as_str(self['type'])


class Field(Protocol, Generic[T]):
    @property
    def name(self) -> str: ...

    def __get__(self, instance: Optional[Walker], owner: Optional[Type['Field']]) -> T: ...
    def __set__(self, instance: Walker, value: T): ...


class FieldBase(Field[T]):
    def __init__(self, name: str):
        self.__name = name

    @property
    def name(self) -> str:
        return self.__name


class ValueField(FieldBase[T]):
    def __init__(self, name: str, of_value: Callable[[Value], T], as_value: Callable[[T], Value]):
        super().__init__(name)
        self.__of_value, self.__as_value = of_value, as_value

    def __get__(self, instance: Optional[Walker], owner: Optional[Type['ValueField']]) -> T:
        if instance is None:
            raise AttributeError(self.name)
        return self.__of_value(as_value(instance[self.name]))

    def __set__(self, instance: Walker, value: T):
        instance[self.name] = self.__as_value(value)


class ObjectField(FieldBase[T]):
    def __init__(self, name: str, type_: TypedWalker[T]):
        super().__init__(name)
        self.__type = type_

    def __get__(self, instance: Optional[Walker], owner: Optional[Type['ObjectField']]) -> TypedWalker[T]:
        if instance is None:
            raise AttributeError(self.name)
        return self.__type(instance[self.name])

    def __set__(self, instance: Walker, value: TypedWalker[T]):
        instance[self.name] = value.name
        if value._config is not instance._config:
            for walker in value.walk_objects():
                instance._config.try_add(walker.name, walker.object)


class FieldDecorator(Generic[T], Field[T]):
    def __init__(self, field: Field[T]):
        self._field = field

    @property
    def name(self) -> str:
        return self._field.name


class OptionalField(Generic[T], FieldDecorator[Optional[T]]):
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


class SequenceField(Generic[T], FieldDecorator[Sequence[T]]):
    def __get__(self, instance: Optional[Walker], owner: Optional[Type['SequenceField']]) -> Sequence[T]:
        if instance is None:
            raise AttributeError(self.name)
        with suppress(KeyError):
            return self._field.__get__(instance, owner)
        return None

    def __set__(self, instance: Walker, value: Sequence[T]):
        for item in value:
            pass


class MappingField(Generic[T], FieldDecorator[Mapping[str, T]]):
    def __init__(self, field: Field[T], name_keys: Optional[str] = None):
        self.__values = SequenceField(field)
        self.__name_keys = name_keys or f'{field.name}_keys'

    def __get__(self, instance: Optional[Walker], owner: Optional[Type['MappingField']]) -> Mapping[str, T]:
        if instance is None:
            raise AttributeError(self.name)
        class Proxy(Mapping[str, T]):
            __getitem__ = partial(self.get_item, instance=instance, owner=owner)
        return Proxy(self, instance)

    def getitem(self, instance: Walker, index: str, owner) -> T:
        with suppress(ValueError):
            return self.__values.__get__(instance, owner)[instance[self.__name_keys].index(index)]
        raise KeyError(str)

    def __set__(self, instance: Walker, value: Mapping[str, T]):
        self.__values.__set__(value.values())
        instance[self.__name_keys] = list(value.keys())


_WALKER_TYPE_REGISTRY: Dict[str, Type[TypedWalker]] = {}

TypedWalkerT = TypeVar('TypedWalkerT', bound=TypedWalker)


class _Missing:
    pass


MISSING = _Missing()


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
        field: FieldBase
        if issubclass(decayed, TypedWalker):
            field = ObjectField(name, decayed)
        else:
            if decayed in typing.get_args(Value):
                as_value, of_value = decayed, decayed
            elif decayed in (int, bool):
                as_value, of_value = float, decayed
            else:
                as_value, of_value = getattr(decayed, 'as_value'), getattr(decayed, 'of_value')
            field = ValueField(name, of_value, as_value)
        return OptionalField(field) if is_optional else SequenceField(field) if issubclass(decayed, Sequence) else field

    if not typename:
        typename = reduce(lambda acc, c: acc + (f'_{c}' if c.isupper() else c), cls.__name__).lower()

    bases = cls.__bases__ if cls.__bases__ != (object,) else ()
    cls_fields: Final[Dict[str, Type]] = dict(chain.from_iterable(typing.get_type_hints(cls_).items() for cls_ in reversed(cls.mro())))

    def init(self: TypedWalker[T], source: Union[Walker, str, None], /, *, fields: Mapping[str, Any] = {}, **kwargs):
        walker: Walker
        if not isinstance(source, Walker):
            name: str = source if source else f'{type(self).__qualname__}_{uuid4().hex}'
            object_ = Object(type=type(self).type)
            config = Config(**{name: object_})
            walker = Walker(config, name, _object=object_)
        else:
            walker = source

        # TODO: should look for __init__ in the bases, but type(super(type(self), self)) == type(self)
        # -> have a classmethod to resolve the type ?
        TypedWalker.__init__(self, walker)

        fields.update(kwargs)
        for field_name, value in fields.items():
            setattr(self, field_name, value)
        for field_name, type_ in cls_fields.items():
            if field_name in fields:
                continue
            is_optional, decayed = decay_optional(type_)
            if is_optional:
                source = walker.get(field_name)
                value = decayed(source) if source else None
            elif issubclass(type_, Walker):
                value = type_(walker.get(field_name))
            else:
                value = type_()
            setattr(self, field_name, value)

    def validate(self) -> bool:
        actual_type: Final[Type[TypedWalker]] = _WALKER_TYPE_REGISTRY[as_str(self['type'])]
        if not issubclass(actual_type, type(self)):
            return False
        with suppress(ValueError, KeyError):
            for field_name in cls_fields.keys():
                getattr(self, field_name)
            return True
        return False

    class_dict = dict(**{name: make_field(name, type_) for name, type_ in cls_fields.items()}, __init__=init, validate=validate)
    result = type(cls.__name__, (*bases, TypedWalker,), class_dict)
    result.type = typename
    _WALKER_TYPE_REGISTRY[typename] = result
    return result
