#include "BleConnectionStatus.h"

BleConnectionStatus::BleConnectionStatus(void) {
  this->inputKeyboard = nullptr;
  this->outputKeyboard = nullptr;
  this->inputMediaKeys = nullptr;
  this->inputMouse = nullptr;
}

void BleConnectionStatus::onConnect(BLEServer* pServer)
{
  this->connected = true;
  // DO NOT force setNotifications(true) here! 
  // Let the client (macOS) subscribe to the CCCDs it wants.
  // Forcing it causes a fatal crash in ESP32 Core 3 NimBLE if we notify() on an unsubscribed channel.
}

void BleConnectionStatus::onDisconnect(BLEServer* pServer)
{
  this->connected = false;
  if(!this->inputKeyboard || !this->inputMouse || !this->inputMediaKeys) return;

  BLE2902* desc = (BLE2902*)this->inputKeyboard->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
  if(desc) desc->setNotifications(false);
  desc = (BLE2902*)this->inputMouse->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
  if(desc) desc->setNotifications(false);  
  desc = (BLE2902*)this->inputMediaKeys->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
  if(desc) desc->setNotifications(false);
}
