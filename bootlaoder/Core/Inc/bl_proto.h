#ifndef BL_PROTO_H
#define BL_PROTO_H

#include "usart.h"

void bl_proto_init(void);
void bl_proto_poll(void);
int  bl_proto_activity(void);   /* 探测窗口期间收到过 SYNC/ENTER_BOOT 帧返回 1 */

#endif /* BL_PROTO_H */
