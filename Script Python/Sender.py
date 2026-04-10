import socket
import time

TARGET_IP_RASP = "10.0.0.20"
TARGET_IP_LAPTOP = "10.0.0.10"

SOURCE_IP_RASP = "10.0.0.20"
SOURCE_IP_LAPTOP = "10.0.0.10"

TARGET_PORT = 8080

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((SOURCE_IP_LAPTOP, 0))

print(f"Preparing to send data to {TARGET_IP_RASP}:{TARGET_PORT}")

message = "Hello Raspberry Pi IV, I am the king"

print(f"Sending: '{message}' (Length: {len(message)} bytes)")

# Send
number = 0

while(number < 1000):
    sock.sendto(message.encode('utf-8'), (TARGET_IP_RASP, TARGET_PORT))
    number += 1
    if number % 100 == 0:
        print(f"Sent {number}/1000 packets")
    time.sleep(0.05)

print("Completed")
