#include "at.h"

AddressTranslateTy AT;


// Tmp function before kernel rewriter
void *AddressTranslate::passArg(void *addr, int64_t size) {
  if (size == this->fake_table_byte) {
    // return addr of real region list
    DP2("*Replaced arg table from %p to %p\n", addr, this->tgt_table);
    return this->tgt_table;
  } else {
    return addr;
  }
}

void *AddressTranslate::passLiteral(void *literal, int64_t size) {
  // mask value
  uintptr_t mask;

  if (size < sizeof(void*)) {
    mask = pow(2, size*8);
    mask -= 1;
  } else {
    mask = 0;
    mask -= 1;
  }

  intptr_t val = mask & (intptr_t) literal;
  //DP2("Get value: %" PRIdPTR " %" PRIxPTR "\n", val, val);
  //DP2("Fake size is %" PRId64 "\n", fake_table_size);
  if (val == this->fake_table_size) {
    DP2("*Replaced arg table size from %" PRIdPTR " to %" PRIdPTR "\n", val, table_size);
    return (void*)this->table_size;
  }
  return literal;
}

void AddressTranslate::addTable(void *tgt_table) {
  DP2("Add tgt_table: %p\n", tgt_table);
  this->tgt_table = tgt_table;
}

void AddressTranslate::addFakeSize( int64_t fake_size) {
  this->fake_table_size = fake_size;
}
void AddressTranslate::addFakeByte( int64_t fake_byte) {
  this->fake_table_byte = fake_byte;
}

void AddressTranslate::addTableSize( int64_t table_size) {
  this->table_size = table_size;
}
