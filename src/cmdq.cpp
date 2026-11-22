#include "cmdq.h"

static QueueHandle_t s_q;

void cmdInit() { s_q = xQueueCreate(16, sizeof(Cmd *)); }

bool cmdPost(CmdType type, const String &payload, int arg) {
  Cmd *c = new Cmd();
  c->type = type; c->payload = payload; c->arg = arg;
  if (xQueueSend(s_q, &c, pdMS_TO_TICKS(50)) != pdTRUE) { delete c; return false; }
  return true;
}

bool cmdTake(Cmd &out) {
  Cmd *c = nullptr;
  if (xQueueReceive(s_q, &c, 0) != pdTRUE || !c) return false;
  out = *c;
  delete c;
  return true;
}
