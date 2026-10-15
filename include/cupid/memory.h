/**
 * @file memory.h
 * @brief GBA memory map and bus definitions.
 */

#ifndef CUPID_MEMORY_H
#define CUPID_MEMORY_H

#include "cupid/types.h"
#include "cupid/timing.h"
#include "cupid/arm.h"

struct CupidGBA;

/** @name Memory Region Indices (address >> 24) */
enum GBAMemoryRegion {
    GBA_REGION_BIOS         = 0x00,
    GBA_REGION_WORKING_RAM  = 0x02,
    GBA_REGION_WORKING_IRAM = 0x03,
    GBA_REGION_IO           = 0x04,
    GBA_REGION_PALETTE_RAM  = 0x05,
    GBA_REGION_VRAM         = 0x06,
    GBA_REGION_OAM          = 0x07,
    GBA_REGION_ROM0         = 0x08,
    GBA_REGION_ROM0_EX      = 0x09,
    GBA_REGION_ROM1         = 0x0A,
    GBA_REGION_ROM1_EX      = 0x0B,
    GBA_REGION_ROM2         = 0x0C,
    GBA_REGION_ROM2_EX      = 0x0D,
    GBA_REGION_SRAM         = 0x0E,
    GBA_REGION_SRAM_MIRROR  = 0x0F
};

/** Backwards-compatible aliases used throughout our codebase */
#define GBA_REGION_EWRAM    GBA_REGION_WORKING_RAM
#define GBA_REGION_IWRAM    GBA_REGION_WORKING_IRAM
#define GBA_REGION_PALETTE  GBA_REGION_PALETTE_RAM

/** @name Memory Region Base Addresses */
enum GBAMemoryBase {
    GBA_BASE_BIOS         = 0x00000000,
    GBA_BASE_EWRAM        = 0x02000000,
    GBA_BASE_IWRAM        = 0x03000000,
    GBA_BASE_IO           = 0x04000000,
    GBA_BASE_PALETTE      = 0x05000000,
    GBA_BASE_VRAM         = 0x06000000,
    GBA_BASE_OAM          = 0x07000000,
    GBA_BASE_ROM0         = 0x08000000,
    GBA_BASE_ROM0_EX      = 0x09000000,
    GBA_BASE_ROM1         = 0x0A000000,
    GBA_BASE_ROM1_EX      = 0x0B000000,
    GBA_BASE_ROM2         = 0x0C000000,
    GBA_BASE_ROM2_EX      = 0x0D000000,
    GBA_BASE_SRAM         = 0x0E000000,
    GBA_BASE_SRAM_MIRROR  = 0x0F000000
};

/** @name Memory Region Sizes */
enum {
    GBA_SIZE_BIOS      = 0x00004000,  /**< 16 KiB */
    GBA_SIZE_EWRAM     = 0x00040000,  /**< 256 KiB */
    GBA_SIZE_IWRAM     = 0x00008000,  /**< 32 KiB */
    GBA_SIZE_IO        = 0x00000400,  /**< 1 KiB */
    GBA_SIZE_PALETTE   = 0x00000400,  /**< 1 KiB */
    GBA_SIZE_VRAM      = 0x00018000,  /**< 96 KiB */
    GBA_SIZE_OAM       = 0x00000400,  /**< 1 KiB */
    GBA_SIZE_ROM       = 0x02000000,  /**< 32 MiB max */
    GBA_SIZE_SRAM      = 0x00008000,  /**< 32 KiB */
    GBA_SIZE_FLASH512  = 0x00010000,  /**< 64 KiB */
    GBA_SIZE_FLASH1M   = 0x00020000,  /**< 128 KiB */
    GBA_SIZE_EEPROM    = 0x00002000,  /**< 8 KiB */
    GBA_SIZE_EEPROM512 = 0x00000200,  /**< 512 B */
    GBA_SIZE_AGB_PRINT = 0x00010000   /**< 64 KiB (AGB print buffer) */
};

/** @name Address Helpers */
enum {
    GBA_OFFSET_MASK = 0x00FFFFFF,
    GBA_BASE_OFFSET = 24
};

/** @name DMA Definitions */
#define GBA_DMA_COUNT 4

enum CupidDMAControl {
    GBA_DMA_INCREMENT        = 0,
    GBA_DMA_DECREMENT        = 1,
    GBA_DMA_FIXED            = 2,
    GBA_DMA_INCREMENT_RELOAD = 3
};

enum CupidDMATiming {
    GBA_DMA_TIMING_NOW     = 0,
    GBA_DMA_TIMING_VBLANK  = 1,
    GBA_DMA_TIMING_HBLANK  = 2,
    GBA_DMA_TIMING_CUSTOM  = 3  /**< Sound FIFO for DMA1/2, video capture for DMA3 */
};

/** Backwards-compatible aliases */
#define DMA_TIMING_NOW     GBA_DMA_TIMING_NOW
#define DMA_TIMING_VBLANK  GBA_DMA_TIMING_VBLANK
#define DMA_TIMING_HBLANK  GBA_DMA_TIMING_HBLANK
#define DMA_TIMING_CUSTOM  GBA_DMA_TIMING_CUSTOM

/**
 * DMA control register (CNT_HI) bit-field accessors.
 *   Bits  5-6 : destControl  (0=inc, 1=dec, 2=fixed, 3=inc/reload)
 *   Bits  7-8 : srcControl   (0=inc, 1=dec, 2=fixed)
 *   Bit   9   : repeat
 *   Bit  10   : width        (0=16-bit, 1=32-bit)
 *   Bit  11   : DRQ / game-pak
 *   Bits 12-13: timing       (0=now, 1=VBlank, 2=HBlank, 3=custom)
 *   Bit  14   : IRQ on end
 *   Bit  15   : enable
 */
#define DMA_REG_DEST_CONTROL(r)  (((r) >> 5) & 3)
#define DMA_REG_SRC_CONTROL(r)   (((r) >> 7) & 3)
#define DMA_REG_REPEAT(r)        (((r) >> 9) & 1)
#define DMA_REG_WIDTH(r)         (((r) >> 10) & 1)
#define DMA_REG_DRQ(r)           (((r) >> 11) & 1)
#define DMA_REG_TIMING(r)        (((r) >> 12) & 3)
#define DMA_REG_IRQ(r)           (((r) >> 14) & 1)
#define DMA_REG_ENABLE(r)        (((r) >> 15) & 1)
#define DMA_REG_CLEAR_ENABLE(r)  ((r) & 0x7FFF)
#define DMA_REG_SET_DEST(r, v)   (((r) & ~(3 << 5))  | (((v) & 3) << 5))
#define DMA_REG_SET_WIDTH(r, v)  (((r) & ~(1 << 10)) | (((v) & 1) << 10))

struct CupidDMA {
    struct CupidGBA* gba;
    unsigned id;
    uint64_t busyUntil;
    struct CupidTimingEvent requestEvent; /**< Pending activation request */
    uint16_t reg;            /**< Raw DMA control register (CNT_HI) */

    uint32_t source;         /**< Programmed SAD (masked on write) */
    uint32_t dest;           /**< Programmed DAD (masked on write) */
    int32_t  count;          /**< Programmed count (zero means maximum) */
    uint32_t nextSource;     /**< Current source during transfer */
    uint32_t nextDest;       /**< Current dest during transfer */
    int32_t  nextCount;      /**< Transfer units remaining in the latched burst */
    uint32_t latch;          /**< Open-bus: last value transferred */

    int32_t  sourceOffset;   /**< Bytes per step (±width, forced +width for ROM) */
    int32_t  destOffset;     /**< Bytes per step */

    /** Decoded control fields (written by cupid_dma_write_cnt) */
    int      srcControl;     /**< 0=inc, 1=dec, 2=fixed */
    int      destControl;    /**< 0=inc, 1=dec, 2=fixed, 3=inc/reload */
    int      timing;         /**< DMA_TIMING_* */
    bool     enable;
    bool     repeat;
    bool     wordTransfer;   /* 0=16-bit, 1=32-bit */
    bool     doIrq;
    bool     ready;
    bool     fifo;
    bool     transferWord;
    bool     externalSeen;
    uint32_t accessAddress;  /**< Address sampled by the current bus phase */
};

/**
 * @name Save Data
 */

enum CupidSaveType {
    SAVE_AUTODETECT = -1,
    SAVE_NONE       = 0,
    SAVE_SRAM,          /* 32 KiB, byte access */
    SAVE_SRAM512,       /* 64 KiB, byte access */
    SAVE_FLASH512,      /* 64 KiB, command state machine */
    SAVE_FLASH1M,       /* 128 KiB, command state machine + bank switch */
    SAVE_EEPROM,        /* 8 KiB, serial bit protocol */
    SAVE_EEPROM512      /* 512 B, serial bit protocol */
};

/* Flash command / EEPROM command enum */
enum CupidSavedataCommand {
    EEPROM_COMMAND_NULL         = 0,
    EEPROM_COMMAND_PENDING      = 1,
    EEPROM_COMMAND_WRITE        = 2,
    EEPROM_COMMAND_READ_PENDING = 3,
    EEPROM_COMMAND_READ         = 4,

    FLASH_COMMAND_START         = 0xAA,
    FLASH_COMMAND_CONTINUE      = 0x55,

    FLASH_COMMAND_ERASE_CHIP    = 0x10,
    FLASH_COMMAND_ERASE_SECTOR  = 0x30,

    FLASH_COMMAND_NONE          = 0,
    FLASH_COMMAND_ERASE         = 0x80,
    FLASH_COMMAND_ID            = 0x90,
    FLASH_COMMAND_PROGRAM       = 0xA0,
    FLASH_COMMAND_SWITCH_BANK   = 0xB0,
    FLASH_COMMAND_TERMINATE     = 0xF0
};

/* Flash state machine states */
enum CupidFlashState {
    FLASH_STATE_RAW      = 0,
    FLASH_STATE_START    = 1,
    FLASH_STATE_CONTINUE = 2
};

/* Flash chip identification */
enum CupidFlashId {
    FLASH_PANASONIC_512K = 0x1B32,
    FLASH_SANYO_1M       = 0x1362
};

/* Well-known flash addresses */
enum {
    FLASH_BASE_HI = 0x5555,
    FLASH_BASE_LO = 0x2AAA
};

struct CupidSavedata {
    enum CupidSaveType type;
    uint8_t* data;
    size_t   size;
    bool     dirty;
    bool     typeForced; /**< An explicit type override limits file-load expansion */

    /** Flash state machine */
    enum CupidFlashState flashState;
    enum CupidSavedataCommand command;
    uint8_t* currentBank;         /**< Points into data + (flashBank << 16) */
    int      flashBank;

    /** EEPROM serial protocol */
    int8_t   readBitsRemaining;
    uint32_t readAddress;
    uint32_t writeAddress;

    /** Self-timed programming, independent of the serial command decoder. */
    struct CupidTiming* timing;
    struct CupidTimingEvent readyEvent;
    uint64_t localClock, readyClock;
    uint32_t programOffset, programLength;
    uint32_t eepromProgramClocks, flashProgramClocks;
    uint32_t flashSectorEraseClocks, flashChipEraseClocks;
    uint8_t programData[8], eepromData[8];
    unsigned eepromWriteBits;
    bool programming, erasing, pollToggle;
};

/**
 * @name ROM Wait-State Lookup Tables
 *
 * Used by cupid_memory_update_waitstates() to decode the WAITCNT register.
 */
extern const char GBA_ROM_WAITSTATES[4];       /**< { 4, 3, 2, 8 } */
extern const char GBA_ROM_WAITSTATES_SEQ[6];   /**< { 2, 1, 4, 1, 8, 1 } */

/** @name GBA Memory */
struct CupidPrefetch {
    uint64_t epoch;           /**< Last clock accounted for by the fetch unit */
    uint32_t head, tail;      /**< Oldest cached and next in-flight addresses */
    uint32_t address;         /**< Current CPU fetch address */
    unsigned count, width;   /**< Buffered halfwords and current CPU fetch width */
    unsigned duty, remaining; /**< Full and remaining cartridge halfword clocks */
    unsigned fetchKind;
    bool active, busy;        /**< Engaged stream and occupied Game Pak bus */
    bool disabled;            /**< Pending burst restart after WAITCNT disable */
};

struct CupidMemory {
    struct CupidPrefetch prefetchUnit;
    /** Main memory buffers (uint8_t* for direct byte access) */
    uint8_t* bios;               /**< 16 KiB BIOS (loaded externally) */
    uint8_t* ewram;              /**< 256 KiB external work RAM */
    uint8_t* iwram;              /**< 32 KiB internal work RAM */
    uint16_t io[512];            /**< IO registers (1 KiB as uint16 array) */
    uint8_t* rom;                /**< Game Pak ROM */
    size_t   romSize;
    size_t   romImageSize;       /**< Original image bytes, before mapping padding */
    uint32_t romMask;
    uint16_t romID;              /**< Cartridge header ID */
    int      fullBios;           /**< Non-zero if a real BIOS is loaded */

    struct CupidSavedata savedata;
    struct CupidDMA dma[GBA_DMA_COUNT];
    struct CupidTimingEvent dmaEvent;   /**< Shared timing event for all channels */
    int  activeDMA;              /**< -1 when no DMA running */
    bool dmaRunning;
    unsigned dmaPhase;
    bool busBusy;
    bool busLocked;
    bool fetchNonseq;
    bool lastWasDMA;
    uint32_t dmaBus;
    uint32_t dmaROMAddress;  /**< Game Pak counter shared by DMA reads/writes */

    /**
     * Wait-state tables (indexed by region = address >> 24).
     */
    char waitstatesNonseq16[256];
    char waitstatesSeq16[256];
    char waitstatesNonseq32[256];
    char waitstatesSeq32[256];

    int      activeRegion;       /**< Current fetch region (-1 = none) */
    uint32_t biosPrefetch;       /**< BIOS read protection latch */
    bool     biosLoaded;

    /** Prefetch buffer state */
    bool     prefetch;           /**< WAITCNT bit 14: prefetch enable */
    uint32_t lastPrefetchedPc;   /**< Last PC that triggered a prefetch */

    /** Open bus value */
    uint32_t openBus;
};

/** @name API */
void cupid_memory_init(struct CupidGBA* gba);
void cupid_memory_reset(struct CupidGBA* gba);
void cupid_memory_deinit(struct CupidGBA* gba);

/** Bus operations (installed as function pointers on CPU) */
uint32_t cupid_memory_load32(struct CupidARM* cpu, uint32_t addr, int* cyc);
/** Value driven by the existing CPU/DMA bus model on an unmapped read. */
uint32_t cupid_memory_open_bus(struct CupidARM* cpu);
uint32_t cupid_memory_load16(struct CupidARM* cpu, uint32_t addr, int* cyc);
uint32_t cupid_memory_loadU16(struct CupidARM* cpu, uint32_t addr, int* cyc);
uint32_t cupid_memory_load8(struct CupidARM* cpu, uint32_t addr, int* cyc);
void     cupid_memory_store32(struct CupidARM* cpu, uint32_t addr, int32_t val, int* cyc);
void     cupid_memory_store16(struct CupidARM* cpu, uint32_t addr, int16_t val, int* cyc);
void     cupid_memory_store8(struct CupidARM* cpu, uint32_t addr, int8_t val, int* cyc);
void     cupid_memory_set_active_region(struct CupidARM* cpu, uint32_t addr);
bool     cupid_memory_is_eeprom_address(const struct CupidMemory* memory, uint32_t address);

/** Block transfer (LDM/STM) */
uint32_t cupid_memory_load_multiple(struct CupidARM* cpu, uint32_t addr, int mask,
                                    enum CupidLSMDirection dir, int* cyc);
uint32_t cupid_memory_store_multiple(struct CupidARM* cpu, uint32_t addr, int mask,
                                     enum CupidLSMDirection dir, int* cyc);

/** Memory stall callback */
int32_t cupid_memory_stall(struct CupidARM* cpu, int32_t wait);

/** @name DMA API */
void cupid_dma_init(struct CupidGBA* gba);
void cupid_dma_reset(struct CupidGBA* gba);
void cupid_dma_write_sad(struct CupidGBA* gba, int ch, uint32_t address);
void cupid_dma_write_dad(struct CupidGBA* gba, int ch, uint32_t address);
void cupid_dma_write_cnt_lo(struct CupidGBA* gba, int ch, uint16_t count);
void cupid_dma_write_cnt(struct CupidGBA* gba, int ch, uint16_t value);
void cupid_dma_run_hblank(struct CupidGBA* gba, int32_t cycles);
void cupid_dma_run_vblank(struct CupidGBA* gba, int32_t cycles);
void cupid_dma_transfer(struct CupidGBA* gba, int ch);
void cupid_dma_request_fifo(struct CupidGBA* gba, int ch, uint64_t timestamp);
void cupid_dma_kick(struct CupidGBA* gba, uint64_t timestamp);
void cupid_dma_run_display(struct CupidGBA* gba);
void cupid_bus_fetch(struct CupidARM* cpu, uint32_t address, unsigned width, bool sequential);
void cupid_bus_finish(struct CupidARM* cpu);
void cupid_bus_wait(struct CupidARM* cpu);
void cupid_bus_advance(struct CupidARM* cpu, unsigned cycles, bool occupied);
void cupid_bus_release(struct CupidARM* cpu);
void cupid_bus_lock(struct CupidARM* cpu, bool locked);
void cupid_prefetch_update(struct CupidGBA* gba, uint64_t timestamp);
unsigned cupid_prefetch_fetch(struct CupidGBA* gba, uint32_t address, unsigned width,
                              unsigned cycles);
void cupid_prefetch_finish_fetch(struct CupidGBA* gba);
unsigned cupid_prefetch_before_data(struct CupidGBA* gba, uint32_t address);
void cupid_prefetch_after_data(struct CupidGBA* gba);

/** @name Waitstate Control */
void cupid_memory_update_waitstates(struct CupidGBA* gba, uint16_t waitcnt);
void cupid_memory_update_ewram_waitstates(struct CupidGBA* gba, uint16_t parameters);

/** @name ROM Loading */
bool cupid_load_rom(struct CupidGBA* gba, const char* path);
bool cupid_load_bios(struct CupidGBA* gba, const char* path);

/** @name Savedata API */
void cupid_savedata_init(struct CupidSavedata* savedata);
void cupid_savedata_reset(struct CupidSavedata* savedata);
void cupid_savedata_deinit(struct CupidSavedata* savedata);
void cupid_savedata_force_type(struct CupidSavedata* savedata, enum CupidSaveType type);
void cupid_savedata_init_sram(struct CupidSavedata* savedata);
void cupid_savedata_init_sram512(struct CupidSavedata* savedata);
void cupid_savedata_init_flash(struct CupidSavedata* savedata);
void cupid_savedata_init_eeprom(struct CupidSavedata* savedata);
uint8_t  cupid_savedata_read_flash(struct CupidSavedata* savedata, uint16_t address);
void     cupid_savedata_write_flash(struct CupidSavedata* savedata, uint16_t address, uint8_t value);
uint16_t cupid_savedata_read_eeprom(struct CupidSavedata* savedata);
void     cupid_savedata_write_eeprom(struct CupidSavedata* savedata, uint16_t value, uint32_t writeSize);

void cupid_savedata_programming_init(struct CupidSavedata* savedata);
void cupid_savedata_bind_timing(struct CupidSavedata* savedata, struct CupidTiming* timing);
void cupid_savedata_cancel_programming(struct CupidSavedata* savedata);
void cupid_savedata_begin_programming(struct CupidSavedata* savedata, uint32_t offset,
                                     uint32_t length, const uint8_t* data, uint32_t clocks);
void cupid_savedata_advance_clock(struct CupidSavedata* savedata, uint64_t clocks);
bool cupid_savedata_set_programming_clocks(struct CupidSavedata* savedata,
                                         uint32_t eeprom, uint32_t byte,
                                         uint32_t sector, uint32_t chip);
void cupid_savedata_detect_type(struct CupidSavedata* savedata, const uint8_t* rom, size_t romSize);

/**
 * Load save data from a .sav file into savedata->data.
 * Recognized compatible file capacities can expand an inferred type. Explicit
 * overrides retain their capacity. A failed load leaves active data unchanged;
 * a missing file returns false with errno=ENOENT.
 */
bool cupid_savedata_load_file(struct CupidSavedata* savedata, const char* path);

/**
 * Flush savedata->data to a .sav file.  Only writes when dirty.
 * Replaces the destination only after a complete staged write. Failures retain
 * dirty state. Returns true on success (or when nothing needed to be written).
 */
bool cupid_savedata_save_file(struct CupidSavedata* savedata, const char* path);

#endif /* CUPID_MEMORY_H */
