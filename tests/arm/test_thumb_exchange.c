/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static const char* externalBios;
static const uint32_t windows[] = {0x02000000,0x03000000,0x08000000,0x09000000,
                                  0x0A000000,0x0B000000,0x0C000000,0x0D000000};
static const uint64_t epochs[] = {0x1000,UINT64_C(0x7FFFFFFE),UINT64_C(0xFFFFFFFE),UINT64_C(0x2FFFFFFFE)};
static const enum CupidPrivMode modes[] = {MODE_USR,MODE_FIQ,MODE_IRQ,MODE_SVC,MODE_ABT,MODE_UND,MODE_SYS};
struct Issue { unsigned count; uint32_t source, opcode, target, marker; bool thumb; };
static void observe(const struct CupidARM* cpu, void* context) {
    struct Issue* issue=context;
    uint32_t expected=issue->count ? issue->target : issue->source;
    CHECK(cupid_arm_instruction_address(cpu)==expected);
    CHECK(cpu->executionMode==(issue->count ? (issue->thumb ? MODE_THUMB : MODE_ARM) : MODE_THUMB));
    CHECK(cpu->prefetch[0]==(issue->count ? issue->marker : issue->opcode));
    CHECK(++issue->count<=2);
}
static uint8_t* bytes(struct CupidGBA* gba,uint32_t address) {
    if(address>=GBA_BASE_ROM0)return gba->memory.rom+(address&(GBA_SIZE_ROM-1));
    if(address>=GBA_BASE_IWRAM)return gba->memory.iwram+(address&(GBA_SIZE_IWRAM-1));
    return gba->memory.ewram+(address&(GBA_SIZE_EWRAM-1));
}
static struct CupidGBA* machine(void) {
    struct CupidGBA* gba=calloc(1,sizeof(*gba));CHECK(gba);cupid_gba_create(gba);
    if(externalBios)CHECK(cupid_load_bios(gba,externalBios));
    gba->memory.rom=calloc(1,GBA_SIZE_ROM);CHECK(gba->memory.rom);
    gba->memory.romSize=GBA_SIZE_ROM;gba->memory.romMask=GBA_SIZE_ROM-1;
    return gba;
}
static void destroy(struct CupidGBA* gba){cupid_gba_destroy(gba);free(gba);}

/* Install owned caller/target streams; do not run firmware boot or replace
 * any original cartridge. The PC source uses the decoded instruction's PC. */
static uint64_t scene(struct CupidGBA* gba,unsigned encoding,unsigned low,
                      enum CupidPrivMode mode,unsigned flags,unsigned masks,
                      unsigned source,unsigned destination,uint16_t waits,
                      uint64_t epoch,bool continued) {
    cupid_gba_reset(gba);cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles=epoch;gba->timing.masterCycles=(uint32_t)epoch;
    struct CupidARM* cpu=&gba->cpu;
    cupid_arm_set_priv_mode(cpu,mode);
    for(unsigned r=0;r<15;r++)cpu->gprs[r]=(int32_t)(0x11220000u+r);
    cpu->spsr.packed=0x6500003F;
    const uint32_t status=(flags<<28)|(masks<<6)|mode|0x20u;
    cpu->cpsr.packed=(int32_t)status;cpu->irqDisableLatch=(masks&2)!=0;
    _CupidSetMode(cpu,MODE_THUMB);
    uint32_t caller=windows[source]+0x100+(low&2);
    unsigned rm=(encoding>>3)&15;
    uint32_t operand=rm==ARM_PC ? caller+4 : windows[destination]+0x400+low;
    bool thumb=(operand&1)!=0;
    uint32_t target=operand&~1u;
    uint32_t physical=target&~(thumb ? 1u : 3u);
    if(rm!=ARM_PC)cpu->gprs[rm]=(int32_t)operand;
    const uint16_t opcode=(uint16_t)(0x4700u|encoding);
    const uint16_t code[]={opcode,0x46C0,0x46C0,0x46C0};
    memcpy(bytes(gba,caller),code,sizeof(code));
    uint32_t marker=thumb ? 0x275A : 0xE3A0705A;
    if(thumb){const uint16_t body[]={0x275A,0x46C0};memcpy(bytes(gba,physical),body,sizeof(body));}
    else{const uint32_t body[]={0xE3A0705A,0xE1A00000};memcpy(bytes(gba,physical),body,sizeof(body));}
    cupid_memory_update_waitstates(gba,waits);
    cpu->gprs[ARM_PC]=(int32_t)caller;CupidThumbWritePC(cpu);
    int32_t saved[15],banks[6][7],savedStatus[6];
    memcpy(saved,cpu->gprs,sizeof(saved));memcpy(banks,cpu->bankedRegisters,sizeof(banks));
    memcpy(savedStatus,cpu->bankedSPSRs,sizeof(savedStatus));
    struct Issue issue={0,caller,opcode,physical,marker,thumb};
    cpu->instructionObserver=observe;cpu->instructionObserverContext=&issue;
    uint64_t before=cupid_timing_global_time(&gba->timing);cupid_gba_step(gba);
    if(cpu->privilegeMode!=mode)fprintf(stderr,"opcode=%04X rm=%u low=%u\n",opcode,rm,low);
    CHECK(issue.count==1 && cpu->privilegeMode==mode);
    CHECK(cpu->executionMode==(thumb ? MODE_THUMB : MODE_ARM));
    CHECK((uint32_t)cpu->cpsr.packed==((status&~0x20u)|(thumb ? 0x20u : 0u)));
    CHECK((uint32_t)cpu->spsr.packed==0x6500003F && cpu->irqDisableLatch==((masks&2)!=0));
    CHECK((uint32_t)cpu->gprs[ARM_PC]==target+(thumb ? 2u : 4u));
    CHECK(cpu->prefetch[0]==marker && cpu->prefetch[1]==(thumb ? 0x46C0u : 0xE1A00000u));
    CHECK(!memcmp(saved,cpu->gprs,sizeof(saved)));
    CHECK(!memcmp(banks,cpu->bankedRegisters,sizeof(banks)));
    CHECK(!memcmp(savedStatus,cpu->bankedSPSRs,sizeof(savedStatus)));
    uint64_t elapsed=cupid_timing_global_time(&gba->timing)-before;
    CHECK(cupid_timing_global_time(&gba->timing)==epoch+elapsed);
    if(source==1 && (destination==1 || rm==ARM_PC))CHECK(elapsed==3);
    if(continued){
        cupid_gba_step(gba);CHECK(issue.count==2 && cpu->gprs[7]==0x5A);
        CHECK(cpu->privilegeMode==mode && cpu->executionMode==(thumb ? MODE_THUMB : MODE_ARM));
        for(unsigned r=0;r<15;r++)if(r!=7)CHECK(cpu->gprs[r]==saved[r]);
        /* The Thumb MOV-immediate marker clears N/Z for its positive result;
         * the ARM MOV without S retains all caller flags. */
        uint32_t after=(status&~0x20u)|(thumb ? 0x20u : 0u);
        if(thumb)after&=~0xC0000000u;
        CHECK((uint32_t)cpu->cpsr.packed==after);
    }
    cpu->instructionObserver=NULL;cpu->instructionObserverContext=NULL;
    return elapsed;
}
static void pair(struct CupidGBA* gba,unsigned encoding,unsigned low,enum CupidPrivMode mode,
                 unsigned flags,unsigned masks,unsigned source,unsigned target,uint16_t waits,
                 uint64_t epoch,bool continued) {
    uint64_t normal=scene(gba,encoding&~0x80u,low,mode,flags,masks,source,target,waits,epoch,continued);
    CHECK(scene(gba,encoding,low,mode,flags,masks,source,target,waits,epoch,continued)==normal);
}
static void encodings(void) {
    struct CupidGBA* gba=machine();unsigned count=0;
    for(unsigned code=0;code<256;code++)for(unsigned low=0;low<4;low++)for(unsigned e=0;e<4;e++){
        pair(gba,code,low,MODE_SYS,(code^e)&15,e,1,1,0,epochs[e],false);count++;
    }
    CHECK(count==4096);printf("PASS all 256 encodings: %u canonical/candidate pairs\n",count);destroy(gba);
}
static void banks(void) {
    const unsigned registers[]={0,8,14,15};struct CupidGBA* gba=machine();unsigned count=0;
    for(unsigned m=0;m<7;m++)for(unsigned flags=0;flags<16;flags++)for(unsigned masks=0;masks<4;masks++){
        for(unsigned r=0;r<4;r++)for(unsigned low=0;low<4;low++){
            pair(gba,0x80|(registers[r]<<3)|7,low,modes[m],flags,masks,1,1,0,epochs[3],false);count++;
        }
    }
    CHECK(count==7168);printf("PASS caller banks/flags/masks: %u pairs\n",count);destroy(gba);
}
static void buses(void) {
    struct CupidGBA* gba=machine();unsigned count=0;
    for(unsigned source=0;source<8;source++)for(unsigned target=0;target<8;target++)for(unsigned low=0;low<4;low++){
        for(unsigned n=0;n<4;n++)for(unsigned s=0;s<2;s++)for(unsigned p=0;p<2;p++)for(unsigned e=0;e<4;e++){
            uint16_t waits=(uint16_t)((n<<2)|(n<<5)|(n<<8)|(s<<4)|(s<<7)|(s<<10)|(p<<14));
            pair(gba,0xA7,low,MODE_SYS,10,3,source,target,waits,epochs[e],false);count++;
        }
    }
    CHECK(count==16384);printf("PASS caller/target buses and all waits/prefetch: %u pairs\n",count);destroy(gba);
}
static void continued(void) {
    struct CupidGBA* gba=machine();unsigned count=0;
    for(unsigned source=0;source<8;source++)for(unsigned rm=0;rm<16;rm++)for(unsigned low=0;low<4;low++){
        pair(gba,0x80|(rm<<3)|5,low,MODE_SYS,10,3,source,1,0x4317,epochs[2],true);count++;
    }
    CHECK(count==512);printf("PASS first target instruction for all source registers: %u pairs\n",count);destroy(gba);
}
int main(int argc,char** argv) {
    CHECK(argc==2 || (argc==4 && !strcmp(argv[2],"--bios")));
    if(argc==4)externalBios=argv[3];
    if(!strcmp(argv[1],"encodings"))encodings();else if(!strcmp(argv[1],"banks"))banks();
    else if(!strcmp(argv[1],"buses"))buses();else if(!strcmp(argv[1],"continued"))continued();else CHECK(false);
    return 0;
}
