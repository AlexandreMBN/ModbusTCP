#ifndef PORT_TCP_CONFIG_H
#define PORT_TCP_CONFIG_H

/* TCP slave configuration */
#define CONFIG_FMB_TCP_PORT_DEFAULT             502
#define CONFIG_FMB_TCP_PORT_MAX_CONN            16
#define CONFIG_FMB_TCP_CONNECTION_TOUT_SEC      20

/* Task configuration */
#define CONFIG_FMB_PORT_TASK_STACK_SIZE         4096
#define CONFIG_FMB_PORT_TASK_PRIO              10
#define CONFIG_FMB_PORT_TASK_AFFINITY          0x0

/* Event handling */
#define CONFIG_FMB_EVENT_QUEUE_TIMEOUT          20
#define CONFIG_FMB_CONTROLLER_NOTIFY_QUEUE_SIZE 20
#define CONFIG_FMB_CONTROLLER_NOTIFY_TIMEOUT    20

/* Enable TCP mode */
#define CONFIG_FMB_COMM_MODE_TCP_EN            1
#define CONFIG_FMB_TIMER_USE_ISR_DISPATCH_METHOD 1

#endif /* PORT_TCP_CONFIG_H */