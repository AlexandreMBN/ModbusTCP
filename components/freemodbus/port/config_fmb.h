#ifndef CONFIG_FMB_H
#define CONFIG_FMB_H

/* Communication settings */
#define CONFIG_FMB_COMM_MODE_TCP_EN                1
#define CONFIG_FMB_COMM_MODE_RTU_EN                1

/* Task configuration */
#define CONFIG_FMB_PORT_TASK_STACK_SIZE            4096
#define CONFIG_FMB_PORT_TASK_PRIO                  10
#define CONFIG_FMB_PORT_TASK_AFFINITY              0x0

/* Event handling */
#define CONFIG_FMB_EVENT_QUEUE_TIMEOUT             20
#define CONFIG_FMB_CONTROLLER_NOTIFY_QUEUE_SIZE    20
#define CONFIG_FMB_CONTROLLER_NOTIFY_TIMEOUT       20

/* TCP settings */
#define CONFIG_FMB_TCP_PORT_DEFAULT                502
#define CONFIG_FMB_TCP_PORT_MAX_CONN               16
#define CONFIG_FMB_TCP_CONNECTION_TOUT_SEC         20

/* Timer settings */
#define CONFIG_FMB_TIMER_USE_ISR_DISPATCH_METHOD   1

#endif /* CONFIG_FMB_H */