/*
 * @file    serial_task.h
 * @brief   UART0 command task for profile switching via USB serial.
 * @author  Claude Code (AI) / Niklas Grill
 * @date    2026-04-29
 */

#ifndef SERIAL_TASK_H
#define SERIAL_TASK_H

void serial_task_init(void);
void serial_task(void *arg);

#endif /* SERIAL_TASK_H */
