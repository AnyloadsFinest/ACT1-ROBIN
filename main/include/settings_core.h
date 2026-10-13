#pragma once

#include "cJSON.h"

/** Merge JSON settings object into live params + NVS. Returns 0 on success. */
int Settings_ApplyJsonObject(cJSON *settings);
