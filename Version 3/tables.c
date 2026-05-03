#include "logengine.h"

const char *SEV_NAMES[SEV_COUNT] = {
    "DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL", "UNKNOWN"
};

const char *SEV_COLORS[SEV_COUNT] = {
    "\033[36m",   /* DEBUG    - cyan    */
    "\033[32m",   /* INFO     - green   */
    "\033[33m",   /* WARNING  - yellow  */
    "\033[31m",   /* ERROR    - red     */
    "\033[35m",   /* CRITICAL - magenta */
    "\033[37m"    /* UNKNOWN  - white   */
};
