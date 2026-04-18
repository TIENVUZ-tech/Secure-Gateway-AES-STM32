import socket
import time

TARGET_IP_RASP = "10.0.0.20"
TARGET_IP_LAPTOP = "10.0.0.10"

SOURCE_IP_RASP = "10.0.0.20"
SOURCE_IP_LAPTOP = "10.0.0.10"

PACKET_COUNT = 100

TARGET_PORT = 8080

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((SOURCE_IP_LAPTOP, 0))

print(f"Preparing to send data to {TARGET_IP_RASP}:{TARGET_PORT}")

payload = b"".join(f"Block_{i:02d}_Data___".encode('ascii') for i in range(32))
assert len(payload) == 512, "Payload is not 512 bytes"

print(f"Sending: '{payload}' (Length: {len(payload)} bytes)")

# Send
start_time = time.time()
sent_count = 0

try:
    for i in range(PACKET_COUNT):
        sock.sendto(payload, (TARGET_IP_RASP, TARGET_PORT))
        sent_count += 1
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

