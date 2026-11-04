#ifndef PRESET_MANAGER_H
#define PRESET_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#define PRESET_COUNT 28

void preset_manager_init(void);
uint8_t preset_manager_current(void);
uint8_t preset_manager_select(int delta);
void preset_manager_load(uint8_t index);
const char *preset_manager_name(uint8_t index);

#endif
