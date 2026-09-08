#!/usr/bin/env python3
"""
Format:
    Filetype: Flipper Apps Index
    Version: 1

    [<api> <target> <tag>]
    <pack>: <url of the pack archive>
    <appid>:<version>:<folder>:<pack>:<size>:<md5>:<icon>:<name>
"""

import argparse
import base64
import hashlib
import os
import struct
import sys

FAPMETA_SECTION = b".fapmeta"
FAPMETA_MAGIC = 0x52474448
FAPMETA_VERSION = 1
NAME_OFFSET = 20
NAME_LENGTH = 32
HAS_ICON_OFFSET = NAME_OFFSET + NAME_LENGTH
ICON_OFFSET = HAS_ICON_OFFSET + 1
ICON_LENGTH = 32

def read_fapmeta(data):
    if data[:4] != b"\x7fELF":
        return None
    (shoff,) = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    if not shoff or not shnum:
        return None

    def header(index):
        return struct.unpack_from("<IIIIII", data, shoff + index * shentsize)

    _, _, _, _, stroff, strsize = header(shstrndx)
    names = data[stroff : stroff + strsize]
    for index in range(shnum):
        name_offset, _, _, _, offset, size = header(index)
        end = names.index(b"\0", name_offset)
        if names[name_offset:end] == FAPMETA_SECTION:
            return data[offset : offset + size]
    return None

def parse_manifest(section):
    if section is None or len(section) < ICON_OFFSET + ICON_LENGTH:
        return None
    magic, version = struct.unpack_from("<II", section, 0)
    if magic != FAPMETA_MAGIC or version != FAPMETA_VERSION:
        return None
    api_minor, api_major, target, _stack, ver_minor, ver_major = struct.unpack_from(
        "<HHHHHH", section, 8
    )
    name = section[NAME_OFFSET : NAME_OFFSET + NAME_LENGTH].split(b"\0")[0]
    icon = b""
    if section[HAS_ICON_OFFSET]:
        icon = section[ICON_OFFSET : ICON_OFFSET + ICON_LENGTH]
    return {
        "api": "%d.%d" % (api_major, api_minor),
        "target": "f%d" % target,
        "version": "%d.%d" % (ver_major, ver_minor),
        "name": name.decode("ascii", "replace").replace(":", " ").strip(),
        "icon": base64.b64encode(icon).decode("ascii"),
    }

def scan_pack(pack, root):
    apps = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for filename in sorted(filenames):
            if not filename.endswith(".fap"):
                continue
            path = os.path.join(dirpath, filename)
            with open(path, "rb") as handle:
                data = handle.read()
            manifest = parse_manifest(read_fapmeta(data))
            if manifest is None:
                print("%s: no usable manifest, skipped" % path, file=sys.stderr)
                continue
            folder = os.path.relpath(dirpath, root).replace(os.sep, "/")
            manifest.update(
                appid=filename[:-4],
                folder="" if folder == "." else folder,
                pack=pack,
                size=len(data),
                md5=hashlib.md5(data).hexdigest(),
            )
            apps.append(manifest)
    apps.sort(key=lambda app: (app["folder"], app["appid"]))
    return apps

def main():
    parser = argparse.ArgumentParser(description="Build the apps index of a release")
    parser.add_argument("--tag", required=True, help="release tag")
    parser.add_argument(
        "--url",
        required=True,
        help="archive URL template, with {tag} and {pack} placeholders",
    )
    parser.add_argument(
        "packs",
        nargs="+",
        metavar="pack=path",
        help="pack name and the artifacts root that holds its category folders",
    )
    args = parser.parse_args()

    apps = []
    packs = []
    for entry in args.packs:
        pack, _, root = entry.partition("=")
        if not pack or not root:
            parser.error('expected "pack=path", got "%s"' % entry)
        if not os.path.isdir(root):
            parser.error('no such artifacts root: "%s"' % root)
        packs.append(pack)
        apps.extend(scan_pack(pack, root))

    if not apps:
        parser.error("no .fap files found, nothing to index")

    apis = {(app["api"], app["target"]) for app in apps}
    api, target = sorted(apis)[-1]
    for odd in sorted(apis - {(api, target)}):
        print(
            "warning: %s %s apps present, index says %s %s" % (odd + (api, target)),
            file=sys.stderr,
        )

    out = ["Filetype: Flipper Apps Index", "Version: 1", ""]
    out.append("[%s %s %s]" % (api, target, args.tag))
    for pack in packs:
        out.append("%s: %s" % (pack, args.url.format(tag=args.tag, pack=pack)))
    for app in apps:
        out.append(
            "%s:%s:%s:%s:%d:%s:%s:%s"
            % (
                app["appid"],
                app["version"],
                app["folder"],
                app["pack"],
                app["size"],
                app["md5"],
                app["icon"],
                app["name"],
            )
        )
    print("\n".join(out))


if __name__ == "__main__":
    main()
