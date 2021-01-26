#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

#include "qlib.h"

/**
 * Supported features:
 * [ ] msgQCreate priority/fifo
 * [x] msgQSend priority
 * [x] msgQReceive
 * [x] msgQDelete
 * [ ] msgQDelete safe -> needs VxWorks-like wrapper struct
 */

#define MSG_Q_CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      if (0) fprintf(stderr, "Failed to check '%s' in '%s':%d\n", #cond, __func__,    \
              __LINE__);                                                       \
      goto failed;                                                             \
    }                                                                          \
  } while (0)

struct msg_q {
  size_t capacity;
  size_t used;
  size_t maxMsgLength;
  uint8_t *data;

  size_t head;
  uint8_t isDestroyed;

  pthread_mutex_t q_mutex;
  pthread_cond_t q_cond;
};

typedef struct msg_q msg_q;

MSG_Q_ID msgQCreate(int maxMsgs, int maxMsgLength, int options) {
  msg_q *q = NULL;
  MSG_Q_CHECK(maxMsgs > 0);
  MSG_Q_CHECK(maxMsgLength > 0);

  q = malloc(sizeof(msg_q));
  MSG_Q_CHECK(NULL != q);
  memset(q, 0, sizeof(msg_q));

  q->capacity = maxMsgs;
  q->used = 0;
  q->maxMsgLength = maxMsgLength;
  q->data = (uint8_t *)malloc(maxMsgLength * maxMsgs);
  MSG_Q_CHECK(NULL != q->data);

  q->head = 0;
  q->isDestroyed = 0;

  MSG_Q_CHECK(0 == pthread_mutex_init(&q->q_mutex, NULL));
  MSG_Q_CHECK(0 == pthread_cond_init(&q->q_cond, NULL));

  return q;

failed:
  if (!q) {
    return NULL;
  }
  if (q->data) {
    free(q->data);
    q->data = NULL;
  }
  memset(q, 0, sizeof(msg_q));
  free(q);
  return NULL;
}

MSG_Q_STATUS msgQDelete(MSG_Q_ID msgQId) {
  /* wake up everyone and return MSG_Q_ERROR */
  MSG_Q_CHECK(NULL != msgQId);
  pthread_mutex_lock(&msgQId->q_mutex);
  msgQId->isDestroyed = 1;
  pthread_mutex_unlock(&msgQId->q_mutex);
  pthread_cond_broadcast(&msgQId->q_cond);
failed:
  return MSG_Q_ERROR;
}

MSG_Q_STATUS msgQSend(MSG_Q_ID msgQId, char *buffer, size_t nBytes, int timeout,
                int priority) {
  MSG_Q_STATUS rc = MSG_Q_ERROR;
  MSG_Q_CHECK(NULL != msgQId);

  pthread_mutex_lock(&msgQId->q_mutex);
  while ((!msgQId->isDestroyed) && (msgQId->used >= msgQId->capacity)) {
    pthread_cond_wait(&msgQId->q_cond, &msgQId->q_mutex);
  }

  MSG_Q_CHECK(!msgQId->isDestroyed);
  MSG_Q_CHECK(msgQId->used < msgQId->capacity);

  size_t idx = msgQId->head + msgQId->used;
  if (MSG_PRI_URGENT == priority) {
    msgQId->head = (msgQId->capacity + msgQId->head - 1) % msgQId->capacity;
    idx = msgQId->head;
  }
  idx %= msgQId->capacity;
  msgQId->used++;

  size_t maxMsgLength = msgQId->maxMsgLength;
  if (nBytes > maxMsgLength) {
    nBytes = maxMsgLength;
  }

  memcpy(&msgQId->data[idx * maxMsgLength], buffer, nBytes);
  rc = nBytes;

failed:
  pthread_mutex_unlock(&msgQId->q_mutex);
  pthread_cond_broadcast(&msgQId->q_cond);
  return rc;
}

MSG_Q_STATUS msgQReceive(MSG_Q_ID msgQId, char *buffer, size_t maxNBytes, int timeout) {
  MSG_Q_STATUS rc = MSG_Q_ERROR;
  MSG_Q_CHECK(NULL != msgQId);

  pthread_mutex_lock(&msgQId->q_mutex);
  while ((!msgQId->isDestroyed) && (msgQId->used <= 0)) {
	if (timeout == MSG_Q_WAIT_FOREVER)
	{
		pthread_cond_wait(&msgQId->q_cond, &msgQId->q_mutex);
	}
	else
	{
		/**
		 * for now, return immediately if queue is empty
		 */

		int pthr_status = -1;
		struct timespec ts = {};
		struct timeval now = {};
		gettimeofday(&now, NULL);
		ts.tv_sec = now.tv_sec;
		ts.tv_nsec = now.tv_usec * 1000UL;

		pthr_status = pthread_cond_timedwait(&msgQId->q_cond, &msgQId->q_mutex, &ts);

		if (pthr_status) {
			goto timeout;
		}
	}
  }

  MSG_Q_CHECK(!msgQId->isDestroyed);
  MSG_Q_CHECK(msgQId->used > 0);

  size_t idx = msgQId->head;
  msgQId->head++;
  msgQId->used--;
  idx %= msgQId->capacity;

  size_t maxMsgLength = msgQId->maxMsgLength;
  if (maxNBytes > maxMsgLength) {
    maxNBytes = maxMsgLength;
  }

  memcpy(buffer, &msgQId->data[idx * maxMsgLength], maxNBytes);
  memset(&msgQId->data[idx * maxMsgLength], 0, maxNBytes);
  rc = maxNBytes;

timeout:
  pthread_mutex_unlock(&msgQId->q_mutex);
  return rc;
failed:
  pthread_mutex_unlock(&msgQId->q_mutex);
  pthread_cond_broadcast(&msgQId->q_cond);
  return rc;
}

int msgQNumMsgs(MSG_Q_ID msgQId) {
  MSG_Q_CHECK(NULL != msgQId);
  return msgQId->used;
failed:
  return MSG_Q_ERROR;
}

#if 0
/******************************************************************************
 * Message Queue Library testing routines
 *****************************************************************************/
enum {
  TEST_Q_N_MSGS = 10,
  TEST_Q_MSG_LEN = 64,
};

static uint32_t calc_sum(uint8_t *data, size_t length) {
  uint32_t sum = 0;
  MSG_Q_CHECK(NULL != data);

  size_t i;
  for (i = 0; i < length; i++) {
    sum += data[i];
  }

failed:
  return sum;
}

static void fill_rand(uint8_t *data, size_t length) {
	size_t i;
	for (i = 0; i < length; i++) {
		data[i] = rand();
	}
}

static void *consumer(void *arg) {
  MSG_Q_ID q = (MSG_Q_ID)arg;
  MSG_Q_CHECK(NULL != q);

  while (1) {
    uint8_t data[TEST_Q_MSG_LEN] = {};

    fprintf(stderr, "CONSUMER: waiting\n");
    MSG_Q_STATUS stat = msgQReceive(q, (char *)data, TEST_Q_MSG_LEN, 0);
    MSG_Q_CHECK(MSG_Q_ERROR != stat);

    uint32_t csum = calc_sum(data, TEST_Q_MSG_LEN);
    fprintf(stderr, "CONSUMER: checksum %x\n", csum);
  }

failed:
  return NULL;
}

static void *producer(void *arg) {
  MSG_Q_ID q = (MSG_Q_ID)arg;
  MSG_Q_CHECK(NULL != q);

  while (1) {
    uint8_t data[TEST_Q_MSG_LEN] = {};
	fill_rand(data, TEST_Q_MSG_LEN);

    uint32_t csum = calc_sum(data, TEST_Q_MSG_LEN);
    fprintf(stderr, "PRODUCER: checksum %x waiting\n", csum);
    MSG_Q_STATUS stat = msgQSend(q, (char *)data, TEST_Q_MSG_LEN, 0, MSG_PRI_NORMAL);
    MSG_Q_CHECK(MSG_Q_ERROR != stat);
    fprintf(stderr, "PRODUCER: checksum %x gone\n", csum);
  }

failed:
  return NULL;
}

int main() {
  pthread_t t_consumer, t_producer;

  memset(&t_consumer, 0, sizeof(t_consumer));
  memset(&t_producer, 0, sizeof(t_producer));

  MSG_Q_ID testQ = msgQCreate(TEST_Q_N_MSGS, TEST_Q_MSG_LEN, 0);
  MSG_Q_CHECK(NULL != testQ);

  MSG_Q_CHECK(0 == pthread_create(&t_consumer, NULL, consumer, testQ));
  MSG_Q_CHECK(0 == pthread_create(&t_producer, NULL, producer, testQ));

  sleep(5);
  msgQDelete(testQ);

  pthread_join(t_consumer, NULL);
  pthread_join(t_producer, NULL);
  return 0;
failed:
  return -1;
}

#endif
