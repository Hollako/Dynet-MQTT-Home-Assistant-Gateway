#pragma once
#include "Globals.h"
#include "EntityManager.h"

// config.json I/O
bool loadConfig();
bool saveConfig();

// entities.json I/O (areas/channels + types)
bool loadEntities();   // loads EntityManager channel types (and discovers present)
bool saveEntities();

// JSON extraction helper used by backup/restore
bool extractTopObject(const String& src, const char* key, String& out);
