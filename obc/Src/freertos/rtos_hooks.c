/**
 * @file   rtos_hooks.c
 * @brief  the callbacks the kernel configuration obliges this build to provide
 */

#include "FreeRTOS.h"
#include "drivers/panic.h"
#include "stm32f446xx.h"
#include "task.h"

// static allocation means the kernel cannot allocate the idle task itself, so the application
// hands it storage (configSUPPORT_DYNAMIC_ALLOCATION is 0). no timer-task twin below because
// configUSE_TIMERS is 0
void vApplicationGetIdleTaskMemory(StaticTask_t** ppxIdleTaskTCBBuffer,
                                   StackType_t** ppxIdleTaskStackBuffer,
                                   configSTACK_DEPTH_TYPE* puxIdleTaskStackSize) {
    static StaticTask_t idle_tcb;
    static StackType_t idle_stack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer = &idle_tcb;
    *ppxIdleTaskStackBuffer = idle_stack;
    *puxIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

// configCHECK_FOR_STACK_OVERFLOW is 2, so the kernel calls this instead of letting a blown stack
// corrupt its neighbour quietly. name the task and reset - the stack is already past its bounds,
// so there is nothing left to trust. next boot reports reset=software (REQ-WDG-002) and this line
// says which task it was.
//
// the panic console, not uart_write: this runs from inside the scheduler with interrupts masked
// above the uart isr, so the driver's tx ring would never drain and the write would spin forever
void vApplicationStackOverflowHook(TaskHandle_t task, char* name) {
    (void)task;

    panic_puts("\r\nSTACK OVERFLOW: ");
    panic_puts(name);
    panic_puts("\r\n");
    panic_drain();

    NVIC_SystemReset();
}
