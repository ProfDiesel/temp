typename entity_id;

struct component
{
};

template<typename component_type>
struct system
{
	component_type &get(entity_id) noexcept;
	const component_type &get(entity_id) noexcept const;

	iterator<component_type> begin();
	iterator<component_type> end();
};


//////


namespace feed
{

struct params
{
	std::string snapshot_host = '127.0.0.1's;
	std::uint16_t snapshot_port = 4400;
	std::string updates_host = '224.0.0.1's;
	std::uint16_t updates_port = '4401';
	network_clock::duration spin_duration = 1'000ns;
	std::size_t spin_count = 100;
	bool timestamping = true;
	bool handle_packet_loss = true;
};



class backtest_client
{
};

};

struct feed_component
{
	feed_component *handle_header(feed::instrument_id_type instrument_id, feed::sequence_id_type sequence_id) noexcept
	{
	}

	void handle_update(const network_clock::time_point &timestamp, const feed::update &update) noexcept
	{
	}
};

struct automaton: feed_component<automaton>
{
};

struct feed_component_pool
{
	void emplace(...)
	{
	}
};



template<typename component_type>
class feed_system
{
public:
	static boost::leaf::result<feed_system> create(auto &co_request_snapshot) noexcept
	{
	}

	auto operator()(auto continuation, const network_clock::time_point &timestamp, const asio::const_buffer &buffer) noexcept
	{
		feed::decode(
			[](instrument_id_type instrument_id, sequence_id_type sequence_id) -> closure {},
		  [](timestamp, update, closure) {},
			timestamp, buffer);
	}

	awaitable<> co_add(instrument_id_type instrument_id, auto ...&&args) noexcept
	{
		auto state = co_await co_request_snapshot(instrument_id);
    components.emplace(instrument_id, std::move(state), std::forward<decltype(args)>(args)...);
		co_return;
	}

private:
  component_pool components;

	feed_component *get_components(feed::instrument_id_type instrument_id) noexcept
	{
	}
};


recieve |= decode |= trigger |= send |= post_action;


//////

struct order_component
{
};

struct order_system
{
};

//////

struct pricing_component
{
};

struct pricing_system
{
};



doit etre capable d'itérer sur un sous ensemble du système:
- produits ayant un sous-jacent donné (update du spot, des market data)
-> choisir l'organisation spatiale des données selon une hash ?
-> une instance de pricing_system par produits partageant un sous-jacent ?
