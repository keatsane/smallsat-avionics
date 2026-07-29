#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

/* STM32F446 starts from the 16 MHz HSI clock until project clock setup is added. */
#define configCPU_CLOCK_HZ            ((unsigned long)16000000UL)
#define configTICK_RATE_HZ            ((TickType_t)1000U)
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS

#define configUSE_PREEMPTION       1
#define configUSE_TIME_SLICING     1
#define configUSE_IDLE_HOOK        0
#define configUSE_TICK_HOOK        0
#define configUSE_NEWLIB_REENTRANT 0

#define configMAX_PRIORITIES     5
#define configMINIMAL_STACK_SIZE ((unsigned short)128)
#define configTOTAL_HEAP_SIZE    ((size_t)(16U * 1024U))
#define configMAX_TASK_NAME_LEN  16
#define configIDLE_SHOULD_YIELD  1

#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configSUPPORT_STATIC_ALLOCATION  0

#define configUSE_MUTEXES             1
#define configUSE_RECURSIVE_MUTEXES   0
#define configUSE_COUNTING_SEMAPHORES 1
#define configUSE_QUEUE_SETS          0
#define configQUEUE_REGISTRY_SIZE     8

#define configUSE_TIMERS             1
#define configTIMER_TASK_PRIORITY    2
#define configTIMER_QUEUE_LENGTH     5
#define configTIMER_TASK_STACK_DEPTH (configMINIMAL_STACK_SIZE * 2U)

#define configCHECK_FOR_STACK_OVERFLOW     0
#define configUSE_MALLOC_FAILED_HOOK       0
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0
#define configCHECK_HANDLER_INSTALLATION   1

#define configUSE_TRACE_FACILITY             0
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#define configGENERATE_RUN_TIME_STATS        0

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

#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
