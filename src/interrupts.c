/*
 * Lockup net: anything that stops the CFW from running normally lands in the failsafe
 * bootloader instead of hanging forever.
 *
 * Why this exists. The boot only validates the image header and jumps — there is no
 * watchdog-fallback and no boot-attempt counter (Ghidra). So a CRC-valid image that
 * hangs, or that faults before ble_init, never falls through to the failsafe and never
 * raises BLE: SWD would be the only way back. Routing the watchdog's NMI into
 * enter_failsafe() converts that dead end into a recoverable state — the validflag is
 * cleared and the next boot lands in the failsafe, where the phone reflashes over BLE.
 *
 * Two paths arrive here:
 *   - a hang: the main loop stops reloading the watchdog, which fires NMI (CFG_WDOG).
 *   - a fault: HardFault_HandlerC's production path calls wdg_reload(1) precisely to
 *     "force execution of NMI Handler". That path needs CFG_DEVELOPMENT_DEBUG OFF —
 *     with it on, the SDK freezes the watchdog and spins waiting for a debugger, and
 *     nothing recovers.
 *
 * What it does NOT cover: a stall with interrupts disabled and the watchdog already
 * frozen, and anything that dies before the watchdog is running at all.
 *
 * NMI_Handler is .weak in startup_DA14531.S, so this definition replaces the SDK's.
 * (This file previously held ~19 IRQ_* stubs that each did __BKPT(0) and spun. They
 * were dead code: the vector table uses the unprefixed names, and the linker dropped
 * every IRQ_* symbol. Deleted rather than left to mislead.)
 */
#include <stdbool.h>

#include <arch.h>
#include <user_app.h>

void NMI_Handler(void)
{
    /*
     * Re-entrancy guard: if clearing the validflag is itself what faulted, do not
     * recurse — reset and let the boot decide. A reset loop is survivable; spiralling
     * into a stack overflow is not.
     *
     * ponytail: plain static, so a fault before .bss init sees garbage and we may skip
     *           straight to the reset. Acceptable — that is the safe direction.
     */
    static volatile bool recovering;

    if (!recovering)
    {
        recovering = true;
        enter_failsafe();   /* resets on success; returns only if nothing valid was found */
    }

    platform_reset(0);
}
