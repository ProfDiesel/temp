from ppf.config.marshalling import GRAMMAR

def test_config():
    GRAMMAR.parseString('"ppf.executable": "ppf", "ppf.type": "ppf"')


