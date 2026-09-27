#include "wilfa.h"

static const NimBLEUUID SCALE_SERVICE_UUID("FFB0");
static const NimBLEUUID EVENT_CHAR_UUID("FFB1");            // Gen 2 also sends weight and takes commands here
static const NimBLEUUID GEN1_WEIGHT_CHAR_UUID("FFB2");
static const NimBLEUUID GEN1_COMMAND_SERVICE_UUID("FEE7");  // only present on Gen 1
static const NimBLEUUID GEN1_COMMAND_CHAR_UUID("FEC7");

WilfaScales::WilfaScales(const DiscoveredDevice& device) : RemoteScales(device) {}

bool WilfaScales::connect() {
  if (clientIsConnected()) {
    log("Already connected\n");
    return true;
  }
  log("Connecting to %s[%s]\n", getDeviceName().c_str(), getDeviceAddress().c_str());
  if (!clientConnect() || !performConnectionHandshake() || !subscribeToNotifications()) {
    clientCleanup();
    return false;
  }
  setWeight(0.f);
  return true;
}

void WilfaScales::disconnect() { clientCleanup(); }
bool WilfaScales::isConnected() { return clientIsConnected(); }

void WilfaScales::update() {
  if (markedForReconnection) {
    log("Reconnecting\n");
    clientCleanup();
    if (connect()) markedForReconnection = false;
  } else {
    verifyConnected();
  }
}

bool WilfaScales::tare() {
  if (!verifyConnected()) return false;
  static const uint8_t tareCommand[] = { 0xF5, 0x10 };
  // The protocol notes don't say which write type the scale expects, so use the one the characteristic advertises.
  return commandCharacteristic->writeValue(tareCommand, sizeof(tareCommand), commandCharacteristic->canWrite());
}

bool WilfaScales::performConnectionHandshake() {
  NimBLERemoteService* scaleService = clientGetService(SCALE_SERVICE_UUID);
  if (scaleService == nullptr) {
    log("Scale service not found\n");
    return false;
  }
  eventCharacteristic = scaleService->getCharacteristic(EVENT_CHAR_UUID);

  NimBLERemoteService* gen1CommandService = clientGetService(GEN1_COMMAND_SERVICE_UUID);
  if (gen1CommandService != nullptr) {
    log("Gen 1 scale\n");
    weightCharacteristic = scaleService->getCharacteristic(GEN1_WEIGHT_CHAR_UUID);
    commandCharacteristic = gen1CommandService->getCharacteristic(GEN1_COMMAND_CHAR_UUID);
  } else {
    log("Gen 2 scale\n");
    weightCharacteristic = eventCharacteristic;
    commandCharacteristic = eventCharacteristic;
  }

  if (eventCharacteristic == nullptr || weightCharacteristic == nullptr || commandCharacteristic == nullptr) {
    log("Required characteristics not found\n");
    return false;
  }
  return true;
}

bool WilfaScales::subscribeToNotifications() {
  auto callback = [this](NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify) {
    notifyCallback(characteristic, data, length, isNotify);
  };
  if (!eventCharacteristic->subscribe(true, callback, true)) {
    log("Failed to subscribe to events\n");
    return false;
  }
  if (weightCharacteristic != eventCharacteristic && !weightCharacteristic->subscribe(true, callback, true)) {
    log("Failed to subscribe to weight\n");
    return false;
  }
  return true;
}

// Every packet starts with a two-byte header. Only weight is needed:
//   FA 01 [hi] [lo]  signed big-endian tenths of a gram, always in grams whatever the display shows
// Others: FA 03 disconnecting, FA 04 [unit], FA 05 low battery, FA 06 unstable, FA 07 overloaded.
// FA 03 is ignored on purpose: the scale drops the link right after it, and reconnect attempts made before
// the link is gone fail instantly, so update() reconnects once isConnected() turns false instead.
void WilfaScales::notifyCallback(NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify) {
  if (length >= 4 && data[0] == 0xFA && data[1] == 0x01) {
    setWeight(static_cast<int16_t>((data[2] << 8) | data[3]) / 10.f);
  }
}

bool WilfaScales::verifyConnected() {
  if (markedForReconnection) return false;
  if (!isConnected()) {
    markedForReconnection = true;
    return false;
  }
  return true;
}
