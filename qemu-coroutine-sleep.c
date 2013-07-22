/*
 * QEMU coroutine sleep
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Stefan Hajnoczi    <stefanha@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "block/coroutine.h"
#include "qemu/timer.h"
#include "qemu/thread.h"

typedef struct CoSleepCB {
    QEMUTimer *ts;
    Coroutine *co;
} CoSleepCB;

static void co_sleep_cb(void *opaque)
{
    CoSleepCB *sleep_cb = opaque;

    qemu_coroutine_enter(sleep_cb->co, NULL);
}

void coroutine_fn co_sleep_ns(QEMUClock *clock, int64_t ns)
{
    CoSleepCB sleep_cb = {
        .co = qemu_coroutine_self(),
    };
    sleep_cb.ts = qemu_new_timer(clock, SCALE_NS, co_sleep_cb, &sleep_cb);
    qemu_mod_timer(sleep_cb.ts, qemu_get_clock_ns(clock) + ns);
    qemu_coroutine_yield();
    qemu_del_timer(sleep_cb.ts);
    qemu_free_timer(sleep_cb.ts);
}

typedef struct CoAioSleepCB {
    QEMUBH *bh;
    int64_t ns;
    Coroutine *co;
} CoAioSleepCB;

static void co_aio_sleep_cb(void *opaque)
{
    CoAioSleepCB *aio_sleep_cb = opaque;

    qemu_coroutine_enter(aio_sleep_cb->co, NULL);
}

static void *sleep_thread(void *opaque)
{
    CoAioSleepCB *aio_sleep_cb = opaque;
    struct timespec req = {
        .tv_sec = aio_sleep_cb->ns / 1000000000,
        .tv_nsec = aio_sleep_cb->ns % 1000000000,
    };
    struct timespec rem;

    while (nanosleep(&req, &rem) < 0 && errno == EINTR) {
        req = rem;
    }

    qemu_bh_schedule(aio_sleep_cb->bh);

    return NULL;
}

void coroutine_fn co_aio_sleep_ns(int64_t ns)
{
    CoAioSleepCB aio_sleep_cb = {
        .ns = ns,
        .co = qemu_coroutine_self(),
    };
    QemuThread thread;

    aio_sleep_cb.bh = qemu_bh_new(co_aio_sleep_cb, &aio_sleep_cb);
    qemu_thread_create(&thread, sleep_thread, &aio_sleep_cb,
                       QEMU_THREAD_DETACHED);
    qemu_coroutine_yield();
    qemu_bh_delete(aio_sleep_cb.bh);
}
