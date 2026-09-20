# Injects the firmware version (-D MESHCACHING_VERSION) at build time:
# the git tag on a release, "v1.2.3-4-gabc123-dirty" on an intermediate
# build, "dev" outside a git repository.
import subprocess

Import("env")


def git_version():
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            text=True,
        ).strip()
    except Exception:
        return "dev"


version = git_version()
print("Firmware version: %s" % version)
env.Append(CPPDEFINES=[("MESHCACHING_VERSION", env.StringifyMacro(version))])
