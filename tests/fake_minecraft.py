#!/usr/bin/env python3
"""Independent Python peer for the production x86 ASI's localhost transport.

The geometry below is a diagnostic triangle, not an implementation of Minecraft.
No Minecraft installation, accounts, saves, or game process are opened.
"""
from __future__ import annotations

import argparse
import math
import pathlib
import socket
import struct
import subprocess
import threading
import time
import unittest

MAGIC = 0x4234434D
VERSION = 6
MAX_PAYLOAD = 32 * 1024 * 1024
HEADER = struct.Struct("<IHHII")
STATE = struct.Struct("<ddd8fIIQII")
INPUT = struct.Struct("<IIIffIiIdddIIffII")
VERTEX = struct.Struct("<8fI")


def packet(kind: int, payload: bytes, sequence: int = 1) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    return HEADER.pack(MAGIC, VERSION, kind, len(payload), sequence) + payload


class PacketReader:
    def __init__(self) -> None:
        self.buffer = bytearray()

    def feed(self, incoming: bytes) -> list[tuple[int, int, bytes]]:
        self.buffer.extend(incoming)
        packets: list[tuple[int, int, bytes]] = []
        while len(self.buffer) >= HEADER.size:
            magic, version, kind, size, sequence = HEADER.unpack_from(self.buffer)
            if magic != MAGIC or version != VERSION or size > MAX_PAYLOAD:
                raise ValueError("malformed packet header")
            if len(self.buffer) < HEADER.size + size:
                break
            payload = bytes(self.buffer[HEADER.size:HEADER.size + size])
            del self.buffer[:HEADER.size + size]
            packets.append((kind, sequence, payload))
        return packets


def validate_input(payload: bytes) -> tuple:
    if len(payload) != INPUT.size:
        raise ValueError("Input must have exactly 80 bytes")
    values = INPUT.unpack(payload)
    flags, held, pressed, yaw, pitch, selected_slot, scroll, epoch, x, y, z, width, height, cx, cy, buttons, reserved = values
    if flags & ~63 or held & ~511 or pressed & ~127 or flags & 20 == 20:
        raise ValueError("unknown Input action bits")
    if flags & 32 and (not flags & 4 or flags & 16):
        raise ValueError("native traversal requires exclusive host authority")
    if selected_slot not in range(9) and selected_slot != 0xFFFFFFFF:
        raise ValueError("invalid Input selected slot")
    if not all(math.isfinite(value) for value in (yaw, pitch, x, y, z, cx, cy)):
        raise ValueError("non-finite Input pose")
    return values


def fixture_packets() -> list[bytes]:
    state = STATE.pack(12.5, 64.0, -8.0, 30.0, -10.0, 20.0, 20.0, 20.0, 5.0, 1.0, 0.5, 5, 2, 12345, 1, 0)
    texture = struct.pack("<III", 7, 2, 2) + bytes((255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255))
    vertices = (
        VERTEX.pack(-0.5, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0xFFFFFFFF)
        + VERTEX.pack(0.5, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0xFFFFFFFF)
        + VERTEX.pack(0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.5, 0.0, 0xFFFFFFFF)
    )
    mesh = struct.pack("<IIIIddd", 1, 42, 1, 0, 0, 0, 0) + struct.pack("<III", 7, 0, 3) + vertices
    return [packet(1, state, 1), packet(2, texture, 2), packet(3, mesh, 3)]


def send_fragmented(connection: socket.socket, data: bytes) -> None:
    # Force headers and vertex fields to cross read boundaries.
    sizes = (1, 2, 7, 3, 13, 5, 29)
    offset = 0
    index = 0
    while offset < len(data):
        end = min(len(data), offset + sizes[index % len(sizes)])
        connection.sendall(data[offset:end])
        offset = end
        index += 1
        time.sleep(0.001)


def run_native(harness: pathlib.Path, plugin: pathlib.Path) -> None:
    errors: list[Exception] = []
    received: list[tuple] = []
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(12)
    port = listener.getsockname()[1]

    def server() -> None:
        try:
            with listener:
                connection, address = listener.accept()
                if address[0] != "127.0.0.1":
                    raise AssertionError("peer did not connect over loopback")
                with connection:
                    connection.settimeout(10)
                    for encoded in fixture_packets():
                        send_fragmented(connection, encoded)
                    reader = PacketReader()
                    while True:
                        incoming = connection.recv(4096)
                        if not incoming:
                            if not received:
                                raise AssertionError("native peer closed without returning Input")
                            break
                        for kind, sequence, payload in reader.feed(incoming):
                            if kind != 16:
                                raise AssertionError(f"unexpected native message {kind}")
                            received.append(validate_input(payload))
        except Exception as error:
            errors.append(error)

    thread = threading.Thread(target=server, name="fake-minecraft", daemon=True)
    thread.start()
    result = subprocess.run([str(harness.resolve()), str(plugin.resolve()), "--transport", str(port)], capture_output=True, text=True, timeout=15)
    thread.join(timeout=12)
    print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="")
    if thread.is_alive():
        listener.close()
        raise AssertionError("fake Minecraft peer did not finish")
    if errors:
        raise AssertionError(f"fake Minecraft peer failed: {errors[0]}") from errors[0]
    if result.returncode:
        raise AssertionError(f"native harness returned {result.returncode}")
    if not received:
        raise AssertionError("expected at least one valid native Input")
    print("PASS: production x86 ASI accepted fragmented State/Texture/Mesh and returned a valid Input over localhost.")


def serve(port: int) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", port))
        listener.listen(1)
        print(f"Diagnostic triangle peer listening on 127.0.0.1:{port}; Ctrl+C stops.")
        while True:
            connection, _ = listener.accept()
            print("Native peer connected.")
            try:
                with connection:
                    connection.settimeout(0.05)
                    for encoded in fixture_packets():
                        send_fragmented(connection, encoded)
                    reader = PacketReader()
                    while True:
                        try:
                            incoming = connection.recv(4096)
                        except socket.timeout:
                            continue
                        if not incoming:
                            break
                        for kind, sequence, payload in reader.feed(incoming):
                            if kind == 16:
                                validate_input(payload)
                            elif kind == 17:
                                break
            except (ConnectionError, ValueError) as error:
                print(f"Peer disconnected: {error}")


class ProtocolTests(unittest.TestCase):
    def test_wire_sizes(self) -> None:
        self.assertEqual((HEADER.size, STATE.size, INPUT.size, VERTEX.size), (16, 80, 80, 36))

    def test_every_header_fragment_boundary(self) -> None:
        data = packet(16, INPUT.pack(3, 1, 2, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, 0, 640, 480, .5, .5, 0, 0), 123)
        for split in range(1, len(data)):
            reader = PacketReader()
            self.assertEqual(reader.feed(data[:split]), [])
            decoded = reader.feed(data[split:])
            self.assertEqual(decoded, [(16, 123, data[16:])])

    def test_many_packets_in_one_read(self) -> None:
        fixtures = fixture_packets()
        self.assertEqual([kind for kind, _, _ in PacketReader().feed(b"".join(fixtures))], [1, 2, 3])

    def test_invalid_headers_fail_before_allocation(self) -> None:
        for magic, version, size in ((0, VERSION, 0), (MAGIC, 1, 0), (MAGIC, VERSION, MAX_PAYLOAD + 1)):
            with self.assertRaises(ValueError):
                PacketReader().feed(HEADER.pack(magic, version, 16, size, 0))

    def test_invalid_native_input(self) -> None:
        for field, value in ((0, 32), (0,23), (1, 512), (2, 128), (3, float("nan")), (4, float("inf")), (5, 9), (8, float("nan"))):
            values = [3, 0, 0, 0.0, 0.0, 0xFFFFFFFF, 0, 0, 0.0, 0.0, 0.0, 640, 480, .5, .5, 0, 0]
            values[field] = value
            with self.assertRaises(ValueError):
                validate_input(INPUT.pack(*values))
        with self.assertRaises(ValueError):
            validate_input(b"short")

    def test_native_traversal_authority(self) -> None:
        values = [3 | 4 | 32, 0, 0, 0.0, 0.0, 0xFFFFFFFF, 0, 7, 1.0, -3.0, 2.0, 640, 480, .5, .5, 0, 0]
        self.assertEqual(validate_input(INPUT.pack(*values))[0], 39)
        for flags in (3 | 32, 3 | 16 | 32, 3 | 4 | 16 | 32, 3 | 64):
            values[0] = flags
            with self.assertRaises(ValueError):
                validate_input(INPUT.pack(*values))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", nargs=2, metavar=("HARNESS", "ASI"))
    parser.add_argument("--serve", type=int, metavar="PORT")
    arguments = parser.parse_args()
    if arguments.serve is not None:
        serve(arguments.serve)
        return
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ProtocolTests))
    if not result.wasSuccessful():
        raise SystemExit(1)
    if arguments.native:
        run_native(pathlib.Path(arguments.native[0]), pathlib.Path(arguments.native[1]))


if __name__ == "__main__":
    main()
