"""Build a macOS icon from the original 16px pixel artwork; no dependencies."""
from pathlib import Path
import argparse
import struct
from make_icons import read_png, png, scale


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    base = read_png(root / "images" / "icon.png")
    chunks = []
    for code, size in ((b"icp4", 16), (b"icp5", 32), (b"icp6", 64),
                       (b"ic07", 128), (b"ic08", 256), (b"ic09", 512), (b"ic10", 1024)):
        data = png(scale(base, size // 16))
        chunks.append(code + struct.pack(">I", len(data) + 8) + data)
    body = b"".join(chunks)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(b"icns" + struct.pack(">I", len(body) + 8) + body)
    print(f"Built {args.output} from images/icon.png")


if __name__ == "__main__":
    main()
