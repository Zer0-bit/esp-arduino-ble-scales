#include "timemore_new.h"
#include "remote_scales_plugin_registry.h"

// Use full 128-bit UUIDs to ensure maximum compatibility
const NimBLEUUID TIMEMORE_NEW_SERVICE_UUID("0000fff0-0000-1000-8000-00805f9b34fb");
const NimBLEUUID TIMEMORE_NEW_NOTIFY_CHAR_UUID("0000fff1-0000-1000-8000-00805f9b34fb");
const NimBLEUUID TIMEMORE_NEW_COMMAND_CHAR_UUID("0000fff2-0000-1000-8000-00805f9b34fb");

// -----------------------------------------------------------------------------------
// ---------------------------------   PUBLIC   --------------------------------------
// -----------------------------------------------------------------------------------

TimemoreNewScales::TimemoreNewScales(const DiscoveredDevice& device) : RemoteScales(device) {}

bool TimemoreNewScales::connect() {
    if (RemoteScales::clientIsConnected()) {
        return true;
    }

    RemoteScales::log("Connecting to %s [%s]\n", RemoteScales::getDeviceName().c_str(), RemoteScales::getDeviceAddress().c_str());

    // [Fix] Dynamically enable secure bonding parameters to fulfill Timemore Dot requirements
    NimBLEDevice::setSecurityAuth(true, false, true);
    // This perfectly simulates the 'createBond()' behavior found in the official Android app
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    // First connection attempt
    bool result = RemoteScales::clientConnect();
    
    if (!result) {
        // [Smart Fault Tolerance]: Connection failure is highly likely due to the scale being reset, causing the old pairing key (Bond) to become invalid
        RemoteScales::log("Connection failed. Attempting to delete old bond and retry...\n");
        
        // Accurately delete the local pairing record for this specific MAC address
        NimBLEDevice::deleteBond(getDevice().getAddress());
        
        // Buffer slightly and make a second attempt; secure pairing will be re-initiated at this time
        delay(500); 
        result = RemoteScales::clientConnect();
    }

    if (!result) {
        RemoteScales::log("Final connection attempt failed.\n");
        RemoteScales::clientCleanup();
        return false;
    }

    // Execute protocol handshake
    if (!performConnectionHandshake()) {
        RemoteScales::log("Handshake failed. Cleaning up.\n");
        RemoteScales::clientCleanup();
        return false;
    }

    //subscribeToNotifications();
    RemoteScales::setWeight(0.f);
    return true;
}

void TimemoreNewScales::disconnect() {
    RemoteScales::clientCleanup();
}

bool TimemoreNewScales::isConnected() {
    return RemoteScales::clientIsConnected();
}

void TimemoreNewScales::update() {
    // Status maintenance logic: If disconnection is detected in this cycle, trigger reconnection
    if (!isConnected()) {
        
        // [Fix] State machine: Mark for reconnection and reset timer to trigger immediately
        if (!markedForReconnection) {
            markedForReconnection = true;
            lastReconnectAttempt = millis() - 5000; 
        }
        
        // Smart throttling: After disconnection, attempt to reconnect only once every 5 seconds, rather than busy-waiting every millisecond
        if (millis() - lastReconnectAttempt > 5000) {
            RemoteScales::log("Device disconnected. Attempting to reconnect...\n");
            
            if (connect()) {
                RemoteScales::log("Reconnected to Timemore Dot successfully.\n");
                markedForReconnection = false; // Clear the disconnection flag upon success
            } else {
                RemoteScales::log("Reconnect failed. Will retry in 5 seconds.\n");
            }
            
            // Update the time of the last attempt
            lastReconnectAttempt = millis();
        }
     } else {
        // Ensure the flag is cleared when normally connected
        markedForReconnection = false;  
    }
}

bool TimemoreNewScales::tare() {
    if (!isConnected() || commandCharacteristic == nullptr) {
        RemoteScales::log("Cannot tare, not connected or no command characteristic.\n");
        return false;
    }

    // Build tare packet according to official specifications: A5 5A 03 0D 00 00 + CRC
    uint8_t packet[8] = { 
        0xA5, 0x5A, 
        0x03, // CTRL_CMD
        0x0D, // TARE_ACTION
        0x00, 0x00, 0x00, 0x00 
    };
    
    uint16_t crc = calculateCRC16(packet, 6);
    packet[6] = crc & 0xFF;         // Store CRC low byte in little-endian format
    packet[7] = (crc >> 8) & 0xFF;  // Store CRC high byte in little-endian format

    RemoteScales::log("Sending tare command...\n");
    commandCharacteristic->writeValue(packet, sizeof(packet), false);
    return true;
}

// -----------------------------------------------------------------------------------
// ---------------------------------  PRIVATE  ---------------------------------------
// -----------------------------------------------------------------------------------

bool TimemoreNewScales::performConnectionHandshake() {
    RemoteScales::log("Performing handshake...\n");

    service = RemoteScales::clientGetService(TIMEMORE_NEW_SERVICE_UUID);
    if (service == nullptr) {
        RemoteScales::log("Failed to get Timemore Dot service.\n");
        RemoteScales::clientCleanup();
        return false;
    }

    notifyCharacteristic = service->getCharacteristic(TIMEMORE_NEW_NOTIFY_CHAR_UUID);
    commandCharacteristic = service->getCharacteristic(TIMEMORE_NEW_COMMAND_CHAR_UUID);
    
    if (notifyCharacteristic == nullptr || commandCharacteristic == nullptr) {
        RemoteScales::log("Failed to get notify or command characteristics.\n");
        RemoteScales::clientCleanup();
        return false;
    }

    // Enable the data notification descriptor (0x2902) on the device
    NimBLERemoteDescriptor* notifyDesc = notifyCharacteristic->getDescriptor(NimBLEUUID((uint16_t)0x2902));
    if (notifyDesc != nullptr) {
        uint8_t value[2] = { 0x01, 0x00 };
        notifyDesc->writeValue(value, 2, true);
    } else {
        RemoteScales::log("Failed to find notification descriptor.\n");
        return false;
    }
    // [Fix] Subscribe to notifications BEFORE sending initialization queries
    // This hooks up the listener to properly ingest responsive handshake data frames
    subscribeToNotifications();

    // Send the 6 initialization query commands used by the official App after successful connection to wake up the data stream
    uint8_t initQueries[] = { 19, 8, 5, 2, 6, 12 };
    for (int i = 0; i < 6; i++) {
        sendQueryCommand(initQueries[i]);
        delay(160); // Strictly follow the packet sending interval specified by official guidelines
    }
    
    RemoteScales::log("Handshake completed successfully.\n");
    return true;
}

void TimemoreNewScales::sendQueryCommand(uint8_t queryType) {
    if (commandCharacteristic == nullptr) return;
    
    // Query frame format: A5 5A 02 [type] 00 00 00 00
    uint8_t payload[8] = { 
        0xA5, 0x5A, 
        0x02, // QUERY_CMD
        queryType, 
        0x00, 0x00, 0x00, 0x00 
    };
    commandCharacteristic->writeValue(payload, sizeof(payload), true);
}

void TimemoreNewScales::notifyCallback(NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify) {
    // Push the byte data received from the lower layer into the sliding buffer
    dataBuffer.insert(dataBuffer.end(), data, data + length);
    
    // Parse in a loop until there are no complete valid frames in the buffer (preventing packet sticking)
    while (decodeAndHandleNotification());
}

bool TimemoreNewScales::decodeAndHandleNotification() {
    if (dataBuffer.size() < 2) return false;

    // Look for valid frame header: A5 5A
    int headerIndex = -1;
    for (size_t i = 0; i < dataBuffer.size() - 1; ++i) {
        if (dataBuffer[i] == 0xA5 && dataBuffer[i+1] == 0x5A) {
            headerIndex = i;
            break;
        }
    }

    // No frame header found, clear garbage data and exit
    if (headerIndex == -1) {
        dataBuffer.clear(); 
        return false;
    }
    
    // Erase all invalid bytes before the frame header
    if (headerIndex > 0) {
        dataBuffer.erase(dataBuffer.begin(), dataBuffer.begin() + headerIndex); 
    }

    // At this point, dataBuffer must start with A5 5A. Check if it contains the complete length field (offset 4~5)
    if (dataBuffer.size() < 6) return false;

    // Read the length of the Data Payload (Big-Endian)
    uint16_t payloadLen = (dataBuffer[4] << 8) | dataBuffer[5];
    size_t fullLen = 6 + payloadLen + 2; // Header(2) + Type(2) + Length field(2) + Payload(N) + CRC(2)
    
    // The buffer data is not yet enough for a complete packet, continue waiting
    if (dataBuffer.size() < fullLen) return false;

    // Verify packet type (0x01 0x01 = WEIGHT_DATA)
    if (dataBuffer[2] == 0x01 && dataBuffer[3] == 0x01 && payloadLen >= 9) {
        // Read the 32-bit signed big-endian raw weight value (located at offset 6)
        int32_t rawWeight = (dataBuffer[6] << 24) | 
                            (dataBuffer[7] << 16) | 
                            (dataBuffer[8] <<  8) | 
                            dataBuffer[9];
        
        // Convert to floating-point weight and push to the Gaggiuino framework
        float weight = rawWeight / 10.0f;
        RemoteScales::setWeight(weight);
    }

    // Core logic: The current complete frame has been processed, completely erase it from the buffer
    dataBuffer.erase(dataBuffer.begin(), dataBuffer.begin() + fullLen);
    
    // Return true to indicate that the outer loop should continue checking the buffer for the next packet
    return dataBuffer.size() >= 6;
}

uint16_t TimemoreNewScales::calculateCRC16(const uint8_t* data, size_t length) {
    // Official algorithm: Initial value 0, polynomial 0xA001, LSB-first
    uint16_t crc = 0x0000; 
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if ((crc & 0x0001) != 0) {
                crc = (crc >> 1) ^ 0xA001; 
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void TimemoreNewScales::subscribeToNotifications() {
    RemoteScales::log("Subscribing to notifications...\n");

    auto callback = [this](NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify) {
        notifyCallback(characteristic, data, length, isNotify);
    };

    if (notifyCharacteristic->canNotify() || notifyCharacteristic->canIndicate()) {
        notifyCharacteristic->subscribe(true, callback);
        RemoteScales::log("Successfully subscribed to notify characteristic.\n");
    } else {
        RemoteScales::log("Notify characteristic cannot notify.\n");
    }
}
