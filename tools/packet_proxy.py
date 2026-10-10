#!/usr/bin/env python3
"""
CSO 封包記錄代理 (TCP + UDP)

用途
----
在不修改官方 SGP 服務端 exe 的前提下，把「客戶端 <-> 服務端」之間所有封包
完整記錄下來，用來比對官方服務端到底送了什麼（例如大頭/道具 439 相關旗標）。

拓樸
----
    客戶端  ->  本代理 (listen)  ->  官方服務端 (target)

因此請把官方服務端的 port 改掉（例如 30003），本代理 listen 在原 port（30002），
客戶端設定完全不用動。

用法
----
    python packet_proxy.py                       # 30002 -> 127.0.0.1:30003
    python packet_proxy.py --target-port 30003 --listen-port 30002
    python packet_proxy.py --log out.log

會同時輸出到螢幕與 --log 指定的檔案。

CSO 封包格式（本專案逆向所得）
------------------------------
    [0x55][seq:u8][len:u16 LE]  4 bytes header，之後為 payload
    payload = [PacketID:u8][subtype:u8][...]
    常見 PacketID: 0x41=Room, 0x44=Host, 0x4C=Option, 0x5A=?, 0x4D=Item ...
"""

import argparse
import datetime
import socket
import struct
import sys
import threading

LOG_LOCK = threading.Lock()
LOG_FILE = None

PACKET_ID_NAMES = {
    0x10: "Character",
    0x14: "Character?",
    0x41: "Room",
    0x44: "Host",
    0x4C: "Option",
    0x4D: "Item",
    0x5A: "Unknown5A",
}


def now() -> str:
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def emit(line: str) -> None:
    with LOG_LOCK:
        print(line, flush=True)
        if LOG_FILE:
            LOG_FILE.write(line + "\n")
            LOG_FILE.flush()


def hexdump(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def decode_cso(data: bytes):
    """嘗試解析 CSO 封包，回傳 (描述, 是否成功)。"""
    if len(data) < 5:
        return None
    if data[0] != 0x55:
        return None
    seq = data[1]
    length = struct.unpack_from("<H", data, 2)[0]
    payload = data[4:4 + length]
    if len(payload) < 2:
        return None
    pid = payload[0]
    sub = payload[1]
    name = PACKET_ID_NAMES.get(pid, f"0x{pid:02X}")
    return f"PacketID={name} subtype={sub} seq={seq} len={length}"


def log_chunk(tag: str, peer: str, data: bytes) -> None:
    desc = decode_cso(data)
    suffix = f"  <-- {desc}" if desc else ""
    emit(f"[{now()}] {tag} {peer} ({len(data)}B): {hexdump(data)}{suffix}")


def split_stream(buf: bytearray):
    """把 TCP 位元組流切成一個個 CSO 封包 [0x55][seq][len:u16][payload...]。"""
    while True:
        # 對齊到 0x55 開頭
        while buf and buf[0] != 0x55:
            del buf[0]
        if len(buf) < 4:
            return
        length = buf[2] | (buf[3] << 8)
        total = 4 + length
        if total < 5 or total > 65535:
            del buf[0]          # 標頭不合理，丟掉一個 byte 重新同步
            continue
        if len(buf) < total:
            return
        packet = bytes(buf[:total])
        del buf[:total]
        yield packet


# --------------------------------------------------------------------------- TCP
def tcp_pump(src: socket.socket, dst: socket.socket, tag: str, peer: str) -> None:
    buf = bytearray()
    try:
        while True:
            chunk = src.recv(65536)
            if not chunk:
                break
            dst.sendall(chunk)
            buf += chunk
            for packet in split_stream(buf):
                log_chunk(tag, peer, packet)
        if buf:
            log_chunk(tag + " (殘留)", peer, bytes(buf))
    except OSError:
        pass
    finally:
        for s in (src, dst):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                s.close()
            except OSError:
                pass


def handle_tcp_client(client: socket.socket, addr, target_host: str, target_port: int) -> None:
    peer = f"{addr[0]}:{addr[1]}"
    try:
        upstream = socket.create_connection((target_host, target_port), timeout=10)
        upstream.settimeout(None)
    except OSError as exc:
        emit(f"[{now()}] [TCP] !! 無法連線到官方服務端 {target_host}:{target_port}: {exc}")
        client.close()
        return

    emit(f"[{now()}] [TCP] 客戶端 {peer} 已連線 -> {target_host}:{target_port}")
    client.settimeout(None)
    t1 = threading.Thread(target=tcp_pump, args=(client, upstream, "[TCP C->S]", peer), daemon=True)
    t2 = threading.Thread(target=tcp_pump, args=(upstream, client, "[TCP S->C]", peer), daemon=True)
    t1.start()
    t2.start()


def tcp_server(listen_host: str, listen_port: int, target_host: str, target_port: int) -> None:
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((listen_host, listen_port))
    srv.listen(16)
    emit(f"[{now()}] [TCP] 監聽 {listen_host}:{listen_port} -> {target_host}:{target_port}")
    while True:
        try:
            client, addr = srv.accept()
        except OSError:
            break
        threading.Thread(
            target=handle_tcp_client,
            args=(client, addr, target_host, target_port),
            daemon=True,
        ).start()


# --------------------------------------------------------------------------- UDP
def udp_server(listen_host: str, listen_port: int, target_host: str, target_port: int) -> None:
    """單一 socket 的 UDP 轉發：
    客戶端 -> (listen) -> 伺服器，伺服器回包也回到同一個 socket -> 客戶端。
    這樣不需要為每個客戶端額外開 ephemeral socket，穩定可靠。"""
    srv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((listen_host, listen_port))
    emit(f"[{now()}] [UDP] 監聽 {listen_host}:{listen_port} -> {target_host}:{target_port}")

    target = (target_host, target_port)
    last_client = None

    while True:
        try:
            data, addr = srv.recvfrom(65536)
        except OSError:
            break

        # 判斷來源：來自 target 的視為伺服器回包，其餘視為客戶端
        from_server = (addr[0] == target_host and addr[1] == target_port) or addr == target
        if from_server:
            log_chunk("[UDP S->C]", f"{target_host}:{target_port}", data)
            if last_client is not None:
                try:
                    srv.sendto(data, last_client)
                except OSError:
                    pass
        else:
            last_client = addr
            log_chunk("[UDP C->S]", f"{addr[0]}:{addr[1]}", data)
            try:
                srv.sendto(data, target)
            except OSError:
                pass


def main() -> int:
    global LOG_FILE
    ap = argparse.ArgumentParser(description="CSO 封包記錄代理")
    ap.add_argument("--listen-host", default="0.0.0.0")
    ap.add_argument("--listen-port", type=int, default=30002)
    ap.add_argument("--target-host", default="127.0.0.1")
    ap.add_argument("--target-port", type=int, default=30003)
    ap.add_argument("--log", default="packet_proxy.log")
    ap.add_argument("--no-udp", action="store_true", help="只代理 TCP")
    args = ap.parse_args()

    if args.log:
        LOG_FILE = open(args.log, "a", encoding="utf-8")
        emit(f"[{now()}] 日誌檔案: {args.log}")

    threads = [
        threading.Thread(
            target=tcp_server,
            args=(args.listen_host, args.listen_port, args.target_host, args.target_port),
            daemon=True,
        )
    ]
    if not args.no_udp:
        threads.append(
            threading.Thread(
                target=udp_server,
                args=(args.listen_host, args.listen_port, args.target_host, args.target_port),
                daemon=True,
            )
        )

    for t in threads:
        t.start()

    emit(f"[{now()}] 就緒。請把官方服務端 port 設為 {args.target_port}，客戶端照舊連 {args.listen_port}。Ctrl+C 結束。")
    try:
        for t in threads:
            t.join()
    except KeyboardInterrupt:
        emit(f"[{now()}] 結束。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
