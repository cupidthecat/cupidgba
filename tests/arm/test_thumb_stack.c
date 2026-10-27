/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static const char* externalBios;
static const uint32_t windows[] = {0x02000000,0x03000000,0x08000000,0x09000000,
                                  0x0A000000,0x0B000000,0x0C000000,0x0D000000};
static const uint64_t epochs[] = {0x1000,UINT64_C(0x7FFFFFFE),UINT64_C(0xFFFFFFFE),UINT64_C(0x2FFFFFFFE)};
static const enum CupidPrivMode modes[] = {MODE_USR,MODE_FIQ,MODE_IRQ,MODE_SVC,MODE_ABT,MODE_UND,MODE_SYS};
struct Issue { unsigned count; uint32_t caller, target, opcode, marker; };
static void observe(const struct CupidARM* cpu,void* context) {
    struct Issue* issue=context;
    CHECK(cupid_arm_instruction_address(cpu)==(issue->count ? issue->target : issue->caller));
    CHECK(cpu->executionMode==MODE_THUMB);
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

/* Owned streams exercise POP independently of firmware boot. The implicit
 * PC transfer has the same bus work as POP {PC}, with a sixteen-word SP step. */
static uint64_t scene(struct CupidGBA* gba,unsigned mask,bool pc,unsigned low,unsigned spLow,
                      enum CupidPrivMode mode,unsigned flags,unsigned masks,unsigned source,
                      unsigned destination,unsigned stack,uint16_t waits,uint64_t epoch,bool continued) {
    cupid_gba_reset(gba);cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles=epoch;gba->timing.masterCycles=(uint32_t)epoch;
    struct CupidARM* cpu=&gba->cpu;cupid_arm_set_priv_mode(cpu,mode);
    for(unsigned r=0;r<15;r++)cpu->gprs[r]=(int32_t)(0x11220000u+r);
    uint32_t status=(flags<<28)|(masks<<6)|mode|0x20u;
    cpu->cpsr.packed=(int32_t)status;cpu->spsr.packed=0x6500003F;
    cpu->irqDisableLatch=(masks&2)!=0;_CupidSetMode(cpu,MODE_THUMB);
    const uint32_t caller=windows[source]+0x100;
    const uint32_t operand=windows[destination]+0x400+low,target=operand&~1u;
    const uint32_t sp=windows[stack]+0x800+spLow;
    cpu->gprs[ARM_SP]=(int32_t)sp;
    const uint16_t opcode=(uint16_t)(0xBC00u|(pc ? 0x100u : 0u)|mask);
    const uint16_t code[]={opcode,0x275A,0x46C0,0x46C0};memcpy(bytes(gba,caller),code,sizeof(code));
    const uint16_t body[]={0x275A,0x46C0};memcpy(bytes(gba,target),body,sizeof(body));
    bool branches=pc || !mask;unsigned count=(unsigned)__builtin_popcount(mask)+(pc ? 1u : 0u);
    uint32_t stackWords[12];for(unsigned i=0;i<12;i++)stackWords[i]=0xAABB0000u+i;
    unsigned word=0;for(unsigned r=0;r<8;r++)if(mask&(1u<<r))stackWords[word++]=0x55660000u+r;
    if(branches)stackWords[word]=operand;
    memcpy(bytes(gba,sp&~3u),stackWords,sizeof(stackWords));
    cupid_memory_update_waitstates(gba,waits);
    cpu->gprs[ARM_PC]=(int32_t)caller;CupidThumbWritePC(cpu);
    int32_t saved[15],banks[6][7],savedStatus[6];memcpy(saved,cpu->gprs,sizeof(saved));
    memcpy(banks,cpu->bankedRegisters,sizeof(banks));memcpy(savedStatus,cpu->bankedSPSRs,sizeof(savedStatus));
    struct Issue issue={0,caller,branches ? target : caller+2,opcode,0x275A};
    cpu->instructionObserver=observe;cpu->instructionObserverContext=&issue;
    uint64_t before=cupid_timing_global_time(&gba->timing);cupid_gba_step(gba);
    CHECK(issue.count==1 && cpu->executionMode==MODE_THUMB && cpu->privilegeMode==mode);
    CHECK((uint32_t)cpu->cpsr.packed==status && (uint32_t)cpu->spsr.packed==0x6500003F);
    CHECK(cpu->irqDisableLatch==((masks&2)!=0));
    CHECK((uint32_t)cpu->gprs[ARM_SP]==sp+(!mask && !pc ? 64u : count*4u));
    CHECK((uint32_t)cpu->gprs[ARM_PC]==issue.target+2);
    CHECK(cpu->prefetch[0]==0x275A && cpu->prefetch[1]==0x46C0);
    for(unsigned r=0;r<15;r++)if(r!=ARM_SP)
        CHECK((uint32_t)cpu->gprs[r]==(r<8 && (mask&(1u<<r)) ? 0x55660000u+r : (uint32_t)saved[r]));
    CHECK(!memcmp(banks,cpu->bankedRegisters,sizeof(banks)) && !memcmp(savedStatus,cpu->bankedSPSRs,sizeof(savedStatus)));
    CHECK(!memcmp(bytes(gba,sp&~3u),stackWords,sizeof(stackWords)));
    uint64_t elapsed=cupid_timing_global_time(&gba->timing)-before;
    if(source==1 && destination==1 && stack==1 && branches && !mask)CHECK(elapsed==5);
    if(continued){
        cupid_gba_step(gba);CHECK(issue.count==2 && cpu->gprs[7]==0x5A);
        CHECK(cpu->executionMode==MODE_THUMB && cpu->privilegeMode==mode);
        CHECK((uint32_t)cpu->cpsr.packed==(status&~0xC0000000u));
        CHECK(!memcmp(bytes(gba,sp&~3u),stackWords,sizeof(stackWords)));
    }
    cpu->instructionObserver=NULL;cpu->instructionObserverContext=NULL;return elapsed;
}
static void pair(struct CupidGBA* gba,unsigned low,unsigned spLow,enum CupidPrivMode mode,
                 unsigned flags,unsigned masks,unsigned source,unsigned target,unsigned stack,
                 uint16_t waits,uint64_t epoch,bool continued) {
    uint64_t normal=scene(gba,0,true,low,spLow,mode,flags,masks,source,target,stack,waits,epoch,continued);
    CHECK(scene(gba,0,false,low,spLow,mode,flags,masks,source,target,stack,waits,epoch,continued)==normal);
}
static void banks(void) {
    struct CupidGBA* gba=machine();unsigned count=0;
    for(unsigned m=0;m<7;m++)for(unsigned flags=0;flags<16;flags++)for(unsigned masks=0;masks<4;masks++)
        for(unsigned sp=0;sp<4;sp++)for(unsigned low=0;low<4;low++){
            pair(gba,low,sp,modes[m],flags,masks,1,1,1,0,epochs[3],false);count++;
        }
    CHECK(count==7168);printf("PASS empty POP banks/flags/masks/alignment: %u pairs\n",count);destroy(gba);
}
static void buses(void) {
    struct CupidGBA* gba=machine();unsigned count=0;
    for(unsigned source=0;source<8;source++)for(unsigned target=0;target<8;target++)for(unsigned stack=0;stack<2;stack++)
        for(unsigned low=0;low<4;low++)for(unsigned n=0;n<4;n++)for(unsigned s=0;s<2;s++)for(unsigned p=0;p<2;p++){
            uint16_t waits=(uint16_t)((n<<2)|(n<<5)|(n<<8)|(s<<4)|(s<<7)|(s<<10)|(p<<14));
            pair(gba,low,(n+s)&3,MODE_SYS,10,3,source,target,stack,waits,epochs[(n+p)&3],false);count++;
        }
    CHECK(count==8192);printf("PASS empty POP caller/stack/target buses and waits: %u pairs\n",count);destroy(gba);
}
static void continued(void) {
    struct CupidGBA* gba=machine();unsigned count=0;
    for(unsigned source=0;source<8;source++)for(unsigned target=0;target<8;target++)for(unsigned low=0;low<4;low++){
        pair(gba,low,low,MODE_SYS,15,3,source,target,low&1,0x4317,epochs[2],true);count++;
    }
    CHECK(count==256);printf("PASS empty POP issued target: %u pairs\n",count);destroy(gba);
}
static void lists(void) {
    struct CupidGBA* gba=machine();unsigned count=0;
    for(unsigned mask=0;mask<256;mask++)for(unsigned pc=0;pc<2;pc++)for(unsigned sp=0;sp<4;sp++){
        if(!mask && !pc)continue;
        scene(gba,mask,pc!=0,sp,sp,MODE_SYS,10,3,1,1,sp&1,0,epochs[sp],true);count++;
    }
    CHECK(count==2044);printf("PASS normal POP lists and explicit PC: %u scenes\n",count);destroy(gba);
}
int main(int argc,char** argv) {
    CHECK(argc==2 || (argc==4 && !strcmp(argv[2],"--bios")));if(argc==4)externalBios=argv[3];
    if(!strcmp(argv[1],"banks"))banks();else if(!strcmp(argv[1],"buses"))buses();
    else if(!strcmp(argv[1],"continued"))continued();else if(!strcmp(argv[1],"lists"))lists();else CHECK(false);
    return 0;
}
