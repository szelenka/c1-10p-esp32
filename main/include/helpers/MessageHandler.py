import time
from collections import deque

class MessageHandler:
    BUFFER_MESSAGE_MAX_SIZE = 256
    BUFFER_DATA_MAX_SIZE = 128

    def __init__(self, stream):
        self.stream = stream
        self.sent_message_buffer = deque(maxlen=50)  # Simulate a ring buffer
        self.received_message_buffer = set()
        self.unacknowledged_messages = set()
        self.callback = None
        self.input_buffer = bytearray(self.BUFFER_MESSAGE_MAX_SIZE)
        self.input_index = 0

    def set_callback(self, callback):
        self.callback = callback

    def send_message(self, data_type, data):
        timestamp = int(time.time() * 1000)  # Simulate FPGA timestamp in milliseconds
        new_message = {
            "timestamp": timestamp,
            "acknowledged": False,
            "processed_timestamp": timestamp,
            "data_type": data_type,
            "data": data
        }
        self.sent_message_buffer.append(new_message)
        self.unacknowledged_messages.add(timestamp)

        message = f"@{chr(data_type)}|{timestamp:X}|{data};"
        self.stream.write(message + "\n")

    def check_incoming(self):
        while self.stream.any():
            c = self.stream.read(1).decode()

            if c == '@':
                self.input_index = 0
                self.input_buffer[self.input_index] = ord(c)
                self.input_index += 1
            elif c == ';':
                if self.input_index < self.BUFFER_MESSAGE_MAX_SIZE - 1:
                    self.input_buffer[self.input_index] = ord(c)
                    self.input_index += 1
                    self.input_buffer[self.input_index] = 0  # Null-terminate
                    self.receive_message(self.input_buffer[:self.input_index].decode())
                self.input_index = 0
            else:
                if self.input_index < self.BUFFER_MESSAGE_MAX_SIZE - 1:
                    self.input_buffer[self.input_index] = ord(c)
                    self.input_index += 1
                else:
                    self.input_index = 0
                    self.stream.write("Error: Input buffer overflow\n")

    def handle_ack(self, timestamp):
        self.mark_message_acknowledged(timestamp)

    def mark_message_acknowledged(self, timestamp):
        for msg in self.sent_message_buffer:
            if msg["timestamp"] == timestamp:
                msg["acknowledged"] = True
                self.unacknowledged_messages.discard(timestamp)
                break

    def send_ack(self, timestamp):
        ack_message = f"@A|{timestamp:X};"
        self.stream.write(ack_message + "\n")

    def send_nack(self, timestamp, reason="Unknown"):
        nack_message = f"@N|{timestamp:X}|{reason};"
        self.stream.write(nack_message + "\n")

    def handle_nack(self, timestamp):
        for msg in self.sent_message_buffer:
            if msg["timestamp"] == timestamp and not msg["acknowledged"]:
                self.resend_message(msg)
                msg["processed_timestamp"] = int(time.time() * 1000)

    def retry_messages(self, current_time, timeout):
        for timestamp in list(self.unacknowledged_messages):
            for msg in self.sent_message_buffer:
                if msg["timestamp"] == timestamp and not msg["acknowledged"]:
                    if current_time - msg["processed_timestamp"] >= timeout:
                        self.resend_message(msg)
                        msg["processed_timestamp"] = current_time

    def resend_message(self, message):
        msg = f"@{chr(message['data_type'])}|{message['timestamp']:X}|{message['data']};"
        self.stream.write(msg + "\n")

    def receive_message(self, raw_message):
        if raw_message[0] != '@':
            self.stream.write("Invalid message: missing '@'\n")
            return

        parts = raw_message[1:].split('|')
        if len(parts) < 2:
            self.send_nack(0)
            return

        type_str = parts[0]
        timestamp_str = parts[1]
        data_str = parts[2] if len(parts) > 2 else ""

        try:
            timestamp = int(timestamp_str, 16)
        except ValueError:
            self.send_nack(0)
            return

        if timestamp in self.received_message_buffer:
            self.send_ack(timestamp)
        else:
            new_message = {
                "timestamp": timestamp,
                "acknowledged": True,
                "processed_timestamp": int(time.time() * 1000),
                "data_type": ord(type_str[0]),
                "data": data_str[:self.BUFFER_DATA_MAX_SIZE - 1]
            }
            self.received_message_buffer.add(timestamp)
            self.send_ack(timestamp)
            self.process_message(new_message)

    def process_message(self, message):
        if message["data_type"] == ord('A'):  # ACK_COMMAND
            self.handle_ack(message["timestamp"])
        elif message["data_type"] == ord('N'):  # NACK_COMMAND
            self.handle_nack(message["timestamp"])
        else:
            if self.callback:
                self.callback(message["data_type"], message["data"])
