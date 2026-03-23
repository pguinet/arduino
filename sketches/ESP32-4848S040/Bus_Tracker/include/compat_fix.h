// Compatibility fix for Arduino_GFX v1.5.6 on ESP-IDF 5.1
// ESP_INTR_CPU_AFFINITY_AUTO was added in ESP-IDF 5.2
#ifndef ESP_INTR_CPU_AFFINITY_AUTO
#include "intr_types.h"
#define ESP_INTR_CPU_AFFINITY_AUTO ((intr_cpu_id_t)0)
#endif
