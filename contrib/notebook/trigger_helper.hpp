#pragma once

#include <feed/feed_structures.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

class polymorphic_trigger_dispatcher;

extern "C" polymorphic_trigger_dispatcher *_make_trigger(const char *config, char **error_message);
extern "C" void _release_error_message(char *error_message);
extern "C" bool _on_update(polymorphic_trigger_dispatcher *trigger, std::int64_t timestamp, const feed::update *update);
extern "C" void _release_trigger(polymorphic_trigger_dispatcher *ptr);

namespace wrapped
{
  class trigger
  {
  public:
    trigger() = default;

    trigger(std::string_view config)
    {
      char *message = nullptr;
      auto ptr = _make_trigger(config.data(), &message);
      if(!ptr && *message)
      {
        std::runtime_error exception(message);
        _release_error_message(message);
        throw exception;
      }
      this->ptr.reset(ptr, _release_trigger);
    }

    bool operator()(std::int64_t timestamp, const feed::update &update) { return _on_update(ptr.get(), timestamp, &update); }

  private:
    std::shared_ptr<polymorphic_trigger_dispatcher> ptr {};
  };
}