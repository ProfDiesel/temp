#include "feed_text.hpp"

#include <gsl/pointers>

#include <observer_ptr/observer_ptr.hpp>

#include <random>

using gen_ptr = gsl::not_null<cci::observer_ptr<std::mt19937>>;

struct random_generator
{
  ifstream input;

  template<typename value_type>
  value_type get() noexcept 
  {
    std::aligned_storage_t<value_type, alignof(value_type)> storage;
    if(input.read(storage) < sizeof(value_type))
      bad_test();
    return *std::launder(reinterpret_cast<value_type*>(&storage));
  };
};

template<typename value_type>
struct mutator;

template<typename value_type>
void mutate(value_type &value)
{
  mutator<value_type> {}(value);
}

template<>
struct mutator<int> : basic_mutator
{
  void operator()(int &value) noexcept
  {
  }
};

template<typename value_type>
struct mutator<std::vector<value_type>> : basic_mutator
{
  void operator()(std::vector<value_type> &value) noexcept
  {
    switch(value.empty() ? 0 : random<>(0, 3))
    {
    case 0:
    {
      auto it = value.begin() + random<>(0, value.size() + 1);
      value.emplace(it, {});
      mutate(*it);
    }
    break;
    case 1:
    {
      auto it = value.begin() + random<>(0, value.size());
      value.erase(it);
    }
    break;
    case 2:
    {
      auto it = value.begin() + random<>(0, value.size());
      mutate(*it);
    }
    break;
    }
  }
};

template<feed::field field>
struct mutator<feed::field_c<field>> : basic_mutator
{
  template<typename value_type>
  void operator()(value_type &value) noexcept
  {
    mutate(value);
  }
};

template<>
struct mutator<feed::update> : basic_mutator
{
  void operator()(feed::update &value) noexcept
  {
    feed::visit_update([&](auto field, auto &&value) { value = mutate<decltype(field), decltype(value)>(value); }, value);
  }
};

struct message_instrument;
template<>
struct mutator<message_instrument> : basic_mutator
{
  void operator()(feed::instrument_id_type &value) noexcept { mutate(value); }
};

struct message_sequence_id;
template<>
struct mutator<message_sequence_id> : basic_mutator
{
  void operator()(feed::sequence_id_type &value) noexcept {}
};

template<>
struct mutator<feed::details::message> : basic_mutator
{
  void operator()(feed::details::message &message) noexcept
  {
    switch(random<>(0, 3))
    {
    case 0: mutate<message_instrument>(message.instrument); break;
    case 1: mutate<message_sequence_id>(message.sequence_id); break;
    case 2: mutate(message.updates); break;
    }
  }
};

void sanitize(std::vector<feed::details::message> &messages)
{
  feed::sequence_id_type sequence_id {};
  for(auto &&message: messages)
  {
    message.sequence_id = sequence_id++;
  }
}

std::vector<feed::details::message> decode(std::string_view input) {}
std::string encode(const std::vector<feed::details::message> &input) {}

extern "C" fuzz(const char *input_buffer, char **output_buffer)
{
  auto messages = decode(input_buffer);
  mutate(messages);
  sanitize(messages);
  auto &&encoded = encode(messages);
  output_buffer = encoded.detach();
}

using feed::update;

struct message
{
  instrument_id_type instrument;
  sequence_id_type sequence_id;

  std::vector<update> updates;
};

struct packet
{
  std::vector<message> messages;
};

void *pack(const packet *packet, void *ptr)
{
  auto result = new(ptr) feed::packet {.nb_messages = packet.messages.size()};
  for(auto &&message: packet.messages)
  {
    for(auto &&update: message.updates) {}
  }
  for(auto &&[message_index, message_addr] = {0, &result->message}; message_index < result->nb_messages; ++message_index)
  {
    auto current_message = new(message_addr) message {.intrument = instrument, .sequence_id = sequence_id, .nb_updates = random<std::size_t>(0, 5)};
    for(auto &&[update_index, udpate_addr] = {0, &current_message->update}; update_index < result->nb_updates; ++update_index)
    {
      *update_addr++ = random_update(instrument);
    }
  }
}

  auto mutate_packet(packet *addr_in, packet *addr)
  {
    auto result = new(addr) packet {.nb_messages = random<std::size_t>(0, 1000)};
    for(auto &&[message_index, message_addr] = {0, &result->message}; message_index < result->nb_messages; ++message_index)
    {
      auto current_message = new(message_addr) message {.intrument = instrument, .sequence_id = sequence_id, .nb_updates = random<std::size_t>()};
      for(auto &&[update_index, udpate_addr] = {0, &current_message->update}; update_index < result->nb_updates; ++update_index)
      {
        *update_addr++ = random_element(random_update_factory)(instrument);
      }
    }
  }
  auto sanitize_packet(packet *addr_in, packet *addr) {}

