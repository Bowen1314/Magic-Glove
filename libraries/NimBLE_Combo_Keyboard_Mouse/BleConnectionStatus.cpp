#include "BleConnectionStatus.h"

BleConnectionStatus::BleConnectionStatus(void) {
  this->inputKeyboard = nullptr;
  this->outputKeyboard = nullptr;
  this->inputMediaKeys = nullptr;
  this->inputMouse = nullptr;
}

void BleConnectionStatus::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo)
{
  this->connected = true;
  // DO NOT force setNotifications(true) here! 
  // Let the client (macOS) subscribe to the CCCDs it wants.
  // Forcing it causes a fatal crash in ESP32 Core 3 NimBLE if we notify() on an unsubscribed channel.
}

void BleConnectionStatus::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason)
{
  this->connected = false;
}
