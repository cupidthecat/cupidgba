/* SPDX-License-Identifier: GPL-3.0-or-later */
#define main cupid_bounded_runner_main
#include "../rom_runner.c"
#undef main
#include "../test.h"

static const uint64_t epochs[] = {0,UINT64_C(0x7FFFFFFE),UINT64_C(0xFFFFFFFE),UINT64_C(0x2FFFFFFFE)};
static const enum CupidPrivMode modes[] = {MODE_USR,MODE_FIQ,MODE_IRQ,MODE_SVC,MODE_ABT,MODE_UND,MODE_SYS};
enum { TERMINAL = GBA_BASE_ROM0 + 0x100 };
struct Fixture { struct RunState run; struct RegisterObservations observations; unsigned issues; };
static void record(const struct CupidARM* cpu,void* context) {
    struct Fixture* fixture=context;++fixture->issues;
    observe_instruction(cpu,&fixture->observations);
}
static struct CupidGBA* machine(void) {
    struct CupidGBA* gba=calloc(1,sizeof(*gba));CHECK(gba);cupid_gba_create(gba);
    gba->memory.rom=calloc(1,GBA_SIZE_ROM);CHECK(gba->memory.rom);
    gba->memory.romSize=GBA_SIZE_ROM;gba->memory.romMask=GBA_SIZE_ROM-1;return gba;
}
static void reset(struct CupidGBA* gba,struct Fixture* fixture,uint64_t epoch,unsigned a1,
                  enum CupidPrivMode mode,unsigned flags,unsigned masks,unsigned value) {
    gba->cpu.instructionObserver=NULL;gba->cpu.instructionObserverContext=NULL;
    cupid_gba_reset(gba);cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles=epoch;gba->timing.masterCycles=(uint32_t)epoch;
    cupid_video_reset(&gba->video);cupid_audio_reset(&gba->audio);
    gba->audio.sampleOrigin=gba->audio.lastSample=epoch;gba->audio.nextSample=epoch+512;
    cupid_irq_init(gba);cupid_arm_set_priv_mode(&gba->cpu,mode);
    gba->cpu.cpsr.packed=(int32_t)((flags<<28)|(masks<<6)|mode);
    gba->cpu.irqDisableLatch=(masks&2)!=0;gba->cpu.gprs[12]=(int32_t)value;
    const uint32_t code[]={0xEAFFFFFE,0xE1A00000,0xE1A00000};
    memcpy(gba->memory.rom+0x100,code,sizeof(code));
    /* Owned BIOS-vector NOP: arbitration must discard the cartridge opcode. */
    const uint32_t vector[]={0xE1A01001,0xEAFFFFFE};memcpy(gba->memory.bios+0x18,vector,sizeof(vector));
    _CupidSetMode(&gba->cpu,MODE_ARM);gba->cpu.gprs[ARM_PC]=TERMINAL+(int32_t)a1;CupidARMWritePC(&gba->cpu);
    memset(fixture,0,sizeof(*fixture));
    fixture->run=(struct RunState){.frames=10,.cycleLimit=epoch+10*UINT64_C(280896),.stepLimit=1000,
        .terminalAddress=TERMINAL,.checkpointAddress=TERMINAL,.expectedRegister=12};
    fixture->observations.run=&fixture->run;
    gba->cpu.instructionObserver=record;gba->cpu.instructionObserverContext=fixture;
}
static void destroy(struct CupidGBA* gba){cupid_gba_destroy(gba);free(gba);}

static void issued(void) {
    struct CupidGBA* gba=machine();struct Fixture fixture;unsigned count=0;
    for(unsigned e=0;e<4;e++)for(unsigned a1=0;a1<4;a1+=2)for(unsigned m=0;m<7;m++)
        for(unsigned flags=0;flags<16;flags++)for(unsigned masks=0;masks<4;masks++)for(unsigned bad=0;bad<2;bad++){
            unsigned value=bad ? 9u : 0u;reset(gba,&fixture,epochs[e],a1,modes[m],flags,masks,value);
            CHECK(run_boundary(gba,0,&fixture.run));
            CHECK(!fixture.run.checkpoint && !fixture.run.terminalIssued && !fixture.run.terminal);
            uint64_t before=cupid_timing_global_time(&gba->timing);
            CHECK(cupid_gba_run_steps(gba,1000,run_boundary,&fixture.run)==1);
            CHECK(fixture.issues==1 && fixture.run.steps==1 && fixture.run.done);
            CHECK(fixture.run.terminalIssued && fixture.run.terminal && fixture.run.checkpoint);
            CHECK(fixture.run.checkpointValue==value && (uint32_t)gba->cpu.gprs[12]==value);
            CHECK(fixture.run.terminalIssuedAddress==TERMINAL && fixture.run.terminalLogicalPC==TERMINAL+a1+4);
            CHECK(!gba->cpuInstructionPending && (uint32_t)gba->cpu.gprs[ARM_PC]==TERMINAL+a1+4);
            CHECK(gba->cpu.prefetch[0]==0xEAFFFFFE && gba->cpu.privilegeMode==modes[m]);
            CHECK((uint32_t)gba->cpu.cpsr.packed==((flags<<28)|(masks<<6)|modes[m]));
            CHECK(cupid_timing_global_time(&gba->timing)>before);++count;
        }
    CHECK(count==7168);
    /* An issued branch with a corrupted completed pipeline cannot pass. This
     * is an owned negative control of the host gate, not a cartridge edit. */
    reset(gba,&fixture,0,0,MODE_SYS,0,3,0);cupid_gba_step(gba);CHECK(fixture.run.terminalIssued);
    gba->cpu.prefetch[0]=0xE1A00000;
    CHECK(!run_boundary(gba,1,&fixture.run) && fixture.run.done && !fixture.run.terminal);
    printf("PASS %u completed terminal scenes and corrupted-return control\n",count);destroy(gba);
}
static void arbitration(void) {
    struct CupidGBA* gba=machine();struct Fixture fixture;unsigned count=0;
    for(unsigned e=0;e<4;e++)for(unsigned a1=0;a1<4;a1+=2){
        reset(gba,&fixture,epochs[e],a1,MODE_SYS,0,0,0);
        gba->irq.line=true;CHECK(run_boundary(gba,0,&fixture.run));cupid_gba_step(gba);
        CHECK(fixture.issues==1 && gba->cpu.privilegeMode==MODE_IRQ);
        CHECK(run_boundary(gba,1,&fixture.run));
        CHECK(!fixture.run.checkpoint && !fixture.run.terminalIssued && !fixture.run.terminal);++count;
        reset(gba,&fixture,epochs[e],a1,MODE_SYS,0,3,0);cupid_gba_halt(gba);
        for(unsigned i=0;i<4;i++){
            CHECK(run_boundary(gba,0,&fixture.run));cupid_gba_step(gba);CHECK(run_boundary(gba,1,&fixture.run));
            CHECK(!fixture.issues && !fixture.run.checkpoint && !fixture.run.terminalIssued && !fixture.run.terminal);
        }
        ++count;
        reset(gba,&fixture,epochs[e],a1,MODE_SYS,0,3,0);cupid_gba_stop(gba);
        CHECK(gba->stopped);CHECK(!run_boundary(gba,0,&fixture.run));cupid_gba_step(gba);
        CHECK(!fixture.issues && !fixture.run.checkpoint && !fixture.run.terminalIssued && !fixture.run.terminal);++count;
    }
    CHECK(count==24);printf("PASS %u IRQ-discard/HALT/STOP terminal controls\n",count);destroy(gba);
}
static bool pause_pending(struct CupidGBA* gba,uint64_t steps,void* context) {
    struct Fixture* fixture=context;
    bool proceed=run_boundary(gba,steps,&fixture->run);
    return proceed && !(steps && gba->cpuInstructionPending);
}
static void suspended(void) {
    struct CupidGBA* gba=machine();struct Fixture fixture;unsigned count=0;
    for(unsigned e=0;e<4;e++)for(unsigned a1=0;a1<4;a1+=2){
        reset(gba,&fixture,epochs[e],a1,MODE_SYS,0,3,0);
        /* Supply the BIOS-only HALTCNT strobe in this owned bus fixture. The
         * resulting wait suspends the branch after issue, before completion. */
        gba->memory.activeRegion=GBA_REGION_BIOS;gba->cpu.memory.timed=true;
        cupid_io_write8(gba,REG_HALTCNT,0);gba->cpu.memory.timed=false;
        CHECK(cupid_gba_run_steps(gba,1000,pause_pending,&fixture)>0);
        CHECK(gba->cpuInstructionPending && gba->cpu.halted && fixture.issues==1);
        CHECK(fixture.run.terminalIssued && fixture.run.checkpoint && !fixture.run.terminal && !fixture.run.done);
        cupid_io_write(gba,REG_IE,IRQ_KEYPAD);cupid_raise_irq(gba,IRQ_KEYPAD,0);
        cupid_gba_run_steps(gba,fixture.run.stepLimit-fixture.run.steps,run_boundary,&fixture.run);
        CHECK(!gba->cpuInstructionPending && !gba->cpu.halted && fixture.issues==1);
        CHECK(fixture.run.done && fixture.run.terminalIssued && fixture.run.terminal);
        CHECK((uint32_t)gba->cpu.gprs[ARM_PC]==TERMINAL+a1+4 && gba->cpu.prefetch[0]==0xEAFFFFFE);++count;
    }
    CHECK(count==8);printf("PASS %u suspended/resumed terminal branches without fabricated reissue\n",count);destroy(gba);
}
int main(int argc,char** argv) {
    CHECK(argc==2);
    if(!strcmp(argv[1],"issued"))issued();else if(!strcmp(argv[1],"arbitration"))arbitration();
    else if(!strcmp(argv[1],"suspended"))suspended();else CHECK(false);
    return 0;
}
