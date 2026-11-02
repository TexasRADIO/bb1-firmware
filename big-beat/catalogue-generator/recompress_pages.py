#!/usr/bin/env python3
"""Re-encode a BBPC catalogue as fixed-Huffman v3; no device access.

Usage: python3 recompress_pages.py INPUT.bbpc OUTPUT.bbpc
The input is preserved. The output must not already exist.
"""
import argparse
import hashlib
import json
import pathlib
import zlib
import bbpc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    original = args.input.read_bytes()
    value = bbpc.decode(original)
    result, _ = bbpc.encode(value, version=3)
    if bbpc.decode(result) != value:
        raise ValueError("Catalogue value roundtrip failed")
    with args.output.open("xb") as stream:
        stream.write(result)
    print(json.dumps({"input_bytes": len(original), "output_bytes": len(result),
        "saving_bytes": len(original)-len(result),
        "input_sha256": hashlib.sha256(original).hexdigest(),
        "output_sha256": hashlib.sha256(result).hexdigest(),
        "zlib_build": zlib.ZLIB_VERSION, "zlib_runtime": zlib.ZLIB_RUNTIME_VERSION}))


if __name__ == "__main__":
    main()
