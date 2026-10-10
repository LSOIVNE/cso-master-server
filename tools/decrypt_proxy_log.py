#!/usr/bin/env python3
"""
解密 packet_proxy.log 中官方 SGP 服務端的加密 TCP 串流。

CSO 協定（由 cso-master-server 的 tcp_connection.cpp 得知）:
  - 每個 TCP 封包 = [0x55][seq:u8][len:u16 LE] + payload
  - payload 第一個 byte 是 PacketID；PacketID 0x07 = Crypt
  - Crypt 封包格式: PacketID(0x07) type(u8) method(u8) [key(32) iv(32)]
      type 0 = Encrypt(伺服器->客戶端用的金鑰), 1 = Decrypt(客戶端->伺服器)
      method: 0/1=CleanUp, 2=RC4, 3=RC4_40, 4=Null
  - method=RC4 時，之後該方向的 payload 用 RC4 連續流加密（header 不加密）
  - 注意: OpenSSL EVP_rc4() 預設 key length = 16，所以只取 key 前 16 bytes

用法:
    python decrypt_proxy_log.py packet_proxy.log
    python decrypt_proxy_log.py packet_proxy.log --dir S2C --key <hex>
"""

import argparse
import re
import sys

LINE_RE = re.compile(
    r"\[(?P<ts>[^\]]+)\]\s+\[(?P<dir>TCP [CS]->[CS])(?P<extra>[^\]]*)\]\s+\S+\s+\((\d+)B\):\s+(?P<hex>(?:[0-9A-F]{2} )+[0-9A-F]{2})"
)

PACKET_ID_NAMES = {
    0x00: "Ping",
    0x01: "ServerList",
    0x02: "Reply",
    0x03: "Login",
    0x04: "Transfer",
    0x05: "Character",
    0x06: "Version",
    0x07: "Crypt",
    0x08: "ServerInfo",
    0x0C: "Option",
    0x10: "UMsg",
    0x14: "Character",
    0x41: "Room",
    0x44: "Host",
    0x4C: "Option",
    0x4D: "Item",
    0x4F: "Shop",
    0x50: "Favorite",
    0x55: "Quest",
    0x59: "Unknown59",
}


def rc4(key: bytes):
    s = list(range(256))
    j = 0
    for i in range(256):
        j = (j + s[i] + key[i % len(key)]) & 0xFF
        s[i], s[j] = s[j], s[i]
    i = j = 0
    while True:
        i = (i + 1) & 0xFF
        j = (j + s[i]) & 0xFF
        s[i], s[j] = s[j], s[i]
        yield s[(s[i] + s[j]) & 0xFF]


def parse_log(path: str, direction: str):
    """回傳 [(ts, packet_bytes), ...]，只取指定方向的 TCP 封包。"""
    packets = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = LINE_RE.search(line)
            if not m:
                continue
            d = m.group("dir")
            if direction == "S2C" and d != "TCP S->C":
                continue
            if direction == "C2S" and d != "TCP C->S":
                continue
            data = bytes.fromhex(m.group("hex"))
            packets.append((m.group("ts"), data))
    return packets


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("log", nargs="?", default="packet_proxy.log")
    ap.add_argument("--dir", choices=["S2C", "C2S"], default="S2C")
    ap.add_argument("--key", default=None, help="手動指定 RC4 key (hex)，否則自動從 Crypt 封包取")
    ap.add_argument("--keylen", type=int, default=16, help="RC4 key 長度 (OpenSSL EVP_rc4 預設 16)")
    args = ap.parse_args()

    packets = parse_log(args.log, args.dir)
    if not packets:
        print("找不到封包"); return 1

    print(f"== 讀到 {len(packets)} 個 {args.dir} TCP 封包 ==")

    key = bytes.fromhex(args.key) if args.key else None
    stream = None
    crypt_seen = False

    for ts, data in packets:
        if len(data) < 5 or data[0] != 0x55:
            continue
        length = data[2] | (data[3] << 8)
        payload = data[4:4 + length]
        if len(payload) < 1:
            continue

        # 若尚未取得金鑰，先看這是不是 Crypt 封包
        if key is None:
            if payload[0] == 0x07 and len(payload) >= 3:
                ctype, method = payload[1], payload[2]
                body = payload[3:]
                if method in (0, 1):
                    print(f"[{ts}] Crypt type={ctype} method={method} (CleanUp, 之後不加密)")
                    key = b""            # 代表不加密
                    stream = None
                else:
                    k = body[:args.keylen]
                    iv = body[32:64]
                    key = k
                    stream = rc4(k)
                    print(f"[{ts}] Crypt type={ctype} method={method} key={k.hex()} iv={iv.hex()[:32]}")
                    print(f"        -> 之後 {args.dir} payload 用 RC4 解密 (keylen={args.keylen})")
                crypt_seen = True
                continue
            # 還沒加密前的明文封包
            name = PACKET_ID_NAMES.get(payload[0], f"0x{payload[0]:02X}")
            print(f"[{ts}] (明文) PacketID={name} sub={payload[1] if len(payload)>1 else '-'} len={length} : {payload.hex()}")
            continue

        # 有金鑰 -> 解密
        if stream is None:
            out = payload            # key == b"" 表示不加密
        else:
            out = bytes(b ^ next(stream) for b in payload)
        pid = out[0]
        sub = out[1] if len(out) > 1 else -1
        name = PACKET_ID_NAMES.get(pid, f"0x{pid:02X}")
        head = out[:40].hex()
        print(f"[{ts}] PacketID={name}(0x{pid:02X}) sub={sub} len={length} : {head}{'...' if len(out)>40 else ''}")
        if pid == 0x41:
            print(f"        ROOM payload: {out.hex()}")

    if not crypt_seen:
        print("(沒有看到 Crypt 封包)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
