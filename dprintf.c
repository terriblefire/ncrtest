/*
 * Dual printf - outputs to both standard console and debug console
 * This outputs to both printf() and RawPutChar for maximum visibility
 */

#include <stdarg.h>
#include <stdio.h>
#include <exec/types.h>

extern struct ExecBase *SysBase;

/* Direct call to RawPutChar via assembly (SysBase->RawPutChar at offset -516) */
static void raw_putchar(char c)
{
    register struct ExecBase *sysbase asm("a6") = SysBase;
    register char ch asm("d0") = c;

    __asm__ volatile (
        "jsr -516(a6)"
        :
        : "r" (sysbase), "r" (ch)
        : "d0", "d1", "a0", "a1"
    );
}

/*
 * dbgprintf - dual output printf
 * Sends output to both regular console (printf) and debug output (RawPutChar)
 */
void dbgprintf(const char *format, ...)
{
    va_list args;
    char buffer[512];
    char *p;

    /* Format the string using vsprintf */
    va_start(args, format);
    vsprintf(buffer, format, args);
    va_end(args);

    /* Output to regular console via printf */
    printf("%s", buffer);

    /* Also output to debug console via RawPutChar */
    for (p = buffer; *p; p++) {
        raw_putchar(*p);
    }
}
