#pragma once
#include <Arduino.h>

// Bambu Lab printer status over the printer's local MQTT broker (TLS :8883, user "bblp",
// password = the printer's access code). Read-only: works in cloud mode too.
// Publishes bambu.* keys into the datastore:
//   bambu.connected  bambu.state (RUNNING/PAUSE/FINISH/...)  bambu.state_text  bambu.job
//   bambu.percent  bambu.layer  bambu.total_layers  bambu.remaining_min  bambu.remaining_text
//   bambu.eta (epoch)  bambu.nozzle  bambu.nozzle_target  bambu.bed  bambu.bed_target
//   bambu.error  bambu.updated
void bambuStart();
void bambuApplySettings();   // main loop: snapshot settings (reconnects if they changed)
