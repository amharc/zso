#include "v2d_common.h"
#include "v2d_fileops.h"
#include "v2d_utils.h"
#include "v2d_utils.h"

static struct class *v2d_class;
static dev_t v2d_dev_base;
static DEFINE_IDR(v2d_idr);
static DEFINE_SPINLOCK(v2d_idr_lock);

static void v2d_device_kobj_release(struct kobject *kobj) {
    struct v2d_device *device;

    BUG_ON(!kobj);

    device = container_of(kobj, struct v2d_device, kobject);
    kobject_put(kobj->parent);

    /* We may finally release the device, no-one else is holding a reference to it */
    up(&device->release_semaphore);
}

static struct kobj_type v2d_device_kobj_type = {.release = v2d_device_kobj_release};

/* device->lock must be held, no context spinlocks must be held */
static void v2d_pop_commands(struct v2d_device *device) {
    struct v2d_command *command, *tmp;
    uint32_t counter = ioread32(device->bar0 + VINTAGE2D_COUNTER);

    if (counter == device->last_counter) {
        return;
    }

    /* If not, something has changed in the meantime. [device->read_index, ..., device->last_counter] was the position
     * of the last command processed by v2d_pop_commands. Hence, new counter did not pass last_counter, so every command
     * till the one with counter equal to `counter' has been processed.
     */

    list_for_each_entry_safe(command, tmp, &device->current_commands, device_list) {
        bool here = command->counter == counter;

        atomic_set(&command->ready, 1);

        list_del(&command->device_list);
        spin_lock_bh(&command->context->lock);
        list_del(&command->context_list);
        spin_unlock_bh(&command->context->lock);

        smp_mb();

        wake_up(&command->queue);
        v2d_put_command(command);

        if (here) {
            break;
        }
    }

    device->read_index = (ioread32(device->bar0 + VINTAGE2D_CMD_READ_PTR) - device->dma_cmd_buf) / sizeof(uint32_t);
    device->last_counter = counter;

    wake_up(&device->write_queue);
}

static void v2d_tasklet(unsigned long _v2d_device) {
    struct v2d_device *v2d_device = (struct v2d_device *)_v2d_device;

    spin_lock_bh(&v2d_device->lock);
    v2d_pop_commands(v2d_device);
    spin_unlock_bh(&v2d_device->lock);
}

static irqreturn_t v2d_irq(int irq, void *_v2d_device) {
    struct v2d_device *v2d_device = _v2d_device;
    uint32_t intr;

    intr = ioread32(v2d_device->bar0 + VINTAGE2D_INTR);
    if (!intr) {
        return IRQ_NONE;
    }
    iowrite32(intr, v2d_device->bar0 + VINTAGE2D_INTR);

    if (unlikely(intr != VINTAGE2D_INTR_NOTIFY)) {
        v2d_debug(v2d_device);
        printk(KERN_CRIT V2D_PREFIX "invalid interrupt: %zd\n", intr);
    }

    tasklet_schedule(&v2d_device->tasklet);

    return IRQ_HANDLED;
}

static int v2d_probe(struct pci_dev *pci_dev, const struct pci_device_id *dev_id) {
    struct v2d_device *v2d_device;
    int minor = 0, ret;
    struct device *device;

    v2d_device = kzalloc(sizeof(*v2d_device), GFP_KERNEL);
    if (unlikely(!v2d_device)) {
        printk(KERN_ERR V2D_PREFIX "no memory for v2d_device\n");
        return -ENOMEM;
    }

    spin_lock_init(&v2d_device->lock);
    INIT_LIST_HEAD(&v2d_device->current_commands);
    init_waitqueue_head(&v2d_device->write_queue);
    mutex_init(&v2d_device->write_lock);
    sema_init(&v2d_device->release_semaphore, 0);
    v2d_device->last_context = NULL;
    v2d_device->last_counter = 0xffffffff;
    v2d_device->write_index = 0;
    v2d_device->read_index = 0;

    tasklet_init(&v2d_device->tasklet, v2d_tasklet, (unsigned long)v2d_device);

    v2d_device->pci_dev = pci_dev;
    cdev_init(&v2d_device->cdev, &v2d_fops);

    ret = pci_enable_device(pci_dev);
    if (IS_ERR_VALUE(ret)) {
        printk(KERN_ERR V2D_PREFIX "pci_enable_device returned %d\n", ret);
        goto err_pci_enable_device;
    }

    ret = pci_request_regions(pci_dev, "v2d");
    if (IS_ERR_VALUE(ret)) {
        printk(KERN_ERR V2D_PREFIX "pci_request_regions returned %d\n", ret);
        goto err_pci_request_regions;
    }

    v2d_device->bar0 = pci_iomap(pci_dev, 0, 0);
    if (!v2d_device->bar0) {
        ret = -ENOMEM;
        printk(KERN_ERR V2D_PREFIX "pci_iomap returned NULL\n");
        goto err_pci_iomap;
    }

    pci_set_master(pci_dev);
    ret = dma_set_mask_and_coherent(&pci_dev->dev, DMA_BIT_MASK(32));
    if (IS_ERR_VALUE(ret)) {
        printk(KERN_ERR V2D_PREFIX "dma_set_mask_and_coherent returned %d\n", ret);
        goto err_dma_set_mask_and_coherent;
    }

    v2d_device->dma_pool = dma_pool_create("vintage2d", &pci_dev->dev, V2D_PAGESIZE, V2D_PAGESIZE, 0);
    if (!v2d_device->dma_pool) {
        ret = -ENOMEM;
        printk(KERN_ERR V2D_PREFIX "dma_pool_create returned NULL\n");
        goto err_dma_pool_create;
    }

    v2d_device->cmd_buf = v2d_alloc_page(v2d_device, &v2d_device->dma_cmd_buf);
    if (!v2d_device->cmd_buf) {
        ret = -ENOMEM;
        printk(KERN_ERR V2D_PREFIX "v2d_alloc_page returned NULL\n");
        goto err_v2d_alloc_page;
    }

    v2d_device->cmd_buf[V2D_CMDBUF_SIZE - 1] = VINTAGE2D_CMD_KIND_JUMP | v2d_device->dma_cmd_buf;

    iowrite32(V2D_ALL_INTRS, v2d_device->bar0 + VINTAGE2D_INTR_ENABLE);
    iowrite32(VINTAGE2D_RESET_DRAW | VINTAGE2D_RESET_FIFO | VINTAGE2D_RESET_TLB, v2d_device->bar0 + VINTAGE2D_RESET);
    iowrite32(v2d_device->dma_cmd_buf, v2d_device->bar0 + VINTAGE2D_CMD_READ_PTR);
    iowrite32(v2d_device->dma_cmd_buf, v2d_device->bar0 + VINTAGE2D_CMD_WRITE_PTR);
    iowrite32(V2D_ALL_INTRS, v2d_device->bar0 + VINTAGE2D_INTR);

    ret = request_irq(pci_dev->irq, v2d_irq, IRQF_SHARED, "v2d", v2d_device);
    if (IS_ERR_VALUE(ret)) {
        printk(KERN_ERR V2D_PREFIX "request_irq returned %d\n", ret);
        goto err_request_irq;
    }

    spin_lock(&v2d_idr_lock);
    minor = idr_alloc(&v2d_idr, v2d_device, 0, V2D_MAX_DEV_COUNT, GFP_KERNEL);
    spin_unlock(&v2d_idr_lock);
    if (IS_ERR_VALUE(minor)) {
        ret = minor;
        printk(KERN_ERR V2D_PREFIX "idr_alloc returned %d\n", ret);
        goto err_idr_alloc;
    }

    ret = kobject_init_and_add(&v2d_device->kobject, &v2d_device_kobj_type, &pci_dev->dev.kobj, "vintage2d");
    if (IS_ERR_VALUE(ret)) {
        printk(KERN_ERR V2D_PREFIX "kobject_init_and_add returned %d\n", ret);
        goto err_kobject_init_and_add;
    }

    ret = cdev_add(&v2d_device->cdev, v2d_dev_base + minor, 1);
    if (IS_ERR_VALUE(ret)) {
        printk(KERN_ERR V2D_PREFIX "cdev_add returned %d\n", ret);
        goto err_cdev_add;
    }

    ret = kobject_add(&v2d_device->cdev.kobj, &v2d_device->kobject, "chardev");
    if (IS_ERR_VALUE(ret)) {
        printk(KERN_ERR V2D_PREFIX "kobject_add returned %d\n", ret);
        goto err_kobject_add;
    }

    device = device_create(v2d_class, &pci_dev->dev, v2d_device->cdev.dev, v2d_device, "v2d%d", minor);
    if (IS_ERR(device)) {
        ret = PTR_ERR(device);
        printk(KERN_ERR V2D_PREFIX "device_create returned %d\n", ret);
        goto err_device_create;
    }

    pci_set_drvdata(pci_dev, v2d_device);

    iowrite32(VINTAGE2D_ENABLE_DRAW | VINTAGE2D_ENABLE_FETCH_CMD, v2d_device->bar0 + VINTAGE2D_ENABLE);
    return 0;

err_device_create:
    kobject_del(&v2d_device->cdev.kobj);
err_kobject_add:
    cdev_del(&v2d_device->cdev);
err_cdev_add:
    kobject_del(&v2d_device->kobject);
err_kobject_init_and_add:
    spin_lock(&v2d_idr_lock);
    idr_remove(&v2d_idr, minor);
    spin_unlock(&v2d_idr_lock);
err_idr_alloc:
    free_irq(pci_dev->irq, v2d_device);
err_request_irq:
    dma_pool_free(v2d_device->dma_pool, v2d_device->cmd_buf, v2d_device->dma_cmd_buf);
err_v2d_alloc_page:
    dma_pool_destroy(v2d_device->dma_pool);
err_dma_pool_create:
err_dma_set_mask_and_coherent:
    pci_iounmap(pci_dev, v2d_device->bar0);
err_pci_iomap:
    pci_release_regions(pci_dev);
err_pci_request_regions:
    pci_disable_device(pci_dev);
err_pci_enable_device:
    kfree(v2d_device);
    return ret;
}

static void v2d_remove(struct pci_dev *pci_dev) {
    struct v2d_device *v2d_device;

    v2d_device = pci_get_drvdata(pci_dev);
    BUG_ON(!v2d_device);

    /* Do not let anybody else open the device */
    device_destroy(v2d_class, v2d_device->cdev.dev);
    cdev_del(&v2d_device->cdev);
    kobject_del(&v2d_device->kobject);

    /* Wait for the current users to exit */
    kobject_put(&v2d_device->kobject);
    down(&v2d_device->release_semaphore);
    kobject_del(&v2d_device->cdev.kobj);

    iowrite32(0, v2d_device->bar0 + VINTAGE2D_ENABLE);
    iowrite32(0, v2d_device->bar0 + VINTAGE2D_INTR_ENABLE);

    tasklet_kill(&v2d_device->tasklet);

    spin_lock(&v2d_idr_lock);
    idr_remove(&v2d_idr, MINOR(v2d_device->cdev.dev));
    spin_unlock(&v2d_idr_lock);
    free_irq(pci_dev->irq, v2d_device);
    dma_pool_free(v2d_device->dma_pool, v2d_device->cmd_buf, v2d_device->dma_cmd_buf);
    dma_pool_destroy(v2d_device->dma_pool);
    pci_iounmap(pci_dev, v2d_device->bar0);
    pci_release_regions(pci_dev);
    pci_disable_device(pci_dev);
    kfree(v2d_device);
}

DEFINE_PCI_DEVICE_TABLE(v2d_id_table) = {{PCI_DEVICE(VINTAGE2D_VENDOR_ID, VINTAGE2D_DEVICE_ID)}, {0}};

static struct pci_driver v2d_driver = {
    .name = "vintage2d", .id_table = v2d_id_table, .probe = v2d_probe, .remove = v2d_remove};

static int __init v2ddrv_init(void) {
    int ret;

    v2d_class = class_create(THIS_MODULE, "v2d");
    if (IS_ERR(v2d_class)) {
        return PTR_ERR(v2d_class);
    }

    ret = alloc_chrdev_region(&v2d_dev_base, 0, V2D_MAX_DEV_COUNT, "v2d");
    if (IS_ERR_VALUE(ret)) {
        goto err_alloc_chrdev_region;
    }

    ret = pci_register_driver(&v2d_driver);
    if (IS_ERR_VALUE(ret)) {
        goto err_pci_register_driver;
    }

    return 0;

err_pci_register_driver:
    unregister_chrdev_region(v2d_dev_base, V2D_MAX_DEV_COUNT);
err_alloc_chrdev_region:
    class_destroy(v2d_class);

    return ret;
}

static void __exit v2ddrv_exit(void) {
    pci_unregister_driver(&v2d_driver);
    unregister_chrdev_region(v2d_dev_base, V2D_MAX_DEV_COUNT);
    class_destroy(v2d_class);
}

module_init(v2ddrv_init);
module_exit(v2ddrv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Krzysztof Pszeniczny");
MODULE_DESCRIPTION("A vintage2d device driver");
