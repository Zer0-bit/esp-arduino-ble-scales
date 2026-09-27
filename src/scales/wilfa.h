#pragma once
#include "remote_scales.h"
#include "remote_scales_plugin_registry.h"

// Wilfa Svart Uni (WSS-2). Handles both hardware generations: Gen 1 runs on AA
// batteries, Gen 2 is rechargeable over USB-C.
// Protocol: https://gist.github.com/ts95/6da343963ada5da0c41a7fd11dbf4736
class WilfaScales : public RemoteScales {
public:
  WilfaScales(const DiscoveredDevice& device);

  bool connect() override;
  void disconnect() override;
  bool isConnected() override;
  void update() override;
  bool tare() override;

private:
  NimBLERemoteCharacteristic* eventCharacteristic = nullptr;   // notify
  NimBLERemoteCharacteristic* weightCharacteristic = nullptr;  // notify
  NimBLERemoteCharacteristic* commandCharacteristic = nullptr; // write

  bool markedForReconnection = false;

  bool performConnectionHandshake();
  bool subscribeToNotifications();
  bool verifyConnected();

  void notifyCallback(NimBLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify);
};

class WilfaScalesPlugin {
public:
  static void apply() {
    RemoteScalesPlugin plugin = RemoteScalesPlugin{
      .id = "plugin-wilfa",
      .handles = [](const DiscoveredDevice& device) { return WilfaScalesPlugin::handles(device); },
      .initialise = [](const DiscoveredDevice& device) -> std::unique_ptr<RemoteScales> { return std::make_unique<WilfaScales>(device); },
    };
    RemoteScalesPluginRegistry::getInstance()->registerPlugin(plugin);
  }

private:
  static bool handles(const DiscoveredDevice& device) {
    // Advertises as "Wilfa Svart scale".
    return device.getName().rfind("Wilfa", 0) == 0;
  }
};
