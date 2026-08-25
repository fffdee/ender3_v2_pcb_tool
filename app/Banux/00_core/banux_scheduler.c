/** @file banux_scheduler.c @brief Banux cooperative system scheduler. */
#include "banux_scheduler.h"
#include "bg_shell.h"
#include "command_parser.h"

void BanuxScheduler_Process(void)
{
    Shell_Process();
    CommandParser_Process();
}
