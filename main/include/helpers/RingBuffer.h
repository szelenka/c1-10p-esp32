#pragma once
#include <cstdint>
#include <unordered_map>
#include <cstring>

#define BUFFER_SIZE 25
#define BUFFER_DATA_MAX_SIZE 256
#define BUFFER_MESSAGE_MAX_SIZE 12+BUFFER_DATA_MAX_SIZE+2 // i.e. "@A|1A2B3C4D|flash=3;\0" 12 + 256 + 2

// Enum to represent different message types (e.g., LED, Coordinates, Audio, etc.)
enum DataType : char {
    UNKNOWN_COMMAND = 'X',
    ACK_COMMAND = 'A',
    NACK_COMMAND = 'N',
    LED_COMMAND = 'L',
    MOVEMENT_COORDINATES = 'M',
    SOUND_COMMAND = 'S'
};

// Define the Message struct to handle both Sent and Received messages
struct Message {
    uint32_t timestamp;      // Message timestamp (ID)
    bool acknowledged;       // Flag to track if the message has been acknowledged
    uint32_t processedTimestamp;  // Time when the message was sent (for retry logic, only for SENT messages)
    DataType dataType;       // Type of data (LED_COMMAND, MOVEMENT_COORDINATES, etc.)
    char data[BUFFER_DATA_MAX_SIZE];          // Data being transmitted (e.g., LED sequence, coordinates, etc.), with a maximum size of 256 characters
};

class RingBuffer {
public:
    RingBuffer() : head(0), tail(0), size(0) {}

    // Function to reset the ring buffer
    void reset() {
        head = 0;
        tail = 0;
        size = 0;
        messageMap.clear();
        memset(buffer, 0, sizeof(buffer));
    }

    // Function to add a new message ID to the buffer
    void add(const Message& message) {
        // If buffer is full, overwrite the oldest message
        if (size == BUFFER_SIZE) {
            // Remove the message from the hash table before overwriting
            messageMap.erase(buffer[tail].timestamp);
            tail = (tail + 1) % BUFFER_SIZE;  // Move tail to the next position
        } else {
            size++;
        }

        buffer[head] = message;
        messageMap[message.timestamp] = head;
        head = (head + 1) % BUFFER_SIZE;
    }

    // Function to check if the message ID already exists in the buffer
    bool contains(uint64_t timestamp) {
        return messageMap.find(timestamp) != messageMap.end();  // Fast O(1) lookup
    }

    // Function to get the Message by timestamp
    Message* get(uint32_t timestamp) {
        auto it = messageMap.find(timestamp);
        if (it != messageMap.end()) {
            return &buffer[it->second];  // Return pointer to the message
        }
        return nullptr;
    }
private:
    Message buffer[BUFFER_SIZE];  // Array to store message IDs (timestamps)
    int head;  // Points to the next position to insert a new message
    int tail;  // Points to the oldest message
    int size;  // Current number of elements in the buffer
    std::unordered_map<uint64_t, bool> messageMap;  // Hash table for fast lookup
};
