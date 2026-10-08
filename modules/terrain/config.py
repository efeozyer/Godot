def can_build(env, platform):
    env.module_add_dependencies("terrain", ["fx"])
    return not env["disable_3d"]


def configure(env):
    pass
