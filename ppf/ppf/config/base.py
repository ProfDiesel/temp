from typing import Union, Dict, List

Value = Union[str, float, List[str], List[float]]

class Object(Dict[str, Value]):
    pass

class Config(Dict[str, Object]):
    pass


