def format_value(value):
    if isinstance(value, str):
        return f'\'{value}\''
    return str(value)

def format_config(config):
    return '\n'.join(f'{key} <- {format_value(value)};' for key, value in config)

payload_command = '''
entrypoint.type <- 'payload';
entrypoint.instrument <- {instrument};
entrypoint.message <- '{base64(message)}';
entrypoint.datagram <- '{base64(datagram)}';

'''

subscribe_command = '''
'''

unsubscribe_command = '''
'''

quit_command = '''
entrypoint.type <- 'quit';

'''

class Fairy:
    def __init__(self, config):
        self.__config = config

    async def setup(self, loop, config):
        self.process = await asyncio.create_subprocess_shell(self.__config['executable'], stdin=PIPE, stdout=PIPE, stderr=PIPE, close_fds=False)
        loop.create_task(self.__command_reader(loop))
        loop.create_task(self.__stderr_reader(loop))
        logging.info(config)

        config = f'''
config.feed <- 'feed';
config.send <- 'send';
config.subscription <- 'subscription';
config.command_out_fd <- 1;
{format_config(self.__config)}
send.fd <- {{down_fd}};
subscription.message <- '{{message_payload}}';
subscription.datagram <- '{{datagram_payload}}';
'''
        self.process.stdin.write(config.encode())
        await self.process.stdin.drain()

    async def quit(self):
        self.process.stdin.write(quit_command.encode())
        await self.process.stdin.drain()
        return_code = await self.process.wait()
        logging.info(return_code)

    async def __command_reader(self, loop):
        while True:
            command = await self.process.stdout.read_until('\n\n'):
        '''
request.type <- request_payload; \n\
request.instrument = {}\n\n");
            '''
            logging.info(line)

    async def __stderr_reader(self, loop):
        async for line in self.process.stderr:
            logging.info(line)

async def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument('--logging-ini')
    parser.add_argument('--config', default='ppf.json')
    args = parser.parse_args(argv)

    if args.logging_ini:
        logging.config.fileConfig(args.logging_ini)
    else:
        logging.basicConfig(level=logging.INFO)

    fairy = Fairy(json.load(args.config))
    await fairy.run()
