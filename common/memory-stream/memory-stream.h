#pragma once
#include <inttypes.h>
#include "common/stream.h"


class DuplexMemoryStream;

extern "C" IStream* new_simplex_memory_stream(uint32_t capacity);

extern "C" DuplexMemoryStream* new_duplex_memory_stream(uint32_t capacity);

// flag is a bitwise switch, showing if read(01) or write(10) are able to fail
// 00 ----- nothing goes fail
// 01 ----- read may fail (1%)
// 10 ----- write may fail (1%)
// 11 ----- both read and write may fail
// default is 11(3 in 10-based integer), both read and write operation may fail
extern "C" IStream* new_fault_stream(IStream* stream, int flag=3, bool ownership=false);

class DuplexMemoryStream
{
public:
    virtual ~DuplexMemoryStream() { }
    IStream* endpoint_a;     // do NOT delete it!!!
    IStream* endpoint_b;     // do NOT delete it!!!
    virtual int close() = 0;
};