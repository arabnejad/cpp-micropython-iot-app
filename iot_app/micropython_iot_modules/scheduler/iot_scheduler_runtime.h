#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Clears timers left by the previous Python application. */
void iot_scheduler_reset(void);

/*
 * Finds how long the main loop can wait before the next Python timer is due.
 *
 * The function checks every active timer and selects the one with the shortest
 * time remaining. For example, if two timers have 600 ms and 4600 ms left, it
 * writes 600 to delay_milliseconds.
 *
 *   0 ms          600 ms                              4600 ms
 *   |-------------|-----------------------------------|
 *   Check timers   Timer A is due                     Timer B is due
 *   <--- 600 ms -->
 *
 * This function only reads the timer deadlines. It does not reduce their
 * remaining time or run any callback.
 *
 * Returns 1 after writing a delay. Returns 0 when there are no active timers or
 * delay_milliseconds is null.
 */
int iot_scheduler_next_delay_milliseconds(uint32_t *delay_milliseconds);

/*
 * Updates the Python timers using the time passed since the previous update.
 *
 * elapsed_milliseconds does not make this function wait. It tells the
 * scheduler how much time has already passed. The function subtracts that time
 * from every active timer and immediately runs each callback that is now due.
 *
 * For example, suppose:
 *
 *   Timer A repeats every 1000 ms and has 300 ms remaining.
 *   Timer B repeats every 5000 ms and has 2000 ms remaining.
 *   The main loop reports that 400 ms has passed.
 *
 * Timer A became due after 300 ms. Because 400 ms passed, its callback is now
 * 100 ms late. Its next 1000 ms interval has already used those 100 ms, so the
 * next call is due in 1000 - 100 = 900 ms.
 *
 *   0 ms          300 ms       400 ms                    1300 ms
 *   |-------------|------------|-------------------------|
 *   Start         A was due    Function runs             A due again
 *                              <-------- 900 ms -------->
 *
 * Timer B is not due. Its new remaining time is 2000 - 400 = 1600 ms.
 *
 * A callback runs at most once during one update. Missed intervals are not run
 * repeatedly to catch up. Returns 1 when all due callbacks finish normally.
 * If a callback raises an exception, the function prints and saves its
 * traceback, then returns 0.
 */
int iot_scheduler_run_due_callbacks(uint32_t elapsed_milliseconds, char *traceback_buffer,
                                    size_t traceback_buffer_size);

#ifdef __cplusplus
}
#endif
