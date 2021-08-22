from toothpaste.typed_walker import walker_type
from toothpaste.walker import Walker

@walker_type
class A:
    name: str

a = A(source=None, name='pipo')
a.validate()
