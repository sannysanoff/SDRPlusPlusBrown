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

    // Type 0x01 block write (12-bit table). The vendor session uploads eight
    // blocks at 0xC000 + n*0x400 whose content is a continuous ramp (not a
    // constant): blocks 0..3 run 0x0555..0x0D50, block 4 is all zeros, block 5
    // is 169 zeros then 0x0000..0x0156, blocks 6..7 run 0x0157..0x0554. The
    // frame layout matches the builder below byte for byte.
    // The FPGA expects the eight ramp blocks right after the configuration
    // frame; the vendor session always sends them, and live tests confirmed
    // the stream only runs when they follow the config frame.
    bool sendType1Table = true;
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
    // Captured from a working vendor session: both divider shift bytes are 0x07
    // with zeroed quotient/remainder words.
    uint8_t txDivShift = 7;
    uint16_t txDivQ = 0;
    uint16_t txDivR = 0;
    uint8_t txDivShift2 = 7;
    uint16_t txDivQ2 = 0;
    uint16_t txDivR2 = 0;
    uint16_t const6258 = 0x6258;   // 0x6258 / 0x0028 depending on the model

    uint8_t tag31 = 0x1F;
    uint8_t tag32 = 0x00;
    uint8_t tag33 = 0x40;
    uint8_t tag34 = 0xFF;

    uint8_t flags = 0x39;          // preamp | 50R | dither | always-0x10
    uint16_t adcGainWord = 0x0000; // vendor session sends zero
    // Constant calibration bytes observed in every captured config frame at
    // image offset 42 (frame 45..50); may be device-specific, so they stay
    // visible. The static analysis had these four bytes too late.
    uint8_t calA = 0x54, calB = 0x76, calC = 0x95, calD = 0x97, calE = 0x23;
    uint8_t calF = 0xDD;

    uint8_t tag55 = 0x82;          // constant in every capture
    uint16_t tag56 = 0xAD01;       // "01 AD" at frames 56..57 in every capture

    uint8_t tag64 = 0xC3;
    uint8_t tag65 = 0x00;
    uint8_t tag66 = 0x06;
    uint8_t tag67 = 0x00;
    uint8_t tag68 = 0x80;
    uint8_t tag69 = 0xFF;

    uint8_t mode = 0x1D;           // demod mode | 0x08 | 0x10 | ...
    uint8_t level = 0x13;         // Tx level, 0..255

    // tuning maths
    double ncoScale = 21.47483648; // 2^31 / 1e8
    double ncoIfHz = 21395000.0;   // IF the DLL uses to derive the NCO from the dial
    int ncoMode = 0;               // 0 = dial, 1 = dial+IF, 2 = dial-IF, 3 = IF only
    // The LO field is the dial frequency in Hz, confirmed by the capture:
    // every 1 MHz dial step moves it by exactly 1,000,000.
    double loScale = 1.0;
    // The trailing NCO word (frame 71..74) is (dial_Hz - 16000) * 2^31 / 2e8 in
    // every capture's initial frame (exact to the last digit); retunes carry a
    // vendor phase trim on top, which the device does not need from us.
    bool sendTxNco = true;
    double txNcoScale = 10.73741824; // 2^31 / 2e8
    double txNcoOffsetHz = -16000.0;

    // A pasted or captured image replaces the generated one byte for byte.
    std::string imageOverrideHex;

    // ------------------------------------------------------------------- stream
    // Captured inbound format: every packet repeats the 25-byte sync, then a
    // type byte. Type 0x5A = info packet (151 bytes after the sync, subtype
    // byte first). Type 0x00 = sample packet: 2048 bytes of little-endian
    // int16 I/Q pairs, then CRC16 - 2076 bytes total, ~32 ms apart at the
    // declared 65104 S/s. No flags byte, no length field.
    bool hasFlagsByte = false;
    int dataBytes = 2048;
    int crcBytes = 2;
    bool rxHasLenField = false;
    bool bigEndianSamples = false;
    bool swapIQ = false;
    bool invertQ = false;
    // int16 -> float conversion. SDR++ convention is normalized I/Q (full ADC
    // scale maps to +/-1.0). The vendor DLL instead multiplies by a fixed
    // 2.5322 (static analysis of ExtIO_Osa.dll) with no normalization; set
    // sampleScale to 2.5322 to reproduce its absolute levels.
    double sampleScale = 1.0 / 32768.0;
    bool muteOnFlagBit1 = true;    // flag bit1 = transmitter keyed -> RX muted
    // Captured stream format: every inbound packet starts with the sync and a
    // type byte. 0x5A = fixed-size info packet (151 bytes after the sync,
    // subtype first). 0x00 = sample packet with 2048 bytes of little-endian
    // int16 pairs, then CRC16.
    int infoPacketBytes = 151;     // bytes after the sync: 5A + subtype + body + CRC

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

// sync + type 0x01 block write with the ramp content the working vendor session
// sends: block 0..3 ramps starting 0x0555/0x0754/0x0953/0x0B52, block 4 zeros,
// block 5 = 169 zeros then a ramp from 0x0000, blocks 6..7 ramps from
// 0x0157/0x0356. block is 0-based.
size_t buildTableFrame(const Params& p, int block, uint8_t* out, size_t outCapacity);

// NCO phase word for a frequency.
int64_t ncoWord(double hz, double scale);

// "AA 0F 55 ..." rendering, capped at maxBytes.
std::string hexDump(const uint8_t* data, size_t len, size_t maxBytes);

// Parses "AA0F55" / "AA 0F 55" / "aa,0f,55" into bytes. Returns false on bad input.
bool parseHexBytes(const std::string& text, std::vector<uint8_t>& out);

}  // namespace osa103
