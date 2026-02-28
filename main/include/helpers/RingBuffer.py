BUFFER_SIZE = 25
BUFFER_DATA_MAX_SIZE = 256

# Enum to represent different message types
class DataType:
    UNKNOWN_COMMAND = 'X'
    ACK_COMMAND = 'A'
    NACK_COMMAND = 'N'
    LED_COMMAND = 'L'
    MOVEMENT_COORDINATES = 'M'
    SOUND_COMMAND = 'S'

# Define the Message class to handle both Sent and Received messages
class Message:
    def __init__(self, timestamp=0, acknowledged=False, processed_timestamp=0, data_type=DataType.UNKNOWN_COMMAND, data=""):
        self.timestamp = timestamp  # Message timestamp (ID)
        self.acknowledged = acknowledged  # Flag to track if the message has been acknowledged
        self.processed_timestamp = processed_timestamp  # Time when the message was sent (for retry logic)
        self.data_type = data_type  # Type of data (LED_COMMAND, MOVEMENT_COORDINATES, etc.)
        self.data = data[:BUFFER_DATA_MAX_SIZE]  # Data being transmitted, truncated to max size

# Define the RingBuffer class
class RingBuffer:
    def __init__(self):
        self.buffer = [None] * BUFFER_SIZE  # Array to store messages
        self.head = 0  # Points to the next position to insert a new message
        self.tail = 0  # Points to the oldest message
        self.size = 0  # Current number of elements in the buffer
        self.message_map = {}  # Dictionary for fast lookup

    # Function to reset the ring buffer
    def reset(self):
        self.head = 0
        self.tail = 0
        self.size = 0
        self.message_map.clear()
        self.buffer = [None] * BUFFER_SIZE

    # Function to add a new message to the buffer
    def add(self, message):
        if self.size == BUFFER_SIZE:
            # Buffer is full, overwrite the oldest message
            oldest_message = self.buffer[self.tail]
            if oldest_message:
                self.message_map.pop(oldest_message.timestamp, None)
            self.tail = (self.tail + 1) % BUFFER_SIZE
        else:
            self.size += 1

        self.buffer[self.head] = message
        self.message_map[message.timestamp] = self.head
        self.head = (self.head + 1) % BUFFER_SIZE

    # Function to check if the message ID already exists in the buffer
    def contains(self, timestamp):
        return timestamp in self.message_map

    # Function to get the Message by timestamp
    def get(self, timestamp):
        index = self.message_map.get(timestamp)
        if index is not None:
            return self.buffer[index]
        return None
