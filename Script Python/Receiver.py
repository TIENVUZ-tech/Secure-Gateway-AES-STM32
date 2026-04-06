import socket
from Crypto.Cipher import AES
from Crypto.Util.Padding import unpad

KEY_RASP = bytes([0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0])
KEY_LAPTOP = bytes([0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F])

IP_RASP = "192.168.1.20"
IP_LAPTOP = "192.168.1.10"

IV = bytes([0] * 16)

LISTEN_IP = IP_LAPTOP
LISTEN_PORT = 8080

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((LISTEN_IP, LISTEN_PORT))

print(f"Listening to encrypted packet from: ({LISTEN_IP}:{LISTEN_PORT})")

while True:
    encrypted_payload, addr = sock.recvfrom(600)
    print(f"Received from {len(encrypted_payload)} bytes from {addr}")
    print(f"Ciphertex (Hex): {encrypted_payload.hex()}")

    try:
        cipher = AES.new(KEY_RASP, AES.MODE_CBC, IV)

        decrypted_padded = cipher.decrypt(encrypted_payload)
        plaintex_bytes = unpad(decrypted_padded, AES.block_size)

        print(f"Plaintex: {plaintex_bytes.decode('utf-8')}")

    except ValueError as e:
        print("I can't decrypt this packet")
        print(f"Detail {e}")
