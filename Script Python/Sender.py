import socket
import time

TARGET_IP_RASP = "192.168.5.10"
TARGET_IP_LAPTOP = "192.168.5.2"

SOURCE_IP_RASP = "192.168.5.10"
SOURCE_IP_LAPTOP = "192.168.5.2"

PACKET_COUNT = 1000

TARGET_PORT = 8080

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((SOURCE_IP_LAPTOP, 0))

print(f"Preparing to send data to {TARGET_IP_RASP}:{TARGET_PORT}")

base_payload = b"".join(f"Block_{i:02d}_Data___".encode('ascii') for i in range(20))

print(f"Base Payload length: {len(base_payload)} bytes")

# Send
start_time = time.time()
sent_count = 0

try:
    for i in range(PACKET_COUNT):
        seq_header = f"SEQ:{i:04d}|".encode('ascii')
        packet_data = seq_header + base_payload
        sock.sendto(packet_data, (TARGET_IP_RASP, TARGET_PORT))
        sent_count += 1
        print(f"\rSent: {seq_header.decode('ascii')} (Total: {len(packet_data)} bytes)", end="")
        time.sleep(0.02)

except KeyboardInterrupt:
    print("\n[!] Stopped test by boss.")
finally:
    elapsed = time.time() - start_time
    print("-" * 30)
    print(f"Experimental result:")
    print(f" - Number of packets sent: {sent_count}")
    print(f" - Total time: {elapsed:.2f} second")
    print(f" - Actual speed: {sent_count / elapsed:.2f} packets/second")
    sock.close()

