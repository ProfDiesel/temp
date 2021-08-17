from typing import Union, Dict, List

ScalarValue = Union[str, float]
SequenceValue = Union[List[str], List[float]]
Value = Union[ScalarValue, SequenceValue]

class Object(Dict[str, Value]):
    pass

class Config(Dict[str, Object]):
    def try_add(self, name: str, object_: Object):
        if name in self:
            raise KeyError(name)
        self[name] = object_
