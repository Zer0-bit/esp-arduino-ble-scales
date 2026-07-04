#include "ikape.h"
#include "remote_scales_plugin_registry.h"

// IK-Cafe A01. Confirmed from live capture:
//   service FFF0, notify FFF1, write FFF2 (write / write-no-response).
//   Streams weight on subscribe - no wake command required.
static const NimBLEUUID SERVICE_UUID("FFF0");
static const NimBLEUUID WEIGHT_CHAR_UUID("FFF1");   // notify
static const NimBLEUUID CMD_CHAR_UUID("FFF2");      // write (tare)

IkapeScales::IkapeScales(const DiscoveredDevice& device)
  : RemoteScales(device) {}

bool IkapeScales::connect() {
  if (clientIsConnected()) { log("Already connected\n"); return true; }
  log("Connecting to %s[%s]\n", getDeviceName().c_str(), getDeviceAddress().c_str());
  if (!clientConnect()) { clientCleanup(); return false; }
  if (!performConnectionHandshake()) return false;
  if (!subscribeToNotifications()) return false;

  // Force grams (unit code 05) so weight is unambiguous regardless of how the
  // user left the scale. Same A7 00 01 02 04 [code] .. 7A grammar as tare;
  // confirmed live that writing this sets the display/unit to grams. The BLE
  // value is decigrams in every unit anyway, so this is belt-and-suspenders.
  static const uint8_t setGrams[] = { 0xA7, 0x00, 0x01, 0x02, 0x04, 0x05, 0x0C, 0x7A };
  cmdCharacteristic->writeValue(const_cast<uint8_t*>(setGrams), sizeof(setGrams), false);

  setWeight(0.f);
  return true;
}

void IkapeScales::disconnect() { clientCleanup(); }
bool IkapeScales::isConnected() { return clientIsConnected(); }

void IkapeScales::update() {
  if (markedForReconnection) {
    log("Reconnecting\n");
    clientCleanup();
    if (connect()) markedForReconnection = false;
  } else {
    verifyConnected();
  }
}

bool IkapeScales::tare() {
  if (!verifyConnected()) return false;
  // Confirmed tare command (write to FFF2, zeroes the scale). Same frame the
  // scale emits on a physical tare press; verified live over BLE.
  // Format A7 00 01 [type=02] [sub=02] [param=01] [CK] 7A, CK=sum(bytes[2..5])&0xFF.
  static const uint8_t tareCmd[] = { 0xA7, 0x00, 0x01, 0x02, 0x02, 0x01, 0x06, 0x7A };
  cmdCharacteristic->writeValue(const_cast<uint8_t*>(tareCmd), sizeof(tareCmd), false);
  return true;
}

bool IkapeScales::performConnectionHandshake() {
  service = clientGetService(SERVICE_UUID);
  if (service == nullptr) { log("Service not found\n"); clientCleanup(); return false; }

  service->getCharacteristics(true);
  weightCharacteristic = service->getCharacteristic(WEIGHT_CHAR_UUID);
  cmdCharacteristic    = service->getCharacteristic(CMD_CHAR_UUID);
  if (weightCharacteristic == nullptr || cmdCharacteristic == nullptr) {
    log("Required characteristics not found\n"); clientCleanup(); return false;
  }
  weightCharacteristic->getDescriptors(true);
  cmdCharacteristic->getDescriptors(true);
  return true;
}

bool IkapeScales::subscribeToNotifications() {
  if (!weightCharacteristic->canNotify()) {
    log("Weight characteristic cannot notify\n"); clientCleanup(); return false;
  }
  auto cb = [this](NimBLERemoteCharacteristic* c, uint8_t* d, size_t len, bool n) {
    notifyCallback(c, d, len, n);
  };
  if (!weightCharacteristic->subscribe(true, cb, true)) {
    log("Failed to subscribe\n"); clientCleanup(); return false;
  }
  return true;
}

void IkapeScales::notifyCallback(NimBLERemoteCharacteristic* characteristic,
                                  uint8_t* data, size_t length, bool isNotify) {
  float w = decodeWeight(data, length);
  if (w > -100000.f) setWeight(w);
}

// 20-byte weight frame: A7 00 01 0E 13 [b5] [b6] [b7] 00 [HH LL] [md] 00*5 [s] [CK] 7A
//   weight = UNSIGNED big-endian uint16 at [9:10], tenths of a gram.
//   CK (byte 18) = sum(bytes[2..17]) & 0xFF. Header 0xA7, footer 0x7A, type[3]=0x0E.
//   bytes[6],[7],[11] are mode/status (weigh vs espresso/timer); bytes[16:17] is a
//   secondary value (flow/rate) present in espresso mode. None affect the weight.
//   Confirmed against live capture across all modes. Standard weigh-mode anchors:
//   00 00 -> 0.0 g, 04 B3 -> 120.3 g.
float IkapeScales::decodeWeight(const uint8_t* data, size_t length) {
  if (length != 20 || data[0] != 0xA7 || data[3] != 0x0E || data[19] != 0x7A)
    return -100001.f;                          // not a weight frame
  uint8_t sum = 0;
  for (size_t i = 2; i <= 17; ++i) sum += data[i];
  if (sum != data[18]) return -100001.f;       // bad checksum

  // Signed int16: the scale floors negatives at 0x0000 on the BLE stream (its
  // display shows them; FFF1 does not - verified with tare+remove in BOTH weigh
  // (md=01) and brew (md=02) modes). Signed is byte-identical to unsigned for
  // everything this scale sends (all << the 3276.7 g crossover), and defensively
  // decodes a real two's-complement negative instead of spiking to ~6553 g.
  int16_t raw = (int16_t)(((uint16_t)data[9] << 8) | data[10]);   // big-endian
  return raw / 10.0f;
}

bool IkapeScales::verifyConnected() {
  if (markedForReconnection) return false;
  if (!isConnected()) { markedForReconnection = true; return false; }
  return true;
}
