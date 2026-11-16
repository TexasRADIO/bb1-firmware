"""Validate the supplied library against release metadata and source help pins."""
from pathlib import Path
import hashlib, json, re, struct, zlib

def require(condition, message):
    if not condition:
        raise ValueError(message)

def verify_library(path, source):
    source=Path(source);path=Path(path)
    spec=json.loads((source/'big-beat/sound-library.json').read_text())
    data=path.read_bytes();sha=hashlib.sha256(data).hexdigest()
    require(len(data)==spec['bytes'] and sha==spec['sha256'], 'wrong sound library')
    pin=(source/'firmware/src/bb/bb_text_pin.h').read_text()
    def number(name):
        return int(re.search(r'#define '+name+r' (0x[0-9a-fA-F]+|[0-9]+)u',pin).group(1),0)
    off=spec['help_offset'];size=number('BB_TX_PIN_BYTES')
    block=data[off:off+32+size]
    require(len(block)==32+size and len(block)<=spec['help_window_bytes'], 'help block bounds')
    magic,version,header,count,plen,crc=struct.unpack_from('<IBBHII',block)
    require((magic,version,header)==(0x58544242,1,32), 'help header')
    require(count==number('BB_TX_PIN_COUNT') and plen==size and crc==number('BB_TX_PIN_CRC'), 'help pins differ')
    require(zlib.crc32(block[32:])==crc and zlib.crc32(block[:28])==struct.unpack_from('<I',block,28)[0], 'help CRC')
    expected=re.search(r'#define BB_TX_BLOCK_SHA256 "([0-9a-f]{64})"',pin).group(1)
    require(hashlib.sha256(block).hexdigest()==expected, 'help block hash')
    return {'path':str(path.resolve()),'sha256':sha,'bytes':len(data),'help_block_sha256':expected,'source_help_pins_match':True}
