#pragma once

#include "pxt.h"
#include "esp_log.h"

namespace pxt {
// nothing here yet
} // namespace pxt

namespace _wifi {
    enum class WifiEvent {
        //%
        ScanDone = 1,
        //%
        GotIP = 2,
        //%
        Disconnected = 3,
        //%
        AccessPointCredentialsAvailable = 4,
        //%
        LoginServerStarted = 5,
        //%
        APStarted = 6,
        //%
        APStopped = 7,
        //%
        APStationConnected = 8,
        //%
        APStationDisconnected = 9,
        //%
        DashboardUpdated = 10,
    };

    int eventID();
    void startHttpServer(const char* hostName);
    int startAPInternal(const char* ssid, const char* password, int channel, int maxConnections);
}