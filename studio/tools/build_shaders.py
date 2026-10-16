"""Compile the studio's shaders for every graphics backend SDL3 supports, as
the port's own (port/tools/build_shaders.py, whose steps this reuses):

    python studio/tools/build_shaders.py [name ...]

Each studio/shaders/<name>.hlsl becomes studio/app/generated/<name>_<stage>.h
(DXIL, SPIR-V and MSL as C arrays). The generated headers are committed, so
building the studio needs no shader tools; run this after changing a shader,
with the Vulkan SDK installed (VULKAN_SDK set)."""
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("port_shaders", ROOT / "port" / "tools" / "build_shaders.py")
port = importlib.util.module_from_spec(spec)
spec.loader.exec_module(port)
port.SHADERS = ROOT / "studio" / "shaders"
port.OUT = ROOT / "studio" / "app" / "generated"

if __name__ == "__main__":
    # the headers name their source under studio/
    original = port.build

    def build(source, dxc, cross, versions, work):
        original(source, dxc, cross, versions, work)
        for stage in ("vs", "ps", "cs"):
            header = port.OUT / f"{source.stem}_{stage}.h"
            if header.exists():
                text = header.read_text(encoding="utf-8")
                text = text.replace("port/tools/build_shaders.py from port/shaders/",
                                    "studio/tools/build_shaders.py from studio/shaders/", 1)
                header.write_text(text, encoding="utf-8", newline="\n")

    port.build = build
    port.ROOT = ROOT
    sys.argv = [sys.argv[0]] + sys.argv[1:]
    # the work folder of the port's script, kept apart
    sys.exit(port.main())
