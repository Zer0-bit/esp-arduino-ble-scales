#pragma once

#include "remote_scales.h"
#include "remote_scales_plugin_registry.h"

// IK-Cafe A01 (and likely other "IK-Cafe ..." rebrands).
//
// NOTE: This device is currently also captured by the Precisa plugin's
// manufacturer-data heuristic (mfr[0]==0xFF && mfr[1]==0xFF && mfr.back()==firstMacByte).
// Because plugin selection is first-match-wins in registration order, this
// plugin must be registered BEFORE PrecisaScalesPlugin::apply(), otherwise
// Precisa will claim the device and parse it with the wrong protocol.

class IkapeScales : public RemoteScales {
public:
  IkapeScales(const DiscoveredDevice& device);
  virtual ~IkapeScales() = default;

  bool connect() override;
  void disconnect() override;
  bool isConnected() override;
  void update() override;
  bool tare() override;

private:
  NimBLERemoteService*        service             = nullptr;
  NimBLERemoteCharacteristic* weightCharacteristic = nullptr; // notify
  NimBLERemoteCharacteristic* cmdCharacteristic    = nullptr; // write (tare / wake)

  bool markedForReconnection = false;

  bool performConnectionHandshake();
  bool subscribeToNotifications();
  bool verifyConnected();

  void notifyCallback(NimBLERemoteCharacteristic* characteristic,
                      uint8_t* data, size_t length, bool isNotify);

  // Returns weight in grams, or a sentinel < -100000.f if the packet
  // is not a weight frame (so the caller can ignore it).
  float decodeWeight(const uint8_t* data, size_t length);
};

class IkapeScalesPlugin {
public:
  static void apply() {
    RemoteScalesPlugin plugin = RemoteScalesPlugin{
      .id = "plugin-ikape",
      .handles = [](const DiscoveredDevice& device) {
        return IkapeScalesPlugin::handles(device);
      },
      .initialise = [](const DiscoveredDevice& device)
          -> std::unique_ptr<RemoteScales> {
        return std::make_unique<IkapeScales>(device);
      },
    };
    RemoteScalesPluginRegistry::getInstance()->registerPlugin(plugin);
  }

private:
  static bool handles(const DiscoveredDevice& device) {
    const std::string& name = device.getName();
    // Prefix match so we claim it deterministically instead of relying on
    // Precisa's generic manufacturer-data heuristic. Confirm the exact
    // advertised prefix from your sniff ("IK-Cafe" per the settings screen).
    return !name.empty() && name.rfind("IK-Cafe", 0) == 0;
  }
};
