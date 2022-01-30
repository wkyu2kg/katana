############################
# Libraries used as plugins
############################

# try:
#     import katanagraph as _

#     has_katanagraph = True
# except ImportError:
#     has_katanagraph = False


import metagraph

# Use this as the entry_point object
registry = metagraph.PluginRegistry("katanagraph")


def find_plugins():
    # Ensure we import all items we want registered
    from . import katanagraph

    registry.register_from_modules(katanagraph)
    return registry.plugins
