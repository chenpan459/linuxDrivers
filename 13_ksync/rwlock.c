/*读写锁（rwlock）是一种用于保护共享资源的同步机制，允许多个读者同时访问资源，但写者独占访问资源。读写锁适用于读多写少的场景。*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/rwlock.h>

static struct task_struct *reader_thread1;
static struct task_struct *reader_thread2;
static struct task_struct *writer_thread;
static rwlock_t my_rwlock;
static int shared_data = 0;

static int reader_fn(void *data) {
    printk(KERN_INFO "Reader thread started\n");

    while (!kthread_should_stop()) {
        read_lock(&my_rwlock);
        printk(KERN_INFO "Reader thread read shared_data: %d\n", shared_data);
        read_unlock(&my_rwlock);
        msleep(1000); // 模拟一些工作
    }

    printk(KERN_INFO "Reader thread stopping\n");
    return 0;
}

static int writer_fn(void *data) {
    printk(KERN_INFO "Writer thread started\n");

    while (!kthread_should_stop()) {
        write_lock(&my_rwlock);
        shared_data++;
        printk(KERN_INFO "Writer thread incremented shared_data to %d\n", shared_data);
        write_unlock(&my_rwlock);
        msleep(2000); // 模拟一些工作
    }

    printk(KERN_INFO "Writer thread stopping\n");
    return 0;
}

static int __init rwlock_example_init(void) {
    printk(KERN_INFO "Initializing rwlock example module\n");

    // 初始化读写锁
    rwlock_init(&my_rwlock);

    // 创建读者线程1
    reader_thread1 = kthread_run(reader_fn, NULL, "reader_thread1");
    if (IS_ERR(reader_thread1)) {
        printk(KERN_ERR "Failed to create reader thread 1\n");
        return PTR_ERR(reader_thread1);
    }

    // 创建读者线程2
    reader_thread2 = kthread_run(reader_fn, NULL, "reader_thread2");
    if (IS_ERR(reader_thread2)) {
        printk(KERN_ERR "Failed to create reader thread 2\n");
        kthread_stop(reader_thread1);
        return PTR_ERR(reader_thread2);
    }

    // 创建写者线程
    writer_thread = kthread_run(writer_fn, NULL, "writer_thread");
    if (IS_ERR(writer_thread)) {
        printk(KERN_ERR "Failed to create writer thread\n");
        kthread_stop(reader_thread1);
        kthread_stop(reader_thread2);
        return PTR_ERR(writer_thread);
    }

    return 0;
}

static void __exit rwlock_example_exit(void) {
    printk(KERN_INFO "Exiting rwlock example module\n");

    // 停止读者线程1
    if (reader_thread1) {
        kthread_stop(reader_thread1);
    }

    // 停止读者线程2
    if (reader_thread2) {
        kthread_stop(reader_thread2);
    }

    // 停止写者线程
    if (writer_thread) {
        kthread_stop(writer_thread);
    }
}

module_init(rwlock_example_init);
module_exit(rwlock_example_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple example of using rwlock to notify threads in the Linux kernel");