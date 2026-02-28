#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <unordered_set>
#include <string>

#include "include/chopper/Timer.h"
#include "include/helpers/RingBuffer.h"


// MessageHandler class that uses the RingBuffer for message management
class MessageHandler {
public:
    using MessageCallback = std::function<void(const DataType&, const char*)>;

    // Constructor
    MessageHandler(Stream& streamRef, uint64_t timeout = 1000) : stream(streamRef), timeout(timeout), callback(nullptr) {}

    // Set the callback function for processing messages
    void setCallback(MessageCallback cb) {
        callback = cb;
    }

    void sendMessage(DataType dataType, const String& data) {
        uint32_t timestamp = Timer::GetFPGATimestamp();
        Message newMessage = {};
        newMessage.timestamp = timestamp;
        newMessage.acknowledged = false;
        newMessage.processedTimestamp = timestamp;
        newMessage.dataType = dataType;
        strncpy(newMessage.data, data.c_str(), BUFFER_DATA_MAX_SIZE - 1);
        newMessage.data[BUFFER_DATA_MAX_SIZE - 1] = '\0';  // Ensure null-termination
        sentMessageBuffer.add(newMessage);
        unacknowledgedMessages.insert(timestamp);

        String message = "@" + String(static_cast<char>(dataType)) + "|" + String(timestamp, HEX) + "|" + data + ";";
        stream.println(message);
    }

    void processMessages() {
        checkIncoming();
        retryMessages();
    }

private:
    Stream& stream;                   // Reference to the stream for communication
    RingBuffer sentMessageBuffer;     // Ring buffer for storing SENT messages
    RingBuffer receivedMessageBuffer; // Ring buffer for storing RECEIVED messages
    std::unordered_set<uint32_t> unacknowledgedMessages;  // Set to track unacknowledged SENT messages
    uint64_t timeout;               // Timeout for message acknowledgment
    MessageCallback callback = nullptr;         // Callback function for processing messages
    char inputBuffer[BUFFER_MESSAGE_MAX_SIZE];
    size_t inputIndex = 0;

    void handleAck(uint32_t timestamp) {
        Message* msg = sentMessageBuffer.get(timestamp);
        if (msg) {
            msg->acknowledged = true;
            unacknowledgedMessages.erase(timestamp);
        }
    }

    void sendAck(uint32_t timestamp) {
        String ackMessage = "@" + String(static_cast<char>(DataType::ACK_COMMAND)) + "|" + String(timestamp, HEX) + ";";
        stream.println(ackMessage);
    }
    void sendNack(uint32_t timestamp, const char* reason = "Unknown") {
        String nackMessage = "@" + String(static_cast<char>(DataType::NACK_COMMAND)) + "|" + String(timestamp, HEX) + "|" + String(reason) + ";";
        stream.println(nackMessage);
    }

    void handleNack(uint32_t timestamp) {
        // For now, treat NACK like a retriable failure
        Message* msg = sentMessageBuffer.get(timestamp);
        if (msg && !msg->acknowledged) {
            resendMessage(*msg);
            msg->processedTimestamp = Timer::GetFPGATimestamp();
        }
    }

    void retryMessages() {
        uint64_t currentTime = Timer::GetFPGATimestamp();
        // Retry unacknowledged messages from the sent buffer
        for (const auto& timestamp : unacknowledgedMessages) {
            Message* msg = sentMessageBuffer.get(timestamp);
            if (msg && !msg->acknowledged && (currentTime - msg->processedTimestamp >= timeout)) {
                // Retry sending the message
                resendMessage(*msg);
                msg->processedTimestamp = currentTime;  // Update sent time after retry
            }
        }
    }

    void resendMessage(Message& message) {
        String msg = "@" + String(static_cast<char>(message.dataType)) + "|" + String(message.timestamp, HEX) + "|" + message.data + ";";
        stream.println(msg);
    }

    void checkIncoming() {
        while (stream.available()) {
            char c = stream.read();

            if (c == '@') {
                // Start of new message
                inputIndex = 0;
                inputBuffer[inputIndex++] = c;
            } else if (c == ';') {
                // End of message
                if (inputIndex < BUFFER_MESSAGE_MAX_SIZE - 1) {
                    inputBuffer[inputIndex++] = c;
                    inputBuffer[inputIndex] = '\0';  // Null-terminate
                    receiveMessage(inputBuffer);  // Convert to String for processing
                }
                inputIndex = 0;  // Reset for next message
            } else {
                // Middle of message
                if (inputIndex < BUFFER_MESSAGE_MAX_SIZE - 1) {
                    inputBuffer[inputIndex++] = c;
                } else {
                    // Buffer overflow: reset
                    inputIndex = 0;
                    stream.println("Error: Input buffer overflow");
                }
            }
        }
    }

    void receiveMessage(const char* rawMessage) {
        if (rawMessage[0] != '@') {
            stream.println("Invalid message: missing '@'");
            return;
        }

        // Copy to avoid modifying original buffer
        char buffer[BUFFER_MESSAGE_MAX_SIZE];
        strncpy(buffer, rawMessage + 1, BUFFER_MESSAGE_MAX_SIZE - 1);  // Skip '@'
        buffer[BUFFER_MESSAGE_MAX_SIZE - 1] = '\0'; // Ensure null termination

        char* typeStr = strtok(buffer, "|");
        char* timestampStr = strtok(nullptr, "|");
        char* dataStr = strtok(nullptr, ";");  // data is optional for ACK/NACK

        // Ensure timestampStr doesn't contain a semicolon if there's no data
        if (timestampStr) {
            char* semicolon = strchr(timestampStr, ';');
            if (semicolon) *semicolon = '\0';
        }

        if (!typeStr || !timestampStr || strlen(typeStr) != 1) {
            // Try to extract a partial timestamp to include in the NACK (if any)
            uint32_t partialTimestamp = timestampStr ? strtoul(timestampStr, nullptr, 16) : 0;
            sendNack(partialTimestamp);
            return;
        }

        uint32_t timestamp = strtoul(timestampStr, nullptr, 16);

        if (receivedMessageBuffer.contains(timestamp)) {
            // Duplicate message: already processed, just re-ACK
            sendAck(timestamp);
        } else {
            Message newMessage = {};
            newMessage.timestamp = timestamp;
            newMessage.acknowledged = true;
            newMessage.processedTimestamp = Timer::GetFPGATimestamp();
            newMessage.dataType = static_cast<DataType>(typeStr[0]);
            // Safely copy dataStr to newMessage.data
            if (dataStr) {
                strncpy(newMessage.data, dataStr, BUFFER_DATA_MAX_SIZE - 1);
                newMessage.data[BUFFER_DATA_MAX_SIZE - 1] = '\0';  // Ensure null-termination
            } else {
                newMessage.data[0] = '\0';  // Empty string if data is missing
            }
            receivedMessageBuffer.add(newMessage);
            sendAck(timestamp);
            processCallback(newMessage);
        }
    }

    void processCallback(const Message& message) {
        switch(message.dataType) {
            case ACK_COMMAND:
                handleAck(message.timestamp);
                break;
            case NACK_COMMAND:
                handleNack(message.timestamp);
                break;
            default:
                if (callback) {
                    callback(message.dataType, message.data);  // Pass the message data to the callback
                }
                break;
        }
    }
};
