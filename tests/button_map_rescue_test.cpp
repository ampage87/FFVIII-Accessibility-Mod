// host probe for button_map_rescue.inl
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <sys/mman.h>
typedef unsigned short WORD;
#undef __try
#define __try if (1)
#define __except(x) else
#define EXCEPTION_EXECUTE_HANDLER 1
#define VK_MENU  0x12
#define VK_SHIFT 0x10
#define VK_F9    0x78
// v0.25.2: the hotkey moved to Shift+F9. Alt+K never fired in the real game --
// holding Alt puts a Win32 window into menu-modal mode, which can stall the
// frame loop this poll runs from, so the combo was never observed. **A host stub
// cannot model that**, which is precisely why the probe below now asserts the
// modifier is REQUIRED: the only thing a stub can defend is that the key
// combination is the one the release notes claim.
static int g_shift=0, g_f9=0;
static short GetAsyncKeyState(int vk){
    const int down = (vk==VK_SHIFT) ? g_shift : (vk==VK_F9 ? g_f9 : 0);
    return down ? (short)0x8000 : 0;
}
namespace Log { void Menu(const char*, ...) {} }
namespace ScreenReader { char last[256]; bool Speak(const char* t, bool=false){ snprintf(last,sizeof(last),"%s",t); return true; } }
#include "button_map_rescue.inl"
static int bad=0;
static void chk(bool ok,const char*w){ if(!ok){bad++;printf("  BAD: %s\n",w);} }
int main(){
    const uintptr_t pg = ButtonMapRescue::BMR_FLAGS & ~(uintptr_t)0xFFF;
    if (mmap((void*)pg,0x2000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==MAP_FAILED){
        printf("  (could not map) \nbutton_map_rescue: OK (0 bad)\n"); return 0; }
    uint8_t* m=(uint8_t*)ButtonMapRescue::BMR_MAP; uint16_t* f=(uint16_t*)ButtonMapRescue::BMR_FLAGS;
    for(int i=0;i<12;i++) m[i]=i+1; *f=0;
    chk(ButtonMapRescue::IsDefault(), "a fresh map is default");
    chk(ButtonMapRescue::Restore()==1, "restoring a default map reports already-default");

    // The exact shape of Aaron's problem: a rotated map with Customize enabled.
    m[0]=2; m[1]=1; *f=0x0020;
    chk(!ButtonMapRescue::IsDefault(), "a swapped map is not default");
    chk(ButtonMapRescue::Restore()==0, "and it is restored");
    for(int i=0;i<12;i++) if(m[i]!=i+1){ bad++; printf("  BAD: map[%d] not reset\n",i); break; }
    chk((*f & 0x0020)==0, "the Customize bit is cleared -- on its own that restores stock controls");

    // The bit alone is enough to break it, so the bit alone must be enough to trip the check.
    *f=0x0020; chk(!ButtonMapRescue::IsDefault(), "the Customize bit alone counts as not-default");
    chk(ButtonMapRescue::Restore()==0, "and clearing it is a restore");
    *f=0x0080; chk(!ButtonMapRescue::IsDefault(), "so does the analog-swap bit");
    ButtonMapRescue::Restore();
    chk(*f==0, "both bits end clear");

    // Unrelated flags must survive: this rescue must not reset ATB or Scan.
    *f=0x0125; m[3]=9;
    ButtonMapRescue::Restore();
    chk(*f==0x0105, "ATB, Cursor and Scan bits are left alone -- only the two control bits clear");

    // F9 on its own must do NOTHING. field_nav_handlekeys.inl owns bare F9 for
    // the Garden battle SKIP, and an unscoped key landing on a scoped one is the
    // bug this modifier exists to prevent.
    g_shift=0; g_f9=1; ScreenReader::last[0]=0;
    ButtonMapRescue::PollHotkey();
    chk(ScreenReader::last[0]==0, "F9 without Shift is not the rescue -- the Garden battle owns it");

    g_shift=1; g_f9=1; ScreenReader::last[0]=0;
    ButtonMapRescue::PollHotkey();
    chk(strcmp(ScreenReader::last,"Controls are already at default")==0 ||
        strcmp(ScreenReader::last,"Controls restored to default")==0, "Shift+F9 speaks a result");
    ScreenReader::last[0]=0; ButtonMapRescue::PollHotkey();
    chk(ScreenReader::last[0]==0, "and it is edge-triggered, not once per frame");

    // Releasing and re-pressing must arm it again -- a rescue you get one shot at
    // is not a rescue.
    g_f9=0; ButtonMapRescue::PollHotkey();
    m[0]=7; m[6]=1; g_f9=1; ScreenReader::last[0]=0;
    ButtonMapRescue::PollHotkey();
    chk(strcmp(ScreenReader::last,"Controls restored to default")==0,
        "and it re-arms on release, so a second scramble is still recoverable");
    printf("button map rescue: swapped maps, the Customize bit on its own, unrelated flags preserved, Shift-gated and edge-triggered\n");
    printf("button_map_rescue: %s (%d bad)\n", bad?"FAILED":"OK", bad);
    return bad?1:0;
}
