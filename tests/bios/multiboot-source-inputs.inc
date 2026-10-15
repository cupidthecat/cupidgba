/* SPDX-License-Identifier: GPL-3.0-or-later */
static void consume_download_audio(void* context, uint64_t clock, int16_t left, int16_t right) {
    (void)clock; (void)left; (void)right;
    struct CupidGBA* gba = context;
    gba->audio.sampleReadPos = gba->audio.sampleWritePos;
}
static void source_download(unsigned fixture) {
    static const struct DownloadExpected observations[] = {
        {UINT64_C(1205034), UINT64_C(0xB22175196C1F7ADC), 0x8BE5},
        {UINT64_C(1127002), UINT64_C(0xBC7E940B535EACE9), 0x7863},
        {UINT64_C(79115624), UINT64_C(0x78F2D23358259AB2), 0x5938},
    };
    const unsigned lengths[] = {512, 256, 0x3FF40};
    const uint64_t origins[] = {UINT32_MAX - 16u, UINT64_C(0x300000000), 0};
    struct CupidGBA* gba = machine(origins[fixture], fixture == 1);
    uint8_t* source;
    uint32_t address;
    if (!fixture) {
        address = 0x03004000;
        source = gba->memory.iwram + 0x4000;
    } else {
        gba->memory.rom = calloc(1, 0x80000); CHECK(gba->memory.rom);
        gba->memory.romSize = gba->memory.romImageSize = 0x80000;
        gba->memory.romMask = 0x7FFFF;
        const unsigned offset = fixture == 1 ? 0x1FF80 : 0x1000;
        address = 0x08000000 + offset;
        source = (uint8_t*)gba->memory.rom + offset;
    }
    const unsigned length = lengths[fixture];
    memset(source - 16, 0xA5, length + 32);
    for (unsigned byte = 0; byte < length; byte++) source[byte] = (uint8_t)(byte * 73 + 19);
    uint8_t* received = malloc(length + 32); CHECK(received); memset(received, 0xA5, length + 32);
    struct DownloadPeer peer = {.gba = gba, .key = 0xFFFF4293, .crc = 0xC387, .polynomial = 0xC37B,
        .xorKey = 0x43202F2F, .reply = 0x7313, .length = length, .handshake = 0x51,
        .random = 0x13, .clock = true, .previous = 0x6451, .signature = UINT64_C(14695981039346656037),
        .source = source, .output = received + 16};
    uint8_t* params = parameters(gba);
    params[20] = 0x51; params[25] = 0x42; params[26] = params[27] = 0xFF;
    params[28] = 0x93; params[30] = 2; put(params, 32, address); put(params, 36, address + length);
    uint8_t expected[128]; memcpy(expected, params, sizeof(expected));
    memset(expected, 0, 20); memset(expected + 56, 0, 12);
    cupid_io_write(gba, REG_RCNT, 0); cupid_io_write(gba, REG_SIOCNT, 0x1000);
    cupid_io_write(gba, REG_SIODATA32_LO, 0x6451); cupid_io_write(gba, REG_SIODATA32_HI, 0x7313);
    cupid_serial_set_output_callback(&gba->serial, download_pins, &peer);
    gba->audio.sampleSink = consume_download_audio; gba->audio.sampleSinkContext = gba;
    const uint64_t clocks = fixture == 2 ?
        execute_call(gba, 2, false, MODE_SYS, &peer.start, 150000000) :
        call(gba, 2, fixture == 0, fixture == 0 ? MODE_IRQ : MODE_SYS, &peer.start);
    CHECK(!gba->cpu.gprs[0] && peer.words == length / 4 && peer.state == 4);
    CHECK(peer.transfers == length / 4 + 5 && !gba->serial.active);
    CHECK(!memcmp(peer.output, source, length) && !memcmp(params, expected, sizeof(expected)));
    for (unsigned byte = 0; byte < length; byte++) CHECK(source[byte] == (uint8_t)(byte * 73 + 19));
    for (unsigned byte = 0; byte < 16; byte++) {
        CHECK(source[(int)byte - 16] == 0xA5 && source[length + byte] == 0xA5);
        CHECK(received[byte] == 0xA5 && received[length + 16 + byte] == 0xA5);
    }
    CHECK(!gba->audio.sampleOverruns);
    CHECK(clocks == observations[fixture].clocks && peer.signature == observations[fixture].signature);
    CHECK(peer.crc == observations[fixture].crc);
    printf("Source download %u clocks=%" PRIu64 " transcript=%016" PRIx64 " crc=%04" PRIx32 "\n",
           fixture, clocks, peer.signature, peer.crc);
    free(received); destroy(gba);
}
