/*
 * OSA103 source module for SDR++.
 * Copyright (c) 2026 the OSA103 source module authors. Non-commercial / educational use only,
 * see LICENSE.md in this directory.
 *
 * Protocol reconstructed from the vendor's ExtIO_Osa.dll v3.09 (OSA103, osa103.ru)
 * by static analysis, for interoperability. No vendor code, listing or bitstream is
 * included. Provided as is, without warranty of any kind: it could not be tested
 * against hardware while it was written.
 */
#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace osa103 {

/*
Protocol parameters reconstructed from ExtIO_Osa.dll v3.09 (see OSA103_PROTOCOL.md).
Every field is a knob: the defaults are what the vendor DLL was observed to send, and
anything the static analysis could not settle is called out in the comments so it can
be changed from the UI or the config file without rebuilding.
*/
struct Params {
    // ---------------------------------------------------------------- transport
    std::string d2xxLibraryPath;   // "" = standard library names / search path
    std::string serial;            // "" = first enumerated device
    std::string bitstreamPath;     // FPGA image for the bit-bang loader; "" = skip
    bool loadBitstream = false;    // only needed if the device boots unconfigured
    bool resetOnOpen = false;      // FT_ResetDevice before configuring
    uint32_t baudRate = 3000000;   // DLL: 3 Mbaud
    uint8_t latencyMs = 2;         // DLL: 2 ms
    uint32_t readTimeoutMs = 20;
    uint32_t writeTimeoutMs = 20;
    int readChunk = 65536;

    // ------------------------------------------------------------------ framing
    bool crcEnabled = true;
    uint16_t crcInit = 0xFFFF;
    uint8_t syncByte0 = 0xAA;
    uint8_t syncByte1 = 0x0F;
    uint8_t syncByte2 = 0x55;
    int syncRepeat = 8;            // 8 x 3 = 24 bytes, then the terminator
    uint8_t syncTerminator = 0x00;
    bool syncSeparateWrite = true; // DLL writes the 25 sync bytes as their own FT_Write

    // ----------------------------------------------------------------- commands
    bool sendHello = true;
    uint8_t hello0 = 0xA5;         // bare write, no framing at all
    uint8_t hello1 = 0x00;
    uint8_t hello2 = 0x00;
    bool sendConfigOnStart = true;
    bool resendConfigOnTune = true;

    // Type 0x01 block write (12-bit table). The DLL uploads a 4096-entry table it
    // computes from the sample rate; the content is not reproduced here (unknown
    // synthesis), so an RX-only driver normally leaves this off.
    bool sendType1Table = false;
    uint16_t type1BaseAddr = 0xC000;
    uint16_t type1Stride = 0x0400;
    int type1Blocks = 8;
    uint16_t type1FillValue = 0x0555;

    // --------------------------------------------------- type 0x03 register image
    // Offsets below are frame offsets from OSA103_PROTOCOL.md; the DLL holds these as
    // constants and their firmware meaning is unresolved, so they are all visible.
    uint8_t tag3 = 0x18;
    uint8_t tag4 = 0x00;
    uint8_t tag5 = 0x08;
    uint8_t tag6 = 0xFF;

    uint8_t rxtxTag = 0x80;        // 0x80 base, |0x01 Tx-IF, |0x04 Tx path
    uint8_t dividerTag = 0x03;     // frame offset 17
    uint8_t dividerTag2 = 0x03;    // frame offset 23
    uint8_t txDivShift = 0;
    uint16_t txDivQ = 0;
    uint16_t txDivR = 0;
    uint8_t txDivShift2 = 0;
    uint16_t txDivQ2 = 0;
    uint16_t txDivR2 = 0;
    uint16_t const6258 = 0x6258;   // 0x6258 / 0x0028 depending on the model

    uint8_t tag31 = 0x1F;
    uint8_t tag32 = 0x00;
    uint8_t tag33 = 0x40;
    uint8_t tag34 = 0xFF;

    uint8_t flags = 0x19;          // preamp | 50R | dither | always-0x10
    uint16_t adcGainWord = 0x8000;
    uint16_t adcOffWord = 0x0000;
    uint16_t adcOffWord2 = 0x0000;
    uint8_t calA = 0, calB = 0, calC = 0, calD = 0, calE = 0;
    uint8_t calF = 0, calG = 0, calH = 0;

    uint8_t tag64 = 0xC3;
    uint8_t tag65 = 0x00;
    uint8_t tag66 = 0x06;
    uint8_t tag67 = 0x00;
    uint8_t tag68 = 0x80;
    uint8_t tag69 = 0xFF;

    uint8_t mode = 0x18;           // demod mode | 0x08 | 0x10 | ...
    uint8_t level = 0x80;          // Tx level, 0..255

    // tuning maths
    double ncoScale = 21.47483648; // 2^31 / 1e8
    double ncoIfHz = 21395000.0;   // IF the DLL uses to derive the NCO from the dial
    int ncoMode = 0;               // 0 = dial, 1 = dial+IF, 2 = dial-IF, 3 = IF only
    double loScale = 1.0;          // multiplier applied to the LO field
    bool sendTxNco = false;        // the DLL also sends a Tx NCO; 0 when idle

    // A pasted or captured image replaces the generated one byte for byte.
    std::string imageOverrideHex;

    // ------------------------------------------------------------------- stream
    bool hasFlagsByte = true;
    int dataBytes = 2048;          // I/Q payload bytes per data packet
    int crcBytes = 2;
    // Device->host packets: whether they repeat the host frame header (u16 length
    // field before the type byte) or start with the type byte straight after sync.
    // The DLL's reader is written for fixed-size packets, i.e. no length field, but
    // this could not be confirmed against hardware.
    bool rxHasLenField = false;
    bool bigEndianSamples = false;
    bool swapIQ = false;
    bool invertQ = false;
    double sampleScale = 2.5322;   // DLL multiplies every sample by this
    bool muteOnFlagBit1 = true;    // flag bit1 = transmitter keyed -> RX muted
    int infoPacketBytes = 149;     // 0x5A packet length including type and CRC

    // -------------------------------------------------------------------- debug
    bool sniffOnly = false;        // never send anything, just dump what arrives
    bool logFrames = false;        // log TX/RX frames as hex
    int logHexBytes = 96;
};

constexpr size_t kConfigImageLen = 73;
constexpr size_t kSyncLen = 25;

/*
Receive-side packet parser, split out of the SDR++ module so it can be exercised by
a standalone test without a device. feed() accepts arbitrary chunk boundaries: it
buffers until a whole packet is available, drops bytes that cannot be part of a sync
pattern, and calls the callbacks with raw packet contents.

Everything the parser assumes about the inbound stream is a Params field, because the
inbound format could not be verified against hardware: sync pattern and repetition,
whether a data packet carries a leading flags byte, the I/Q payload size, and the CRC
width.
*/
class Parser {
public:
    explicit Parser(const Params& params);

    void setParams(const Params& params) { p = params; }
    const Params& params() const { return p; }

    void reset();
    void feed(const uint8_t* data, size_t len);

    // Ready-to-use payload of a data packet (either the whole payload or, with
    // hasFlagsByte, everything after the flags byte) plus its flag byte.
    std::function<void(const uint8_t* payload, int len, uint8_t flags)> onData;
    std::function<void(const uint8_t* payload, int len)> onInfo;

    uint64_t bytesDiscarded = 0;
    uint64_t dataPackets = 0;
    uint64_t infoPackets = 0;
    uint64_t crcErrors = 0;
    uint8_t lastFlags = 0;

private:
    void process();

    Params p;
    std::vector<uint8_t> buf;
};

uint16_t crc16(const uint8_t* data, size_t len, uint16_t init = 0xFFFF);

// Number of bytes written into out (the sync prefix), 25 for the observed pattern.
size_t buildSync(const Params& p, uint8_t* out, size_t outCapacity);

// Appends sync + frame; returns the number of bytes or 0 if outCapacity is too small.
size_t buildFrame(const Params& p, uint8_t type, const uint8_t* payload, size_t payloadLen,
                  uint8_t* out, size_t outCapacity);

// The 73-byte configuration image (payload of the type 0x03 frame).
size_t buildConfigImage(const Params& p, double dialHz, uint8_t* out, size_t outCapacity);

// sync + type 0x03 frame for a dial frequency.
size_t buildConfigFrame(const Params& p, double dialHz, uint8_t* out, size_t outCapacity);

// sync + type 0x01 block write for one 512-word block.
size_t buildType1Frame(const Params& p, uint16_t addr, uint16_t fillValue, uint8_t* out,
                       size_t outCapacity);

// NCO phase word for a frequency.
int64_t ncoWord(double hz, double scale);

// "AA 0F 55 ..." rendering, capped at maxBytes.
std::string hexDump(const uint8_t* data, size_t len, size_t maxBytes);

// Parses "AA0F55" / "AA 0F 55" / "aa,0f,55" into bytes. Returns false on bad input.
bool parseHexBytes(const std::string& text, std::vector<uint8_t>& out);

}  // namespace osa103
