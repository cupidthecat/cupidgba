/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include "caller.h"
#include "multiboot-normal.inc"
#include "multiboot-multi-frames.inc"
#include "multiboot-peer-frames.inc"

static const char* externalBios;
static uint32_t parameterAddress;
static bool unmaskedCaller;

static uint32_t word(const uint8_t* data, unsigned offset) {
    uint32_t result; memcpy(&result, data + offset, 4); return result;
}
static void put(uint8_t* data, unsigned offset, uint32_t value) { memcpy(data + offset, &value, 4); }
static uint8_t* parameters(struct CupidGBA* gba) {
    return parameterAddress >> 24 == 2 ? gba->memory.ewram + (parameterAddress & 0x3FFFF)
                                     : gba->memory.iwram + (parameterAddress & 0x7FFF);
}
static struct CupidGBA* machine(uint64_t clock, bool internal) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba);
    if (externalBios) CHECK(cupid_load_bios(gba, externalBios));
    cupid_gba_reset(gba);
    cupid_test_bios_caller(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock; gba->timing.masterCycles = (uint32_t)clock;
    cupid_video_reset(&gba->video);
    cupid_audio_reset(&gba->audio);
    gba->audio.lastSample = gba->audio.sampleOrigin = clock;
    gba->audio.nextSample = clock + 512;
    parameterAddress = internal ? 0x03002000 : 0x02008000;
    memset(parameters(gba), 0xA5, 128);
    return gba;
}
static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }

struct DownloadPeer {
    struct CupidGBA* gba;
    uint32_t key, crc, polynomial, xorKey, reply, response, transmitted, previous;
    unsigned bits, words, length, transfers, state, fault, waitReplies;
    uint8_t handshake, random, received[1024];
    const uint8_t* source;
    uint8_t* output;
    void (*finished)(void* context, uint64_t timestamp);
    void* finishedContext;
    bool clock;
    uint64_t start, signature;
};
static uint32_t download_crc(uint32_t crc, uint32_t data, uint32_t polynomial) {
    crc ^= data;
    for (unsigned bit = 0; bit < 32; bit++) crc = (crc >> 1) ^ ((crc & 1) ? polynomial : 0);
    return crc;
}
static void record_word(struct DownloadPeer* peer, uint32_t transmitted, uint64_t timestamp) {
    const uint64_t fields[] = {timestamp - peer->start, transmitted, peer->response};
    const unsigned widths[] = {8, 4, 4};
    for (unsigned field = 0; field < 3; field++) for (unsigned byte = 0; byte < widths[field]; byte++)
        peer->signature = (peer->signature ^ (uint8_t)(fields[field] >> (byte * 8))) * UINT64_C(1099511628211);
}
static void download_word(struct DownloadPeer* peer, uint32_t transmitted, uint64_t timestamp) {
    record_word(peer, transmitted, timestamp);
    if (!peer->transfers) {
        CHECK(transmitted == peer->length / 4 - 0x34);
        peer->reply = peer->fault == 2 ? 0x00C1 : 0x00C0;
    } else if (peer->words < peer->length / 4) {
        peer->key = peer->key * 0x6F646573 + 1;
        const uint32_t clear = transmitted ^ peer->key ^
            (uint32_t)-(int32_t)(0x020000C0 + peer->words * 4) ^ peer->xorKey;
        put(peer->output ? peer->output : peer->received, peer->words * 4, clear);
        const uint8_t* source = peer->source ? peer->source : peer->gba->memory.ewram + 0x10000;
        CHECK(clear == word(source, peer->words * 4));
        peer->crc = download_crc(peer->crc, clear, peer->polynomial);
        ++peer->words;
        peer->reply = 0x00C0 + peer->words * 4;
    } else if (!peer->state) {
        CHECK(transmitted == 0x65);
        peer->state = 1;
        peer->reply = peer->waitReplies ? 0x74 : 0x75;
    } else if (peer->state == 1) {
        CHECK(transmitted == 0x65);
        if (peer->response >> 16 == 0x75) peer->state = 2;
        else {
            CHECK(peer->response >> 16 == 0x74 && peer->waitReplies);
            if (!--peer->waitReplies) peer->reply = 0x75;
        }
    } else if (peer->state == 2) {
        CHECK(transmitted == 0x66);
        peer->state = 3;
        peer->crc = download_crc(peer->crc,
            0xFFFF0000u | ((uint32_t)peer->random << 8) | peer->handshake, peer->polynomial);
        peer->reply = peer->crc ^ (peer->fault == 1);
    } else {
        CHECK(peer->state == 3 && transmitted == peer->crc);
        peer->state = 4;
        if (peer->finished) peer->finished(peer->finishedContext, timestamp);
    }
    peer->previous = transmitted;
    ++peer->transfers;
}
static void download_pins(void* context, uint8_t mask, uint8_t levels, uint64_t timestamp) {
    (void)mask;
    struct DownloadPeer* peer = context;
    const bool clock = (levels & SERIAL_SC) != 0;
    if (peer->gba->serial.active) {
        if (peer->clock && !clock) {
            if (peer->gba->serial.remaining == 32) {
                peer->bits = 0; peer->transmitted = 0;
                peer->response = (peer->reply << 16) | (peer->previous & 0xFFFF);
            }
            cupid_serial_set_input(&peer->gba->serial, SERIAL_SI,
                peer->response & (0x80000000u >> peer->bits) ? SERIAL_SI : 0);
        } else if (!peer->clock && clock) {
            peer->transmitted = (peer->transmitted << 1) | ((levels & SERIAL_SO) != 0);
            if (++peer->bits == 32) download_word(peer, peer->transmitted, timestamp);
        }
    }
    peer->clock = clock;
}
static uint64_t execute_call(struct CupidGBA* gba, unsigned mode, bool thumb,
                             enum CupidPrivMode bank, uint64_t* traceStart, unsigned instructionLimit) {
    const uint32_t wide[] = {0xEF250000, 0xEAFFFFFE};
    const uint16_t narrow[] = {0xDF25, 0xE7FE};
    memcpy(gba->memory.iwram + 0x100, thumb ? (const void*)narrow : (const void*)wide,
           thumb ? sizeof(narrow) : sizeof(wide));
    cupid_arm_set_priv_mode(&gba->cpu, bank);
    for (unsigned reg = 0; reg < 15; reg++) gba->cpu.gprs[reg] = (int32_t)(0x11220000u + reg);
    gba->cpu.gprs[0] = (int32_t)parameterAddress; gba->cpu.gprs[1] = (int32_t)mode;
    gba->cpu.gprs[ARM_SP] = 0x03007000;
    const uint32_t status = 0xA0000000u | (unmaskedCaller ? 0 : 0x80u) | bank | (thumb ? 32u : 0u);
    gba->cpu.cpsr.packed = (int32_t)status; gba->cpu.spsr.packed = 0x6500003F;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = 0x03000100;
    if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
    const uint64_t start = cupid_timing_global_time(&gba->timing);
    if (traceStart) *traceStart = start;
    unsigned steps;
    for (steps = 0; steps < instructionLimit; steps++) {
        cupid_gba_step(gba);
        if (gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM) &&
            (uint32_t)gba->cpu.gprs[ARM_PC] == 0x03000100u + (thumb ? 4u : 8u)) break;
    }
    if (steps == instructionLimit) fprintf(stderr, "Download did not return: pc=%08" PRIx32 "\n", (uint32_t)gba->cpu.gprs[ARM_PC]);
    CHECK(steps < instructionLimit);
    CHECK(gba->cpu.privilegeMode == bank && (uint32_t)gba->cpu.cpsr.packed == status);
    CHECK((uint32_t)gba->cpu.gprs[1] == mode && (uint32_t)gba->cpu.gprs[2] == 0x11220002);
    for (unsigned reg = 3; reg < 15; reg++)
        CHECK((uint32_t)gba->cpu.gprs[reg] == (reg == ARM_SP ? 0x03007000u : 0x11220000u + reg));
    if (bank == MODE_IRQ) CHECK((uint32_t)gba->cpu.spsr.packed == 0x6500003F);
    return cupid_timing_global_time(&gba->timing) - start;
}
static uint64_t call(struct CupidGBA* gba, unsigned mode, bool thumb,
                     enum CupidPrivMode bank, uint64_t* traceStart) {
    /* The initial delay alone exceeds 524,000 instructions. Retain the
     * original two-million-instruction guard for every small-download case. */
    return execute_call(gba, mode, thumb, bank, traceStart, 2000000);
}
#include "multiboot-bus-inputs.inc"

static void normal_download(unsigned fixture, unsigned fault, bool interactions) {
    const unsigned modes[] = {0, 2, 0, 2}, lengths[] = {256, 512, 256, 1024};
    const uint64_t origins[] = {0, UINT32_MAX - 16u, UINT64_C(0x300000000), 0};
    struct CupidGBA* gba = machine(origins[fixture], fixture >= 2);
    struct DownloadBusWatch watch = {.gba = gba};
    struct DownloadPeer peer = {.gba = gba, .key = 0xFFFF4293, .crc = 0xC387, .polynomial = 0xC37B,
        .xorKey = 0x43202F2F, .reply = 0x7313, .length = lengths[fixture], .handshake = 0x51,
        .random = 0x13, .clock = true, .previous = 0x6451, .signature = UINT64_C(14695981039346656037),
        .fault = fault, .waitReplies = fault == 3 ? 2 : 0};
    memset(peer.received, 0xA5, sizeof(peer.received));
    for (unsigned byte = 0; byte < peer.length; byte++) gba->memory.ewram[0x10000 + byte] = (uint8_t)(byte * 73 + 19);
    uint8_t* params = parameters(gba);
    params[20] = peer.handshake; params[25] = 0x42; params[26] = params[27] = 0xFF;
    params[28] = 0x93; params[30] = 2;
    put(params, 32, 0x02010000); put(params, 36, 0x02010000 + peer.length);
    uint8_t expected[128]; memcpy(expected, params, sizeof(expected));
    const unsigned result = fault == 1 || fault == 2 || fault == 4 || fault == 5;
    for (unsigned field = 0; field < 5; field++) put(expected, field * 4, result);
    for (unsigned field = 0; field < 3; field++) put(expected, 56 + field * 4, result);
    cupid_io_write(gba, REG_RCNT, 0); cupid_io_write(gba, REG_SIOCNT, 0x1000);
    cupid_io_write(gba, REG_SIODATA32_LO, 0x6451);
    cupid_io_write(gba, REG_SIODATA32_HI, fault == 4 ? 0x7213 : 0x7313);
    cupid_serial_set_output_callback(&gba->serial, fault == 5 ? NULL : download_pins, &peer);
    if (interactions) {
        start_download_bus_watch(&watch);
        peer.finished = watch_download_completion; peer.finishedContext = &watch;
    }
    unmaskedCaller = interactions;
    const uint64_t clocks = call(gba, modes[fixture], (fixture & 1) != 0,
        interactions ? MODE_SYS : (fixture & 1 ? MODE_IRQ : MODE_SYS), &peer.start);
    unmaskedCaller = false;
    CHECK((unsigned)gba->cpu.gprs[0] == result && !gba->serial.active);
    if (fault == 2) CHECK(peer.words == 1 && peer.transfers == 2 && !peer.state);
    else if (fault == 4 || fault == 5) CHECK(!peer.words && !peer.transfers && !peer.state);
    else CHECK(peer.words == peer.length / 4 && peer.state == 4 &&
        peer.transfers == peer.words + (fault == 3 ? 7u : 5u));
    CHECK(!memcmp(peer.received, gba->memory.ewram + 0x10000, peer.words * 4));
    for (unsigned byte = peer.words * 4; byte < sizeof(peer.received); byte++) CHECK(peer.received[byte] == 0xA5);
    CHECK(!memcmp(expected, params, sizeof(expected)));
    if (interactions) {
        static const struct DownloadExpected observations[] = {
            {UINT64_C(1238452), UINT64_C(0xFA1B64F03D0E3DC0), 0xC67B},
            {UINT64_C(1206323), UINT64_C(0x73B6E24B50AB51D5), 0x7942},
        };
        CHECK(clocks == observations[fixture].clocks && peer.signature == observations[fixture].signature);
        CHECK(peer.crc == observations[fixture].crc);
        finish_download_bus_watch(&watch, (fixture & 1) != 0, modes[fixture]);
    } else if (!fault) {
        CHECK(clocks == downloadExpected[fixture].clocks && peer.signature == downloadExpected[fixture].signature);
        CHECK(peer.crc == downloadExpected[fixture].crc);
    } else {
        const struct DownloadExpected* observation = &peerDownloadExpected[fault - 1];
        CHECK(clocks == observation->clocks && peer.signature == observation->signature && peer.crc == observation->crc);
    }
    printf("%s download %u clocks=%" PRIu64 " transcript=%016" PRIx64 " crc=%04" PRIx32 "\n",
           interactions ? "Bus" : (fault ? "Peer" : "Normal"), fault ? fault : fixture, clocks, peer.signature, peer.crc);
    destroy(gba);
}
static void successful_normal_download(unsigned fixture) { normal_download(fixture, 0, false); }
static void empty_source_guards(void) {
    const uint64_t clocks[] = {256, 252, 256, 196};
    for (unsigned mode = 0; mode < 4; mode++) {
        struct CupidGBA* gba = machine(0, false);
        memset(parameters(gba), 0, 128);
        CHECK(call(gba, mode, false, MODE_SYS, NULL) == clocks[mode]);
        CHECK(gba->cpu.gprs[0] == 1);
        uint8_t expected[128] = {0};
        for (unsigned field = 0; field < 5; field++) put(expected, field * 4, 1);
        put(expected, 56, 1); put(expected, 60, 1); put(expected, 64, 1);
        CHECK(!memcmp(parameters(gba), expected, sizeof(expected)));
        destroy(gba);
    }
}
#include "multiboot-multiplayer.inc"
#include "multiboot-source-inputs.inc"
#include "multiboot-guard-inputs.inc"

int main(int argc, char** argv) {
    CHECK(argc == 2 || (argc == 4 && !strcmp(argv[2], "--external-bios")));
    if (argc == 4) externalBios = argv[3];
    if (!strcmp(argv[1], "guards")) RUN(empty_source_guards);
    else if (!strcmp(argv[1], "normal-0")) successful_normal_download(0);
    else if (!strcmp(argv[1], "normal-1")) successful_normal_download(1);
    else if (!strcmp(argv[1], "normal-2")) successful_normal_download(2);
    else if (!strcmp(argv[1], "normal-3")) successful_normal_download(3);
    else if (!strcmp(argv[1], "multi-0")) successful_multiplayer_download(0);
    else if (!strcmp(argv[1], "multi-1")) successful_multiplayer_download(1);
    else if (!strcmp(argv[1], "multi-2")) successful_multiplayer_download(2);
    else if (!strcmp(argv[1], "multi-sparse")) successful_multiplayer_download(3);
    else if (!strcmp(argv[1], "multi-unselected")) multiplayer_download(3, 3);
    else if (!strcmp(argv[1], "multi-offset-reject")) multiplayer_download(2, 1);
    else if (!strcmp(argv[1], "multi-crc-reject")) multiplayer_download(2, 2);
    else if (!strcmp(argv[1], "crc-reject")) normal_download(0, 1, false);
    else if (!strcmp(argv[1], "offset-reject")) normal_download(0, 2, false);
    else if (!strcmp(argv[1], "client-wait")) normal_download(0, 3, false);
    else if (!strcmp(argv[1], "client-reject")) normal_download(0, 4, false);
    else if (!strcmp(argv[1], "disconnect")) normal_download(0, 5, false);
    else if (!strcmp(argv[1], "bus-0")) normal_download(0, 0, true);
    else if (!strcmp(argv[1], "bus-1")) normal_download(1, 0, true);
    else if (!strcmp(argv[1], "source-iwram")) source_download(0);
    else if (!strcmp(argv[1], "source-rom-page")) source_download(1);
    else if (!strcmp(argv[1], "maximum")) source_download(2);
    else if (!strcmp(argv[1], "guard-matrix-0")) guarded_source_matrix(0);
    else if (!strcmp(argv[1], "guard-matrix-1")) guarded_source_matrix(1);
    else if (!strcmp(argv[1], "guard-matrix-2")) guarded_source_matrix(2);
    else if (!strcmp(argv[1], "guard-readonly")) readonly_parameter_guard();
    else if (!strcmp(argv[1], "guard-dma")) enabled_dma_guard();
    else CHECK(false);
    return 0;
}
