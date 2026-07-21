#pragma once
#include <ESPAsyncWebServer.h>

void startWebServer();
extern AsyncWebServer server;   // if blynk_vending_machine.ino needs to add routes too