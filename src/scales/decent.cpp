#include "decent.h"
#include "remote_scales_plugin_registry.h"
#include <iostream>

using namespace std;

const NimBLEUUID serviceUUID("FFF0");
const NimBLEUUID readCharacteristicUUID("FFF4");
const NimBLEUUID writeCharacteristicUUID("36F5");

DecentScales::DecentScales(const DiscoveredDevice& device)
  : RemoteScales(device) {
}

DecentScales::~DecentScales() {}

bool DecentScales::connect() {
  if (RemoteScales::clientIsConnected()) {
    RemoteScales::log("Already connected\n");
    return true;
  }

  RemoteScales::log("Connecting to %s[%s]\n",
    RemoteScales::getDeviceName().c_str(),
    RemoteScales::getDeviceAddress().c_str());

  bool result = RemoteScales::clientConnect();
  if (!result) {
    RemoteScales::clientCleanup();
    return false;
  }

  if (!performConnectionHandshake()) {
    return false;
  }

  if (!subscribeToNotifications()) {
    return false;
  }

  RemoteScales::setWeight(0.f);
  return true;
}

void DecentScales::disconnect() { RemoteScales::clientCleanup(); }

bool DecentScales::isConnected() { return RemoteScales::clientIsConnected(); }

void DecentScales::update() {
  if (markedForReconnection) {
    RemoteScales::log("Marked for disconnection. Will attempt to reconnect.\n");
    RemoteScales::clientCleanup();
    if (connect()) {
      markedForReconnection = false;
    }
    else {
      RemoteScales::log("Failed to reconnect\n");
      return;
    }
  }
  else {
    verifyConnected();
  }
}

bool DecentScales::tare() {
  if (!verifyConnected())
    return false;
  uint8_t payload[] = { 0x03, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x0C };
  writeCharacteristic->writeValue(payload, sizeof(payload), false);
  return true;
};

bool DecentScales::performConnectionHandshake() {
  RemoteScales::log("Performing handshake\n");

  service = RemoteScales::clientGetService(serviceUUID);
  if (service != nullptr) {
  }
  else {
    clientCleanup();
    return false;
  }
  RemoteScales::log("Got Service\n");

  readCharacteristic = service->getCharacteristic(readCharacteristicUUID);
  writeCharacteristic = service->getCharacteristic(writeCharacteristicUUID);
  if (readCharacteristic == nullptr || writeCharacteristic == nullptr) {
    clientCleanup();
    return false;
  }
  RemoteScales::log("Got readCharacteristic and writeCharacteristic\n");
  return true;
}

bool DecentScales::subscribeToNotifications() {
  if (readCharacteristic->canNotify()) {
    auto callback = [this](NimBLERemoteCharacteristic* characteristic,
      uint8_t* data, size_t length, bool isNotify) {
        readCallback(characteristic, data, length, isNotify);
      };
    if (!readCharacteristic->subscribe(true, callback, false)) {
      clientCleanup();
      return false;
    }
  }
  else {
    clientCleanup();
    return false;
  }
  RemoteScales::log("Registered for notify\n");
  return true;
}

void DecentScales::readCallback(NimBLERemoteCharacteristic* pCharacteristic,
  uint8_t* pData, size_t length, bool isNotify) {
  if ((length == 7 || length == 10) && pData[0] == 0x03) {
    if(pData[1] == 0xCA || pData[1] == 0xCE){
      handleWeightNotification(pData, length);
    }
    else if(pData[1] == 0xAA){
      handleButtonPress(pData, length);
    }
  }
  else {
    RemoteScales::log("Wrong packet length\n");
  }
}

void DecentScales::handleWeightNotification(uint8_t* pData, size_t length) {
  int16_t weight100 =
    static_cast<int16_t>((uint16_t)((pData[2] << 8) | pData[3]));

  uint8_t xorByte = pData[length - 1];

  if (xorByte != 0) {
    uint8_t xorSum = pData[0];

    for (int i = 1; i < length - 1; i++) {
      xorSum ^= pData[i];
    }

    if (xorSum != xorByte) {
      RemoteScales::log("Wrong checksum\n");
      return;
    }
  }

  RemoteScales::setWeight(weight100 / 10.f);
  RemoteScales::log("Weight received\n");
}

void DecentScales::handleButtonPress(uint8_t* pData, size_t length) {
  uint8_t button = pData[2];

  if(button == 0x01){
    RemoteScales::log("Tare button pressed\n");
    RemoteScales::tare();
  }else if(button == 0x02){
    RemoteScales::log("Timer button pressed\n");
    // ignore for now 
  }else{
    RemoteScales::log("Button pressed: %d\n", button);
  }
}

bool DecentScales::verifyConnected() {
  if (markedForReconnection) {
    return false;
  }
  if (!isConnected()) {
    markedForReconnection = true;
    return false;
  }
  return true;
}
