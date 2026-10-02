"""pdb_publics.py -- Extract public symbols from a Windows PDB (MSF container).

The dwmcore.pdb already on this machine carries the exact GUID+age of the
installed dwmcore.dll, so the symbol map belongs to the running compositor and
is not an approximation.

Reads: MSF 7.00 superblock -> stream directory -> DBI stream (stream 3) to find
the publics/GSI stream index -> the symbol records. No external tool needed.

Demangling is delegated to Microsoft's own undname.exe, which sits in the same
MSVC toolset used to build the driver, so the C++ names come from the vendor.

Usage:
    python pdb_publics.py <file.pdb> --out symbols.tsv [--demangle]
    python pdb_publics.py <file.pdb> --grep Shader
"""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
from typing import Dict, List, Optional, Tuple

MSF_MAGIC = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\x00\x00\x00"
S_PUB32 = 0x110E

UNDNAME = (
    r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC"
    r"\14.51.36231\bin\Hostx64\x64\undname.exe"
)


class MSFError(Exception):
    pass


class MSF:
    def __init__(self, path: str) -> None:
        with open(path, "rb") as handle:
            self.data = handle.read()
        if not self.data.startswith(MSF_MAGIC):
            raise MSFError("assinatura MSF 7.00 ausente")
        (
            self.block_size,
            self.free_block_map,
            self.num_blocks,
            self.num_directory_bytes,
            self._unknown,
            self.block_map_addr,
        ) = struct.unpack_from("<IIIIII", self.data, 32)
        self._directory = self._read_directory()
        count = struct.unpack_from("<I", self._directory, 0)[0]
        sizes = list(struct.unpack_from(f"<{count}I", self._directory, 4))
        blocks_base = 4 + 4 * count
        self._stream_blocks: List[List[int]] = []
        for index in range(count):
            nblocks = struct.unpack_from("<I", self._directory, blocks_base + 4 * index)[0]
            base = blocks_base + 4 * (count + index)
            blocks = list(struct.unpack_from(f"<{nblocks}I", self._directory, base))
            self._stream_blocks.append(blocks)
        self._sizes = sizes

    def block(self, index: int) -> bytes:
        """Byte offset of a data block.

        Measured on this dwmcore.pdb: the block map (superblock field
        BlockMapAddr = 1244, the LAST block) sits at 1244 * block_size, which
        also makes the final block end exactly at EOF. The alternative
        convention (56 + (N-1)*block_size) lands on zeros and yields an empty
        stream directory -- a silent wrong answer, so this identity is asserted
        by the caller through the resulting stream count.
        """
        if index == 0:
            return b""
        start = index * self.block_size
        return self.data[start : start + self.block_size]

    def _read_directory(self) -> bytes:
        """BlockMapAddr is a block whose CONTENT is the array of stream-directory
        block indices (the array is block_size/4 - 1 long; the final dword of the
        block is the link to the next block-map block). Reading it as a linked
        list of directory blocks instead truncates the directory to one block and
        then fails to read the stream table.
        """
        out = bytearray()
        map_block = self.block_map_addr
        guard = 0
        while map_block not in (0, 0xFFFFFFFF) and len(out) < self.num_directory_bytes:
            block = self.block(map_block)
            capacity = (self.block_size // 4) - 1
            indices = struct.unpack_from(f"<{capacity}I", block, 0)
            for index in indices:
                if len(out) >= self.num_directory_bytes:
                    break
                if index == 0:
                    continue
                out += self.block(index)
            map_block = struct.unpack_from("<I", block, len(block) - 4)[0]
            guard += 1
            if guard > 10000:
                break
        return bytes(out[: self.num_directory_bytes])

    def stream(self, index: int) -> bytes:
        if index >= len(self._stream_blocks):
            return b""
        out = bytearray()
        for block in self._stream_blocks[index]:
            out += self.block(block)
        return bytes(out[: self._sizes[index]])

    def stream_count(self) -> int:
        return len(self._stream_blocks)

    def stream_size(self, index: int) -> int:
        return self._sizes[index] if index < len(self._sizes) else 0


def dbi_public_stream(msf: MSF) -> Tuple[int, dict]:
    """Return the publics stream index and the DBI header fields."""
    dbi = msf.stream(3)
    if len(dbi) < 32:
        raise MSFError("stream DBI (3) ausente ou curto demais")
    (
        version_signature,
        version_header,
        age,
        global_stream,
        build_number,
        public_stream,
    ) = struct.unpack_from("<IIIHHH", dbi, 0)
    (
        mod_info_size,
        section_contribution_size,
        section_map_size,
        source_info_size,
        type_server_map_size,
    ) = struct.unpack_from("<iiiii", dbi, 24)
    mfc_type_server = struct.unpack_from("<I", dbi, 44)[0]
    optional_dbg_header_size = struct.unpack_from("<i", dbi, 48)[0]
    ec_substream_size = struct.unpack_from("<i", dbi, 52)[0]
    flags = struct.unpack_from("<H", dbi, 56)[0]
    machine = struct.unpack_from("<H", dbi, 58)[0]
    header = {
        "version_signature": version_signature,
        "version_header": version_header,
        "age": age,
        "global_stream_index": global_stream,
        "build_number": build_number,
        "public_stream_index": public_stream,
        "mod_info_size": mod_info_size,
        "section_map_size": section_map_size,
        "optional_dbg_header_size": optional_dbg_header_size,
        "ec_substream_size": ec_substream_size,
        "flags": flags,
        "machine": machine,
        "mfc_type_server_index": mfc_type_server,
    }
    return public_stream, header


def publics(msf: MSF, stream_index: int) -> List[Dict[str, object]]:
    data = msf.stream(stream_index)
    if len(data) < 28:
        raise MSFError(f"stream publics {stream_index} ausente")
    version, sym_hash, num_buckets, num_symbols = struct.unpack_from("<IIII", data, 0)
    if version != 20040203:
        # reported, not fatal: the record walk below does not depend on it
        pass
    out: List[Dict[str, object]] = []
    cursor = 24 + 4 * num_buckets
    while cursor + 8 <= len(data) and len(out) < num_symbols:
        record_len, _count = struct.unpack_from("<II", data, cursor)
        record = data[cursor + 8 : cursor + 8 + record_len]
        cursor += 8 + record_len
        if len(record) < 2:
            continue
        record_kind = struct.unpack_from("<H", record, 0)[0]
        if record_kind == S_PUB32 and len(record) >= 16:
            flags, frame, offset, length = struct.unpack_from("<IIII", record, 8)
            end = record.find(b"\0", 16)
            name = record[16:end].decode("ascii", "replace") if end > 16 else ""
            out.append(
                {
                    "kind": record_kind,
                    "flags": flags,
                    "frame": frame,
                    "offset": offset,
                    "length": length,
                    "name": name,
                }
            )
    return out


def demangle(names: List[str]) -> List[str]:
    if not os.path.isfile(UNDNAME):
        return names
    try:
        completed = subprocess.run(
            [UNDNAME] + names,
            capture_output=True,
            text=True,
            timeout=600,
        )
    except Exception:  # noqa: BLE001
        return names
    lines = [line for line in completed.stdout.splitlines() if line.strip()]
    if len(lines) != len(names):
        return names
    return lines


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("pdb")
    parser.add_argument("--out")
    parser.add_argument("--demangle", action="store_true")
    parser.add_argument("--grep")
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    msf = MSF(args.pdb)
    print(f"[msf] block_size={msf.block_size} blocks={msf.num_blocks} streams={msf.stream_count()}")
    public_index, header = dbi_public_stream(msf)
    print(f"[dbi] {header}")
    if public_index == 0xFFFF:
        print("[erro] DBI sem stream publics (PDB privado sem simbolos publicos?)")
        return 2
    symbols = publics(msf, public_index)
    print(f"[publics] stream={public_index} {len(symbols)} registros S_PUB32")

    names = [str(s["name"]) for s in symbols]
    if args.demangle:
        names = demangle(names)

    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            handle.write("frame\toffset\tlength\tflags\tname\n")
            for symbol, pretty in zip(symbols, names):
                handle.write(
                    f"{symbol['frame']}\t{symbol['offset']}\t{symbol['length']}"
                    f"\t{symbol['flags']}\t{pretty}\n"
                )
        print(f"[tsv] {args.out}")

    rows = [
        (s, p)
        for s, p in zip(symbols, names)
        if args.grep is None or args.grep.lower() in str(p).lower()
    ]
    if args.limit:
        rows = rows[: args.limit]
    for symbol, pretty in rows:
        print(f"  {symbol['frame']:>4}:0x{symbol['offset']:08X}  {pretty}")
    if args.grep:
        print(f"[grep '{args.grep}'] {len(rows)} simbolos")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())