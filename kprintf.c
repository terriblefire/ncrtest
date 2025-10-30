
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <exec/types.h>
#include <proto/exec.h>

#include "kprintf.h"

#if defined(ENABLE_KPRINTF)

/* Set UART speed for serial debug output */
static void set_uart_speed(uint32_t speed)
{
    __asm__ volatile (
        "move.w %0,0xdff032"
        :
        : "d" (speed)
    );
}

/* Wrapper for raw_put_char callback used by RawDoFmt */
static void raw_put_char(void)
{
    /* RawDoFmt calls this with character in D0, data pointer in A3 */
    __asm__ volatile (
        "move.l a3,-(sp)\n"
        "move.l a6,-(sp)\n"
        "move.l d0,-(sp)\n"
        "move.l 12(sp),a6\n"  /* Get SysBase from A3 */
        "move.l 4(sp),d0\n"   /* Get character from stack */
        "jsr -516(a6)\n"      /* Call RawPutChar */
        "addq.l #4,sp\n"
        "move.l (sp)+,a6\n"
        "move.l (sp)+,a3\n"
    );
}

void kprintf(const char *format, ...)
{
    va_list args;
    struct ExecBase* SysBase = *((struct ExecBase **) (4L));

    set_uart_speed(3546895/9600);

    va_start(args, format);

    RawDoFmt((STRPTR) format, (APTR) args,
             raw_put_char, (APTR) SysBase);

    va_end(args);
}

static char* strcpy(char *to, const char *from)
{
	char *save = to;

	for (; (*to = *from) != '\0'; ++from, ++to);
	return(save);
}

static char* strcat(char *s, const char *append)
{
	char *save = s;

	for (; *s; ++s);
	while ((*s++ = *append++) != '\0');
	return(save);
}

static int strcmp(const char *s1, const char *s2)
{
    while (*s1 == *s2++)
        if (*s1++ == 0)
            return (0);
    return (*(unsigned char *)s1 - *(unsigned char *)--s2);
}

static bool isgraph(int c)
{
    return 0x21 <= c && c <= 0x7e;
}

void DumpBuffer(const uint8_t* buffer, uint32_t size)
{
    uint32_t i, j, len;
    char format[150];
    char alphas[27];
    strcpy(format, "$%08lx [%03lx]: %04lx %04lx %04lx %04lx %04lx %04lx %04lx %04lx ");

    for (i = 0; i < size; i += 16) {
        len = size - i;

        // last line is less than 16 bytes? rewrite the format string
        if (len < 16) {
            strcpy(format, "$%08lx [%03lx]: ");

            for (j = 0; j < 16; j+=2) {
                if (j < len) {
                    strcat(format, "%04lx");

                } else {
                    strcat(format, "____");
                }

                strcat(format, " ");
            }

        } else {
            len = 16;
        }

        // create the ascii representation
        alphas[0] = '\'';
        for (j = 0; j < len; ++j) {
            alphas[j+1] = (isgraph(buffer[i + j]) ? buffer[i + j] : '.');
        }
        for (; j < 16; ++j) {
            alphas[j+1] = '_';
        }
        strcpy(&alphas[j+1], "'\n");

        uint16_t* p = (uint16_t*)&buffer[i];
        kprintf(format, buffer+i, i, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        kprintf("%s", alphas);
    }
}

#endif
