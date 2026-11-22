#pragma once
#include <Arduino.h>

void webStart();
void webApplySettings();        // main loop: snapshot settings + credentials for the server task
void webSnapshotLayout();       // main loop: snapshot the saved layout
void webTick();                 // main loop: publishes the status snapshot
