/** @file banux_scheduler.h @brief Banux cooperative system scheduler. */
#ifndef BANUX_SCHEDULER_H
#define BANUX_SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

/** Run one nonblocking iteration of all core-managed system services. */
void BanuxScheduler_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* BANUX_SCHEDULER_H */
