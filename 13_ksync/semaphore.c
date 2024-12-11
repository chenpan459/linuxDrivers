/*可以使用信号量（semaphore）来实现线程间的同步和通知。信号量是一种常用的同步机制，可以用于控制对共享资源的访问。*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/semaphore.h>

static struct task_struct *thread1;
static struct task_struct *thread2;
static struct semaphore sem;

static int thread_fn1(void *data) {
    printk(KERN_INFO "Thread 1 started\n");

    while (!kthread_should_stop()) {
        printk(KERN_INFO "Thread 1 waiting for semaphore\n");
        if (down_interruptible(&sem)) {
            printk(KERN_INFO "Thread 1 interrupted\n");
            break;
        }
        printk(KERN_INFO "Thread 1 acquired semaphore\n");
        msleep(1000); // 模拟一些工作
        printk(KERN_INFO "Thread 1 releasing semaphore\n");
        up(&sem);
        msleep(1000); // 模拟一些延迟
    }

    printk(KERN_INFO "Thread 1 stopping\n");
    return 0;
}

static int thread_fn2(void *data) {
    printk(KERN_INFO "Thread 2 started\n");

    while (!kthread_should_stop()) {
        printk(KERN_INFO "Thread 2 waiting for semaphore\n");
        if (down_interruptible(&sem)) {
            printk(KERN_INFO "Thread 2 interrupted\n");
            break;
        }
        printk(KERN_INFO "Thread 2 acquired semaphore\n");
        msleep(1000); // 模拟一些工作
        printk(KERN_INFO "Thread 2 releasing semaphore\n");
        up(&sem);
        msleep(1000); // 模拟一些延迟
    }

    printk(KERN_INFO "Thread 2 stopping\n");
    return 0;
}

static int __init semaphore_example_init(void) {
    printk(KERN_INFO "Initializing semaphore example module\n");

    // 初始化信号量，初始值为1
    sema_init(&sem, 1);

    // 创建线程1
    thread1 = kthread_run(thread_fn1, NULL, "thread1");
    if (IS_ERR(thread1)) {
        printk(KERN_ERR "Failed to create thread 1\n");
        return PTR_ERR(thread1);
    }

    // 创建线程2
    thread2 = kthread_run(thread_fn2, NULL, "thread2");
    if (IS_ERR(thread2)) {
        printk(KERN_ERR "Failed to create thread 2\n");
        kthread_stop(thread1);
        return PTR_ERR(thread2);
    }

    return 0;
}

static void __exit semaphore_example_exit(void) {
    printk(KERN_INFO "Exiting semaphore example module\n");

    // 停止线程1
    if (thread1) {
        kthread_stop(thread1);
    }

    // 停止线程2
    if (thread2) {
        kthread_stop(thread2);
    }
}

module_init(semaphore_example_init);
module_exit(semaphore_example_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple example of using semaphore to notify threads in the Linux kernel");