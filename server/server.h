#ifndef CONSTANTS_H
#define CONSTANTS_H

#define DIRECTED_TIMEOUT 600 /* seconds to hold a directed link alive */

#define QUEUE_NUM -1        /* maximum messages in an outgoing queue */
#define QUEUE_MEM 1048576   /* maximum memory in an outgoing queue */
#define QUEUE_AGE 600       /* maximum age of an outgoing queue */

extern int server_port;

#endif
