#include <math.h>
#include <stdint.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"

#define TABLE_MAX_LOAD 0.75

static uint32_t hashValue(Value value) {
  switch (value.type) {
    case VAL_BOOL:
      return AS_BOOL(value) ? 1u : 0u;

    case VAL_NIL:
      return 2u;

    case VAL_NUMBER: {
      double number = AS_NUMBER(value);

      if (number == 0.0) {
        return 0;
      }

      if (isnan(number)) {
        return 0x7fc00000u;
      }

      uint64_t bits;
      memcpy(&bits, &number, sizeof(bits));

      uint32_t hash = (uint32_t)bits ^ (uint32_t)(bits >> 32);
      hash ^= hash >> 16;
      hash *= 0x7feb352du;
      hash ^= hash >> 15;
      hash *= 0x846ca68bu;
      hash ^= hash >> 16;
      return hash;
    }

    case VAL_OBJ: {
      if (!IS_STRING(value)) {
        return 0;
      }

      ObjString* string = AS_STRING(value);
      const unsigned char* chars =
          (const unsigned char*)stringChars(string);
      uint32_t hash = 2166136261u;

      for (int i = 0; i < string->length; i++) {
        hash ^= chars[i];
        hash *= 16777619u;
      }

      return hash;
    }
  }

  return 0;
}

static bool keysEqual(Value a, Value b) {
  if (IS_NUMBER(a) && IS_NUMBER(b) &&
      isnan(AS_NUMBER(a)) && isnan(AS_NUMBER(b))) {
    return true;
  }

  return valuesEqual(a, b);
}

static Entry* findEntry(
    Entry* entries, int capacity, Value key) {
  uint32_t index = hashValue(key) & (uint32_t)(capacity - 1);
  Entry* tombstone = NULL;

  for (;;) {
    Entry* entry = &entries[index];

    if (entry->state == ENTRY_EMPTY) {
      return tombstone != NULL ? tombstone : entry;
    }

    if (entry->state == ENTRY_TOMBSTONE) {
      if (tombstone == NULL) {
        tombstone = entry;
      }
    } else if (keysEqual(entry->key, key)) {
      return entry;
    }

    index = (index + 1) & (uint32_t)(capacity - 1);
  }
}

void initTable(Table* table) {
  table->count = 0;
  table->used = 0;
  table->capacity = 0;
  table->entries = NULL;
}

void freeTable(Table* table) {
  FREE_ARRAY(Entry, table->entries, table->capacity);
  initTable(table);
}

static void adjustCapacity(Table* table, int capacity) {
  Entry* entries = GROW_ARRAY(Entry, NULL, 0, capacity);

  for (int i = 0; i < capacity; i++) {
    entries[i].key = NIL_VAL;
    entries[i].value = NIL_VAL;
    entries[i].state = ENTRY_EMPTY;
  }

  table->count = 0;
  table->used = 0;

  for (int i = 0; i < table->capacity; i++) {
    Entry* oldEntry = &table->entries[i];

    if (oldEntry->state != ENTRY_OCCUPIED) {
      continue;
    }

    Entry* destination = findEntry(entries, capacity, oldEntry->key);
    *destination = *oldEntry;
    table->count++;
    table->used++;
  }

  FREE_ARRAY(Entry, table->entries, table->capacity);
  table->entries = entries;
  table->capacity = capacity;
}

bool tableGet(Table* table, Value key, Value* value) {
  if (table->count == 0) {
    return false;
  }

  Entry* entry = findEntry(table->entries, table->capacity, key);

  if (entry->state != ENTRY_OCCUPIED) {
    return false;
  }

  *value = entry->value;
  return true;
}

bool tableSet(Table* table, Value key, Value value) {
  if (table->used + 1 > table->capacity * TABLE_MAX_LOAD) {
    int capacity = table->capacity;

    if (table->count + 1 > capacity * TABLE_MAX_LOAD) {
      capacity = GROW_CAPACITY(capacity);
    }

    adjustCapacity(table, capacity);
  }

  Entry* entry = findEntry(table->entries, table->capacity, key);
  bool isNewKey = entry->state != ENTRY_OCCUPIED;

  if (isNewKey) {
    table->count++;

    if (entry->state == ENTRY_EMPTY) {
      table->used++;
    }
  }

  entry->key = key;
  entry->value = value;
  entry->state = ENTRY_OCCUPIED;
  return isNewKey;
}

bool tableDelete(Table* table, Value key) {
  if (table->count == 0) {
    return false;
  }

  Entry* entry = findEntry(table->entries, table->capacity, key);

  if (entry->state != ENTRY_OCCUPIED) {
    return false;
  }

  entry->key = NIL_VAL;
  entry->value = NIL_VAL;
  entry->state = ENTRY_TOMBSTONE;
  table->count--;
  return true;
}
