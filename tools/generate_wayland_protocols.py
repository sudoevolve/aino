"""更新 aino.hpp 内的协议 ABI；开发时使用，用户编译不需要协议 XML 或 scanner。"""

import argparse
from pathlib import Path
import textwrap
import xml.etree.ElementTree as ET


def generate(wayland: Path, protocols: Path) -> str:
    paths = [wayland, *[protocols / name for name in (
        "stable/xdg-shell/xdg-shell.xml",
        "stable/viewporter/viewporter.xml",
        "staging/fractional-scale/fractional-scale-v1.xml",
        "unstable/text-input/text-input-unstable-v3.xml",
        "unstable/xdg-decoration/xdg-decoration-unstable-v1.xml",
    )]]
    roots = [ET.parse(path).getroot() for path in paths]
    interfaces = [item for root in roots for item in root.findall("interface")]
    lines = ["// BEGIN AINO WAYLAND PROTOCOLS (tools/generate_wayland_protocols.py)",
             "// 协议布局来自 Wayland / wayland-protocols XML，保留原协议名称与版本。"]
    for root in roots:
        copyright = root.findtext("copyright")
        if copyright:
            lines.extend([f'/* {root.get("name")}', textwrap.dedent(copyright).strip(), "*/"])
    for interface in interfaces:
        lines.append(f'extern const Interface {interface.get("name")};')
    kinds = dict(int="i", uint="u", fixed="f", string="s", object="o", new_id="n", array="a", fd="h")
    for interface in interfaces:
        name = interface.get("name")
        counts = []
        for kind in ("request", "event"):
            messages = interface.findall(kind)
            counts.append(len(messages))
            entries = []
            for index, message in enumerate(messages):
                args = message.findall("arg")
                signature = message.get("since", "")
                if signature == "1":
                    signature = ""
                types = []
                for arg in args:
                    code = kinds[arg.get("type")]
                    if code == "n" and not arg.get("interface"):
                        signature += "su"
                    signature += ("?" if arg.get("allow-null") == "true" else "") + code
                    types.extend(["nullptr"] * (3 if code == "n" and not arg.get("interface") else 1))
                    if arg.get("interface"):
                        types[-1] = f'&{arg.get("interface")}'
                table = "nullptr"
                if types:
                    table = f"{name}_{kind}_types_{index}"
                    lines.append(f'inline const Interface* {table}[] = {{{", ".join(types)}}};')
                entries.append(f'{{"{message.get("name")}", "{signature}", {table}}}')
            if entries:
                lines.append(f'inline const Message {name}_{kind}s[] = {{')
                lines.extend(f"        {entry}," for entry in entries)
                lines.append("};")
        requests = f"{name}_requests" if counts[0] else "nullptr"
        events = f"{name}_events" if counts[1] else "nullptr"
        lines.append(f'inline const Interface {name}{{"{name}", {interface.get("version")}, '
                     f'{counts[0]}, {requests}, {counts[1]}, {events}}};')
    lines.append("// END AINO WAYLAND PROTOCOLS")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wayland", type=Path, default=Path("/usr/share/wayland/wayland.xml"))
    parser.add_argument("--protocols", type=Path, default=Path("/usr/share/wayland-protocols"))
    parser.add_argument("--header", type=Path, default=Path(__file__).resolve().parents[1] / "aino.hpp")
    args = parser.parse_args()
    source = args.header.read_text(encoding="utf-8")
    begin = source.index("// BEGIN AINO WAYLAND PROTOCOLS")
    end = source.index("// END AINO WAYLAND PROTOCOLS", begin) + len("// END AINO WAYLAND PROTOCOLS")
    args.header.write_text(source[:begin] + generate(args.wayland, args.protocols) + source[end:],
                           encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
