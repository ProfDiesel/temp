

class decoder : public refinitiv::ema::access::OmmConsumerClient
{
    decoder(auto &&message_header_handler, auto &&update_handler)
    {}

    auto operator ()()
    {
        consumer.dispatch();
    }

protected:
    void onUpdateMsg(const UpdateMsg &updateMsg, const OmmConsumerEvent &event)
    {
        cout << updateMsg << endl; // defaults to updateMsg.toString()
    }

    void onStatusMsg(const StatusMsg &statusMsg, const OmmConsumerEvent &event)
    {
        cout << statusMsg << endl; // defaults to statusMsg.toString()
    }

    void onRefreshMsg(const RefreshMsg &refreshMsg, const OmmConsumerEvent &event)
    {
        timestamp = clock::now();
        const auto instrument_closure = message_header_handler(refreshMsg.getServiceName(), refreshMsg.getName());
        if(UNLIKELY(!instrument_closure))
            continue;

        switch(refreshMsg.getPayload().getDataTyoe())
        {
            case DataType::FieldListEnum:
                std::for_each(refreshMsg.getPayload().getFieldList(), update_handler(timestamp, update, instrument_closure));
                break;
            case DataType::NoDataEnum:
                break;
        }
    }
}

OmmConsumer consumer(OmmConsumerConfig("emaconfig.xml").operationModel(OmmConsumerConfig::UserDispatchEnum));
consumer.registerClient(ReqMsg().serviceName("DIRECT_FEED").name("IBM.N"), client);
