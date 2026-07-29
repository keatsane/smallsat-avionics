#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

/* The live core clock, not a literal: clock_init() brings the part to 180 MHz and writes that
 * into SystemCoreClock, so taking it from there cannot drift from the hardware. FreeRTOS programs
 * SysTick from this, and a stale value does not fail loudly - it just makes every delay wrong by
 * the ratio (a 16 MHz constant against a 180 MHz part stretches every tick 11.25x).
 *
 * CMSIS declares it with C linkage, so this has to as well - otherwise a C++ translation unit that
 * sees both headers gets two conflicting declarations of the same symbol. */
#ifdef __cplusplus
extern "C" uint32_t SystemCoreClock;
#else
extern uint32_t SystemCoreClock;
#endif
#define configCPU_CLOCK_HZ            (SystemCoreClock)
#define configTICK_RATE_HZ            ((TickType_t)1000U)
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS

#define configUSE_PREEMPTION 1
/* off deliberately: every task gets its own priority, so slicing would only add nondeterminism
 * between any two that ever ended up sharing one */
#define configUSE_TIME_SLICING 0
#define configUSE_IDLE_HOOK    0
#define configUSE_TICK_HOOK    0
/* newlib's printf family is not reentrant without per-task state, which costs RAM in every task.
 * cheaper and clearer to keep it off and let one task own all console and telemetry output */
#define configUSE_NEWLIB_REENTRANT 0

/* headroom over the planned task set (idle, downlink, telemetry, sensors, uplink, control,
 * watchdog) - each level costs one list head, so being tight here buys nothing */
#define configMAX_PRIORITIES     8
#define configMINIMAL_STACK_SIZE ((unsigned short)128)
#define configMAX_TASK_NAME_LEN  16
#define configIDLE_SHOULD_YIELD  1

/* static only. the rest of this codebase already refuses the heap - ETL fixed-capacity
 * containers, no image buffer in RAM, no allocation in fixed-rate paths - and static task and
 * queue storage makes memory a link-time fact instead of a boot-time gamble. with dynamic off, a
 * stray xTaskCreate fails to link rather than failing in flight, which is why there is no
 * configTOTAL_HEAP_SIZE below */
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configSUPPORT_STATIC_ALLOCATION  1

#define configUSE_MUTEXES             1
#define configUSE_RECURSIVE_MUTEXES   0
#define configUSE_COUNTING_SEMAPHORES 1
#define configUSE_QUEUE_SETS          0
#define configQUEUE_REGISTRY_SIZE     8

/* off: every periodic job here is a task doing vTaskDelayUntil, so the timer daemon would be a
 * task and a queue bought for nothing. turn it on with the first job that genuinely needs a
 * one-shot callback */
#define configUSE_TIMERS 0

/* 2 = the pattern check as well as the pointer check. a blown stack is the classic rtos failure
 * and it corrupts silently otherwise; REQ-RT-003 also promises stack high-water reporting, and
 * this is the half that catches the overflow rather than reporting the margin */
#define configCHECK_FOR_STACK_OVERFLOW 2

/* no heap to fail (see configSUPPORT_DYNAMIC_ALLOCATION), so there is no malloc hook to install */
#define configUSE_MALLOC_FAILED_HOOK       0
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0
#define configCHECK_HANDLER_INSTALLATION   1

#define configUSE_TRACE_FACILITY             0
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#define configGENERATE_RUN_TIME_STATS        0

/* any isr that calls a FromISR api must sit numerically at or above MAX_SYSCALL (that is, at or
 * less urgent than 5), or the port trips configASSERT. the board's side of that contract is
 * IRQ_PRIO_UART in board.h - every isr that can reach the kernel gets an explicit priority there,
 * because the reset default is 0 and 0 is the one value that is never legal */
#define configPRIO_BITS                              4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define INCLUDE_vTaskDelay                  1
#define INCLUDE_vTaskDelayUntil             1
#define INCLUDE_vTaskDelete                 1
#define INCLUDE_vTaskSuspend                1
#define INCLUDE_xTaskGetSchedulerState      1
#define INCLUDE_xTaskGetCurrentTaskHandle   1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_eTaskGetState               1

#define configASSERT(x)                \
    do {                               \
        if ((x) == 0) {                \
            __asm volatile("bkpt #0"); \
            for (;;) {                 \
            }                          \
        }                              \
    } while (0)

/* the port takes SVC and PendSV outright - nothing else wants them. SysTick is deliberately NOT
 * mapped: the systick driver owns that vector and forwards to xPortSysTickHandler, so millis()
 * survives the scheduler start (see systick.c) */
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler

#endif /* FREERTOS_CONFIG_H */
