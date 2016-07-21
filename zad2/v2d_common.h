#pragma once
#include "v2d_ioctl.h"
#include "vintage2d.h"
#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/circ_buf.h>
#include <linux/device.h>
#include <linux/dmapool.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#define V2D_PREFIX "vintage2d: "
#define V2D_MAX_DEV_COUNT 255
#define V2D_MIN_DIMENSION 1
#define V2D_MAX_DIMENSION 2048
#define V2D_DIMENSIONS_NOT_SET 0
#define V2D_PAGESIZE VINTAGE2D_PAGE_SIZE
#define V2D_PAGES (V2D_MAX_DIMENSION * V2D_MAX_DIMENSION / V2D_PAGESIZE)
#define V2D_INVALID_COMMAND 0xffffffffu
#define V2D_CMDBUF_SIZE 1024
#define V2D_ALL_INTRS                                                                                                  \
    (VINTAGE2D_INTR_NOTIFY | VINTAGE2D_INTR_INVALID_CMD | VINTAGE2D_INTR_PAGE_FAULT | VINTAGE2D_INTR_CANVAS_OVERFLOW | \
     VINTAGE2D_INTR_FIFO_OVERFLOW)

#define V2D_MIN_SPACE_FOR_COMMAND 16

#ifndef ENOTSUP
/* Same as in glibc */
#define ENOTSUP EOPNOTSUPP
#endif

struct v2d_command;
struct v2d_context;

struct v2d_device {
    struct pci_dev *pci_dev;
    struct cdev cdev;
    void __iomem *bar0;
    spinlock_t lock;

    struct dma_pool *dma_pool;

    uint32_t *cmd_buf;
    dma_addr_t dma_cmd_buf;

    unsigned long read_index;  // protected by lock
    unsigned long write_index; // protected by write_lock

    struct mutex write_lock;
    struct list_head current_commands; // protected by lock

    /* Normally protected by write_lock, but see v2d_release */
    volatile struct v2d_context *last_context;

    wait_queue_head_t write_queue;

    struct tasklet_struct tasklet;
    uint32_t last_counter;

    struct kobject kobject;
    struct semaphore release_semaphore;
};

typedef uint32_t v2d_pte_t;

struct v2d_page {
    void *cpu;
    struct page *page;
    dma_addr_t dma;
};

struct v2d_pagetable {
    v2d_pte_t *entries;
    dma_addr_t dma_addr;
    struct v2d_page pages[V2D_PAGES];
};

struct v2d_state {
    uint32_t src_pos_cmd;
    uint32_t dst_pos_cmd;
    uint32_t fill_color_cmd;
};

struct v2d_context {
    struct v2d_device *device;
    spinlock_t lock;
    uint16_t width, height; // protected by lock
    atomic_t ready;
    struct v2d_pagetable pages; // protected by lock
    struct v2d_state state;
    struct list_head commands;
};

struct v2d_command {
    struct v2d_context *context;
    struct list_head context_list, device_list;

    /* Position in the write buffer of the trailing counter command */
    size_t counter;

    /* Because every command maintains a separate wait queue
     * (for fsyncs), I need to perform reference counting,
     * so I would not free a command before the last fsync waiting
     * on it is awaken.
     */
    wait_queue_head_t queue;
    atomic_t refcount;
    atomic_t ready; // 1 if the command has finished, 0 otherwise
};
