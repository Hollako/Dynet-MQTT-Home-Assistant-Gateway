#pragma once
#include "Globals.h"
#include "EntityManager.h"

// config.json I/O
bool loadConfig();
bool saveConfig();

// entities.json I/O (areas/channels + types)
bool loadEntities();      // loads EntityManager channel types (and discovers present)
bool saveEntities();      // mark dirty — actual write is deferred up to SAVE_DEBOUNCE_MS
bool saveEntitiesNow();   // write immediately (bypasses debounce — use after import/restore)
void serviceEntitiesSave(); // call every loop() — fires the deferred write when due

// JSON extraction helper used by backup/restore
bool extractTopObject(const String& src, const char* key, String& out);
