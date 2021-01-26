#ifndef __QLIB_H__
#define __QLIB_H__

struct msg_q;
typedef struct msg_q *MSG_Q_ID;

#define MSG_Q_STATUS int
#define MSG_Q_ERROR (-1)

#define MSG_Q_FIFO 0x00
#define MSG_Q_PRIORITY 0x01
#define MSG_Q_EVENTSEND_ERR_NOTIF 0x02

#define MSG_PRI_NORMAL 0
#define MSG_PRI_URGENT 1

MSG_Q_ID msgQCreate(int maxMsgs, int maxMsgLength, int options);
MSG_Q_STATUS msgQDelete(MSG_Q_ID msgQId);

#define MSG_Q_NO_WAIT 0
#define MSG_Q_WAIT_FOREVER (-1)

MSG_Q_STATUS msgQSend(MSG_Q_ID msgQId, char *buffer, size_t nBytes, int timeout,
                int priority);
MSG_Q_STATUS msgQReceive(MSG_Q_ID msgQId, char *buffer, size_t maxNBytes, int timeout);

#endif //__QLIB_H__
