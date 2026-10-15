/* SPDX-License-Identifier: GPL-3.0-or-later */
struct MultiDownloadPeer {
    struct CupidGBA* gba;
    uint8_t players, received[256];
    uint16_t reply, lower;
    uint32_t key, crc, finish;
    unsigned transfers, halfwords, state;
    bool preparing;
    uint64_t start, signature;
};
static bool multiplayer_download_word(void* context, uint16_t transmitted, unsigned baud,
                                      uint16_t received[4], uint64_t timestamp) {
    CHECK(baud == 3);
    struct MultiDownloadPeer* peer = context;
    const uint8_t random[] = {0, 0x13, 0x27, 0x39};
    for (unsigned player = 0; player < 4; player++) {
        received[player] = peer->players & (1u << player) ? peer->reply : 0xFFFF;
        if (player && (peer->players & (1u << player)) && (peer->preparing || !peer->transfers))
            received[player] = (uint16_t)(0x7300 | random[player]);
    }
    if (peer->preparing) return true;
    const uint64_t fields[] = {timestamp - peer->start, transmitted,
        received[1], received[2], received[3]};
    const unsigned widths[] = {8, 2, 2, 2, 2};
    for (unsigned field = 0; field < 5; field++) for (unsigned byte = 0; byte < widths[field]; byte++)
        peer->signature = (peer->signature ^ (uint8_t)(fields[field] >> (byte * 8))) * UINT64_C(1099511628211);
    if (!peer->transfers) {
        CHECK(transmitted == 256 / 4 - 0x34);
        peer->reply = 0xC0;
    } else if (peer->halfwords < 128) {
        if (!(peer->halfwords & 1)) peer->lower = transmitted;
        else {
            const uint32_t encoded = peer->lower | ((uint32_t)transmitted << 16);
            peer->key = peer->key * 0x6F646573 + 1;
            const unsigned offset = (peer->halfwords - 1) * 2;
            const uint32_t clear = encoded ^ peer->key ^
                (uint32_t)-(int32_t)(0x020000C0 + offset) ^ 0x6465646F;
            CHECK(clear == word(peer->gba->memory.ewram, 0x10000 + offset));
            put(peer->received, offset, clear);
            peer->crc = download_crc(peer->crc, clear, 0xA517);
        }
        ++peer->halfwords;
        peer->reply = (uint16_t)(0xC0 + peer->halfwords * 2);
    } else if (peer->state < 2) {
        CHECK(transmitted == 0x65);
        ++peer->state;
        peer->reply = 0x75;
    } else if (peer->state == 2) {
        CHECK(transmitted == 0x66);
        peer->state = 3;
        peer->crc = download_crc(peer->crc, peer->finish, 0xA517);
        peer->reply = (uint16_t)peer->crc;
    } else {
        CHECK(peer->state == 3 && transmitted == peer->crc);
        peer->state = 4;
    }
    ++peer->transfers;
    return true;
}
static void successful_multiplayer_download(unsigned fixture) {
    struct CupidGBA* gba = machine(fixture ? UINT32_MAX - 16u : 0, fixture != 0);
    struct MultiDownloadPeer peer = {.gba = gba, .players = fixture ? 15 : 3,
        .reply = 0x7313, .crc = 0xFFF8, .key = fixture ? 0x44434293 : 0xFFFF4293,
        .finish = fixture ? 0x392713DA : 0xFFFF1351, .preparing = true,
        .signature = UINT64_C(14695981039346656037)};
    CHECK(cupid_serial_set_multiplayer_peer(&gba->serial, 0, peer.players, multiplayer_download_word, &peer));
    /* Complete the caller's final initiation exchange before entering SWI 25h. */
    const uint8_t handshake = fixture ? 0xDA : 0x51;
    cupid_io_write(gba, REG_RCNT, 0);
    cupid_io_write(gba, REG_SIOMLT_SEND, 0x6400 | handshake);
    cupid_io_write(gba, REG_SIOCNT, 0x2083);
    cupid_timing_tick(&gba->timing, fixture ? 10486 : 5755);
    CHECK(!gba->serial.active);
    peer.preparing = false;
    uint8_t* params = parameters(gba);
    params[20] = handshake; params[25] = 0x42; params[26] = fixture ? 0x43 : 0xFF;
    params[27] = fixture ? 0x44 : 0xFF; params[28] = 0x93; params[30] = peer.players & 14;
    put(params, 32, 0x02010000); put(params, 36, 0x02010100);
    uint8_t expected[128]; memcpy(expected, params, sizeof(expected));
    memset(expected, 0, 20); memset(expected + 56, 0, 12);
    for (unsigned byte = 0; byte < 256; byte++) gba->memory.ewram[0x10000 + byte] = (uint8_t)(byte * 73 + 19);
    const uint64_t clocks = call(gba, 1, fixture != 0, fixture ? MODE_IRQ : MODE_SYS, &peer.start);
    CHECK(!gba->cpu.gprs[0] && peer.halfwords == 128 && peer.state == 4 && peer.transfers == 133);
    CHECK(!memcmp(peer.received, gba->memory.ewram + 0x10000, sizeof(peer.received)));
    CHECK(!memcmp(params, expected, sizeof(expected)));
    CHECK(!gba->serial.active);
    CHECK(clocks == multiDownloadExpected[fixture].clocks && peer.signature == multiDownloadExpected[fixture].signature);
    CHECK(peer.crc == multiDownloadExpected[fixture].crc);
    printf("Multiplayer download %u clocks=%" PRIu64 " transcript=%016" PRIx64 " crc=%04" PRIx32 "\n",
           fixture, clocks, peer.signature, peer.crc);
    destroy(gba);
}
