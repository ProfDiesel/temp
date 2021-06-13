from typing import Union, Dict, List

Value = Union[str, float, List[str], List[float]]

class Object(Dict[str, Value]):
    pass

class Config(Dict[str, Object]):
    def try_add(self, name: str, object_: Object):
        if name in self:
            raise KeyError(name)
        self[name] = object_

