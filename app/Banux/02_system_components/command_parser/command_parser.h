/** @file command_parser.h @brief Cooperative UTF-8 command script parser. */
#ifndef __COMMAND_PARSER_H__
#define __COMMAND_PARSER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void CommandParser_Init(void);
void CommandParser_RegisterCommands(void);
void CommandParser_Process(void);
int CommandParser_Start(const char *path);
int CommandParser_Delay(uint32_t milliseconds);
int CommandParser_IsRunning(void);

#ifdef __cplusplus
}
#endif

#endif /* __COMMAND_PARSER_H__ */
