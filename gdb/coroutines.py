import re
from contextlib import suppress
from functools import reduce
from operator import or_
from typing import (Set, TYPE_CHECKING, Any, Callable, Collection, Iterable, List,
                    Literal, Optional, Tuple)

import gdb
import gdb.printing

if TYPE_CHECKING:
    import Dashboard

    R: Any

    def ansi(text: str, style: dict) -> str:
        ...


def command(function: Callable[[gdb.Value, bool], None]):
    class Command(gdb.Command):
        def __init__(self):
            super().__init__(function.__name__, gdb.COMMAND_USER)

        def invoke(self, arg, from_tty):
            function(arg, from_tty)
    Command()


"""
Coroutine
"""
class CoroutineHandle:
    def __init__(self, value):
        self.value = value

    def to_string(self) -> str:
        pc: int = int(self.resumption)
        sal = gdb.find_pc_line(pc)
        if not sal or not sal.symtab:
            return f'{pc:016x}'
        filename, line = sal.symtab.filename, sal.line
        return f'{ansi(str(filename), R.style_high)}:{ansi(line, R.style_high)}'

    @property
    def resumption(self) -> gdb.Value:
        coro_struct_ptr = self.value['__handle_'].address.cast(
            gdb.lookup_type('void').pointer().pointer().pointer()
        )
        return coro_struct_ptr.dereference().dereference()

    @classmethod
    def current(cls) -> Optional['CoroutineHandle']:
        regex = re.compile(r'std::experimental::coroutines_v1::coroutine_handle<.*>::resume')
        current = gdb.newest_frame()
        while not regex.match(current.function().name):
            current = current.older()
            if not current:
                return None
        coroutine = current.read_var('this').dereference()
        return cls(coroutine)


def _coroutine_printer_collection():
    collection = gdb.printing.RegexpCollectionPrettyPrinter('libc++/coroutines_v1')
    collection.add_printer(
        'coroutine handler',
        'std::experimental::coroutines_v1::coroutine_handle<.*>$',
        CoroutineHandle,
    )
    return collection


gdb.printing.register_pretty_printer(None, _coroutine_printer_collection(), replace=True)


"""
LEAF
"""


class LeafSlot:
    def __init__(self, value: gdb.Value):
        self.value = value

    @property
    def chain(self) -> Iterable['LeafSlot']:
        if not self.is_valid:
            return
        current = self.value
        while current:
            yield LeafSlot(current)
            current = current['prev_']

    @property
    def is_valid(self) -> bool:
        return int(self.value['top_']) != 0

    def to_string(self):
        return f'value:{self.value["value_"]} deep:{self.value["enable_deep_frame_deactivation_"]}' if self.is_valid else 'invalid'

    def current(self) -> Optional[gdb.Value]:
        return self.current_(self.value.type.template_argument(0))

    @staticmethod
    def current_(type_: str) -> Optional[gdb.Value]:
        top = gdb.parse_and_eval(f'boost::leaf::leaf_detail::tl_slot_ptr<{type_}>::p')
        return LeafSlot(top.dereference()) if top else None


class LeafContext:
    def __init__(self, value: gdb.Value):
        self.value = value

    @property
    def slots(self) -> List['LeafSlot']:
        from printers import StdTuplePrinter
        return [LeafSlot(slot) for _, slot in StdTuplePrinter(self.value['tup_']).children()]

    def to_string(self) -> str:
        if not len(self.slots):
            return f'slot: [no slot] active:{self.value["is_active_"]}'
        first_slot = self.slots[0].current()
        first_slot_addr = int(first_slot.value.address) if first_slot is not None else 0
        return f'slot: {first_slot_addr:016x} active:{self.value["is_active_"]}'

    def display_hint(self) -> Literal['array', 'map', 'string']:
        return 'array'


class LeafPolymorphicContext:
    def __init__(self, value: gdb.Value):
        self.value = value

    def to_string(self) -> str:
        try:
            poly_type = self.value.dynamic_type.template_argument(0).reference()
            actual = self.value.dynamic_cast(poly_type)
        except:
            actual = self.value
        return f'{LeafContext(actual).to_string()} id: {int(self.value["captured_id_"]["value_"])}'

def _leaf_printer_collection():
    collection = gdb.printing.RegexpCollectionPrettyPrinter('boost::leaf')
    collection.add_printer(
        'leaf context', '^boost::leaf::context<.*>$', LeafContext
    )
    collection.add_printer(
        'leaf polymorphic_context', '^boost::leaf::polymorphic_context$', LeafPolymorphicContext
    )
    return collection

gdb.printing.register_pretty_printer(None, _leaf_printer_collection(), replace=True)


"""
ASIO
"""

class AsioFrame:
    def __init__(self, value: gdb.Value):
        self.value = value

    def to_string(self) -> str:
        result: str = f'{self.value["coro_"]}'
        ctx: gdb.Value = self.value['ctx_']['__ptr_']
        result = f'{result} ctx: {ctx.dereference().format_string() if int(ctx) else "[no ctx]"}'
        thread: int = int(self.value['attached_thread_'])
        if thread:
            result = f'{result} - thr: {thread:016x}'
        return result


class AsioThread:
    _thread_ptrs: Set[int] = set()

    def __init__(self, value: gdb.Value):
        self.value = value

    @property
    def frames(self) -> Iterable[gdb.Value]:
        try:
            # asio 1.19
            frame = self.value['bottom_of_stack_']['frame_']
            if not frame:
                return
            current = frame['top_of_stack_']
        except gdb.error:
            # asio 1.18
            current = self.value['top_of_stack_']
        while current:
            yield current
            current = current['caller_']

    def children(self) -> Iterable[Tuple[str, str]]:
        return ((f'{int(frame.address):016x}', frame.dereference()) for frame in self.frames)

    def to_string(self) -> str:
        result = f'{int(self.value.address):016x}'
        with suppress(gdb.error):
            ctx: gdb.Value = self.value['ctx_']['__ptr_']
            result = f'{result} ctx: {ctx} {ctx.dereference().format_string() if int(ctx) else "[no ctx]"}'
        current = self.current()
        if current is not None and int(self.value.address) == int(current.value.address):
            return ansi(result, R.style_high)
        else:
            return result

    def display_hint(self):
        return 'array'

    @classmethod
    def current(cls) -> Optional['AsioThread']:
        current = gdb.newest_frame()
        while current.function().name != 'asio::detail::awaitable_thread<boost::leaf::executor>::pump()':
            current = current.older()
            if not current:
                return None
        thread = current.read_var('this').dereference()
        return cls(thread)

    @classmethod
    def add_thread(cls, thread_ptr):
        cls._thread_ptrs.add(int(thread_ptr))

    @classmethod
    def remove_thread(cls, thread_ptr):
        cls._thread_ptrs.remove(int(thread_ptr))

    @classmethod
    def clear(cls):
        cls._thread_ptrs = set()

    @classmethod
    def threads(cls) -> Iterable['AsioThread']:
        type_ = gdb.lookup_type('asio::detail::awaitable_thread<boost::leaf::executor>').pointer()
        return (cls(gdb.Value(ptr).cast(type_).dereference()) for ptr in cls._thread_ptrs)


def _on_new_inferior(inferior: gdb.Inferior):
    AsioThread.clear()


gdb.events.new_inferior.connect(_on_new_inferior)


def _printer_collection():
    collection = gdb.printing.RegexpCollectionPrettyPrinter('asio')
    collection.add_printer(
        'asio frame', '^asio::detail::awaitable_frame(_base)?<.*>$', AsioFrame
    )
    collection.add_printer(
        'asio thread', '^asio::detail::awaitable_thread<.*>$', AsioThread
    )
    return collection


gdb.printing.register_pretty_printer(None, _printer_collection(), replace=True)


@command
def register_thread(arg: str, from_tty: bool):
    AsioThread.add_thread(gdb.parse_and_eval('this'))

@command
def unregister_thread(arg: str, from_tty: bool):
    AsioThread.remove_thread(gdb.parse_and_eval('this'))

@command
def print_threads(arg: str, from_tty: bool):
    for thread in AsioThread.threads():
        print(thread.value.format_string())

@command
def print_context(arg: str, from_tty: bool):
    current = LeafContext(gdb.parse_and_eval(arg))
    if current is None:
        return
    print(current.to_string())
    for slot in current.slots[0].chain:
        print(slot.to_string())

@command
def print_current_slot(arg: str, from_tty: bool):
    current = LeafSlot.current_(arg)
    if current is None:
        return
    for slot in current.chain:
        print(slot.to_string())


class AsioThreadPanel(Dashboard.Module):
    """ASIO coroutine threads panel"""

    def label(self) -> str:
        return 'Coroutines'

    def lines(
        self, term_width: int, term_height: int, style_changed: bool
    ) -> Collection[str]:
        out = []

        for thread in AsioThread.threads():
            out.append(thread.to_string())
            for n, frame in thread.children():
                out.append(
                    f'{ansi(str(n), R.style_high if n == "0" else R.style_low)}: {frame}'
                )
        return out


"""
IPython
"""

@command
def ipython(arg: gdb.Value, from_tty: bool):
    """ iptython console --existing kernel-XXX.json """
    import IPython
    IPython.embed_kernel()
