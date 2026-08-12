#pragma once

/** Show the RGB565-optimized BrewSphere startup emblem. */
void statusScreenBrand();
/** Show the RGB565-optimized two-line BrewSphere wordmark. */
void statusScreenBrandWordmark();

void statusScreenPortal();
void statusScreenConnectFailed();
void statusScreenWifiReset();
void statusScreenFirmwareUpdate();

/** Saved-network connect animation (call Tick until connect finishes). */
void statusScreenConnectingBegin(const char* ssid);
void statusScreenConnectingTick();
