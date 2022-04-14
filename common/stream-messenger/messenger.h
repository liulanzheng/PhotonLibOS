#pragma once
#include <cinttypes>
#include <photon/common/message-channel.h>
#include <photon/common/stream.h>

// a message channel based on any `IStream` object
namespace StreamMessenger
{
    // header of the stream message channel
    struct Header
    {
        const static uint64_t MAGIC   = 0x4962b4d24caa439e;
        const static uint32_t VERSION = 0;

        uint64_t magic   = MAGIC;       // the header magic
        uint32_t version = VERSION;     // version of the message
        uint32_t size;                  // size of the payload, not including the header
        uint64_t reserved = 0;          // padding to 24 bytes
    };

    // This class turns a stream into a message channel, by adding a `Header` to the
    // message before send it, and extracting the header immediately after recv it.
    // The stream can be any `IStream` object, like TCP socket or UNIX domain socket.
    IMessageChannel* new_messenger(IStream* stream);
}
