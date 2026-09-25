"""Exercise complete, truncated and torn DX.56 records."""

import importlib.util
import struct
from pathlib import Path

source = Path(__file__).with_name("decode-bc250-paging-trace.py")
spec = importlib.util.spec_from_file_location("paging_trace_decoder", source)
decoder = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(decoder)

record = bytearray(decoder.RECORD_SIZE)
decoder.HEADER.pack_into(record, 0, decoder.MAGIC, 1, 16, 1, 42, 0, 123456)
decoder.SLOT.pack_into(record, decoder.HEADER.size, 42, 23, 11, -1, 0xABCD, 123400)
checksum = decoder.fnv1a(record[:-decoder.TRAILER.size])
decoder.TRAILER.pack_into(record, len(record) - decoder.TRAILER.size, checksum, 0)
parsed = decoder.decode_record(bytes(record))
assert parsed["latest_sequence"] == 42
assert parsed["slots"][0]["thread"] == "0xABCD"
assert parsed["slots"][0]["status"] == "0xFFFFFFFF"

torn = bytearray(record)
torn[decoder.HEADER.size + 4] ^= 1
for bad in (bytes(torn), bytes(record[:-1])):
    try:
        decoder.decode_record(bad)
    except ValueError:
        pass
    else:
        raise AssertionError("registro corrompido foi aceito")

assert struct.pack("<I", decoder.MAGIC) in bytes(record)

# DX.65: v2 carrega o batimento do escritor em Reserved.
def make_v2(latest, tick, when):
    rec = bytearray(decoder.RECORD_SIZE)
    decoder.HEADER.pack_into(rec, 0, decoder.MAGIC, 2, 16, 1, latest, tick, when)
    decoder.SLOT.pack_into(rec, decoder.HEADER.size, latest, 5, 12, 0x103, 0x1D28, when)
    decoder.TRAILER.pack_into(rec, len(rec) - decoder.TRAILER.size,
                              decoder.fnv1a(rec[:-decoder.TRAILER.size]), 0)
    return bytes(rec)

v2 = decoder.decode_record(make_v2(77, 9, 5_000_000))
assert v2["version"] == 2 and v2["writer_tick"] == 9
# v1 com Reserved != 0 continua recusado.
bad_v1 = bytearray(record)
decoder.HEADER.pack_into(bad_v1, 0, decoder.MAGIC, 1, 16, 1, 42, 5, 123456)
decoder.TRAILER.pack_into(bad_v1, len(bad_v1) - decoder.TRAILER.size,
                          decoder.fnv1a(bad_v1[:-decoder.TRAILER.size]), 0)
try:
    decoder.decode_record(bytes(bad_v1))
except ValueError:
    pass
else:
    raise AssertionError("v1 com Reserved != 0 foi aceito")
print("DX.56 DECODER: PASS (completo, corrompido, truncado, v2 com batimento)")
