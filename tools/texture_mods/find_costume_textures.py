"""Lists the textures that only one of two texture dumps contains.

Make two dumps (--sr_texture_dump_dir) at the same point of the game, one per
costume, then:

    python tools/texture_mods/find_costume_textures.py <dump_without> <dump_with>

Prints "key,width,height,format" for every key in <dump_with>/index.csv that
<dump_without>/index.csv lacks, in dump order. City streaming also differs
between runs, so the list still needs a look at the PNGs.
"""
import csv
import sys
from pathlib import Path


def read_index(dump: Path) -> list[dict]:
    with open(dump / "index.csv", newline="", encoding="utf-8") as file:
        return list(csv.DictReader(file))


def only_in(without: list[dict], with_: list[dict]) -> list[dict]:
    known = {row["key"] for row in without}
    return [row for row in with_ if row["key"] not in known]


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    rows = only_in(read_index(Path(sys.argv[1])), read_index(Path(sys.argv[2])))
    print("key,width,height,format")
    for row in rows:
        print(f'{row["key"]},{row["width"]},{row["height"]},{row["format"]}')
    return 0


if __name__ == "__main__":
    sys.exit(main())
