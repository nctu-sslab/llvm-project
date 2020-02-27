// For deep-copy address translate
#ifndef _OMPTARGET_AT_H_
#define _OMPTARGET_AT_H_

#include <omptarget.h>

#include "device.h"
#include "private.h"

typedef struct AddressTranslate {

  intptr_t fake_table_size;
  intptr_t fake_table_byte;
  intptr_t table_size;
  void *tgt_table;


  void addTable(void *tgt_table);
  void *passArg(void *addr, int64_t size);
  void *passLiteral(void *literal, int64_t size);

  void addFakeSize(int64_t fake_size);
  void addFakeByte(int64_t fake_byte);

  void addTableSize(int64_t table_size);
} AddressTranslateTy;

extern AddressTranslateTy AT;

#endif
