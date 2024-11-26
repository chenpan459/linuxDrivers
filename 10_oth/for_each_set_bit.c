#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>

#define BITMAP_SIZE 64

static int __init my_module_init(void)
{
    unsigned long bitmap[BITMAP_SIZE / BITS_PER_LONG] = {0};
    int bit;

    // 设置一些位
    set_bit(1, bitmap);
    set_bit(3, bitmap);
    set_bit(5, bitmap);
    set_bit(63, bitmap);

    printk(KERN_INFO "Bitmap: ");
    for_each_set_bit(bit, bitmap, BITMAP_SIZE) {
        printk(KERN_CONT "%d ", bit);
    }
    printk(KERN_CONT "\n");

    return 0;
}

static void __exit my_module_exit(void)
{
    printk(KERN_INFO "Module exiting\n");
}

module_init(my_module_init);
module_exit(my_module_exit);

MODULE_LICENSE("GPL");
//MODULE_LICENSE("GPL");
//MODULE_AUTHOR("Your Name");
//MODULE_DESCRIPTION("A simple example using for_each_set_bit");