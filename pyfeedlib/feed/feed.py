import feedlib

class CustomCffiNativeHandle(CffiNativeHandle):
    def __init__(self, pointer, prior_ref_count = 0):
        super(CustomCffiNativeHandle, self).__init__(pointer, type_id='', prior_ref_count = prior_ref_count)

    def _release_handle(self) -> bool:
        ut_dll.release(self.get_handle())
        return True

class Dog(CustomCffiNativeHandle):
    def __init__(self, pointer = None):
        if pointer is None:
            pointer = ut_dll.create_dog()
        super(Dog, self).__init__(pointer)
    # etc.

class DogOwner(CustomCffiNativeHandle):

    def __init__(self, dog):
        super(DogOwner, self).__init__(None)
        self._set_handle(ut_dll.create_owner(dog.get_handle()))
        self.dog = dog
        self.dog.add_ref() # Do note this important reference increment

    def say_walk(self):
        ut_dll.say_walk(self.get_handle())

    def _release_handle(self) -> bool:
        super(DogOwner, self)._release_handle()
        # super(DogOwner, self)._release_handle()
        self.dog.release()
        return True

class State:
    def __init__(self, instrument: int):
        self._self = feedlib.up_state_new(instrument)

    def __del__(self):
        feedlib.up_state_free(self._self)

    @property
    def sequence_id(self):
        return feedlib.up_state_get_sequence_id(self._self)

    @sequence_id.setter
    def set_sequence_id(self, seq_id):
        feedlib.up_state_set_sequence_id(self._self, seq_id)

    @property
    def bitset(self):
        return feedlib.up_state_get_bitset(self._self)

    def get_float(self, field):
        return feedlib.up_state_get_float(self._self, field)

    def get_uint(self, field):
        return feedlib.up_state_get_uint(self._self, field)

    def update_float(self, field, value):
        return feedlib.up_state_update_float(self._self, field, value)

    def update_uit(self, field, value):
        return feedlib.up_state_update_uint(self._self, field, value)


class Encoder:
    def __init__(self):
        self._self = feedlib.up_encoder_new()

    def __del__()
