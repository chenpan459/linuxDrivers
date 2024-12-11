#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <linux/ioctl.h>
#include <asm/gpio.h>
#include <mach/soc.h>
#include <mach/platform.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/sched.h>
 
 
struct btn_dest{
	int gpio;//gpio端口号
	char *name;//名称
	char code;//键值(代表哪个按键)
};
 
//定义btn的硬件信息
struct btn_dest btn_info[] = {
	[0] = {
		.gpio = PAD_GPIO_A+28,
		.name = "K2",
		.code = 0x50,
	},
	[1] = {
		.gpio = PAD_GPIO_B+9,
		.name = "K6",
		.code = 0x60,
	},
	[2] = {
		.gpio = PAD_GPIO_B+30,
		.name = "K3",
		.code = 0x70,
	},
	[3] = {
		.gpio = PAD_GPIO_B+31,
		.name = "K4",
		.code = 0x80,
	}
};
 
//内核定时器
struct timer_list btn_timer;
//声明等待队列头
wait_queue_head_t wqh;
//代表键值和状态
char key = 0;
//按键事件发生标志
int flag = 0;//默认没有发生0-没有 1-发生
 
/*
inode是文件的节点结构，用来存储文件静态信息
文件创建时，内核中就会有一个inode结构
file结构记录的是文件打开的信息
文件被打开时内核就会创建一个file结构
*/
int btn_open(struct inode *inode, struct file *filp)
{
	printk("enter btn_open!\n");
 
	return 0;
}
 
ssize_t btn_read(struct file *filp, char __user *buf, size_t size, loff_t *offset)
{
	if(size!=1)
		return -EINVAL;
 
	//阻塞等待按键事件
	if(wait_event_interruptible(wqh, flag==1))
		return -EINTR;//被信号打断
 
	//上报键值和状态
	if(copy_to_user(buf, &key, size))
		return -EFAULT;
 
	//上报完数据flag清0
	flag = 0;
	
	return size;
}
 
int btn_release(struct inode *inode, struct file *filp)
{
	printk("enter btn_release!\n");
 
	return 0;
}
 
//声明操作函数集合
struct file_operations btn_fops = {
	.owner = THIS_MODULE,
	.open = btn_open,
	.read = btn_read,
	.release = btn_release,//对应用户close接口
};
 
//分配初始化miscdevice
struct miscdevice btn_dev = {
	.minor = MISC_DYNAMIC_MINOR,//系统分配次设备号
	.name = "btn",//设备文件名
	.fops = &btn_fops,//操作函数集合
};
 
//超时处理函数--- 真实按键事件
void btn_timer_function(unsigned long data)
{
	struct btn_dest *pdata = (struct btn_dest *)data;//引脚数据
	
	//区分按下松开
	//设置键值和状态
	key = pdata->code|gpio_get_value(pdata->gpio);
 
	flag = 1;
	
	//唤醒睡眠的进程
	wake_up_interruptible(&wqh);
}
 
//中断处理函数
irqreturn_t btn_handler(int irq, void *dev_id)
{
	//设置超时处理函数的参数
	btn_timer.data = (unsigned long)dev_id;
	//重置定时器10ms超时，用作按键消抖
	mod_timer(&btn_timer, jiffies+msecs_to_jiffies(10));
	
	return IRQ_HANDLED;//处理成功
}
 
//加载函数
int btn_init(void)
{
	int ret,i,j;
 
	//注册miscdevice
	ret = misc_register(&btn_dev);
	printk("BTN\tinitialized\n");
 
	//申请中断
	for(i=0;i<ARRAY_SIZE(btn_info);i++){
		//申请中断
		ret = request_irq(gpio_to_irq(btn_info[i].gpio), //中断号
						btn_handler, //中断处理函数
						IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, //中断标志,包括触发方式----- 上升下降沿触发
						btn_info[i].name, //中断名称
						&btn_info[i]);//传递给中断处理函数的参数
		if(ret<0){
			printk("request_irq failed!\n");
			goto failure_request_irq;
		}
	}
 
	//初始化等待队列
	init_waitqueue_head(&wqh);
 
	//初始化定时器
	init_timer(&btn_timer);
	btn_timer.function = btn_timer_function;
 
	printk("btn init!\n");
 
	return 0;
 
failure_request_irq:
	for(j=0;j<i;j++){
		free_irq(gpio_to_irq(btn_info[j].gpio), &btn_info[j]);
	}
	misc_deregister(&btn_dev);
	return ret;
}
 
//卸载函数
void btn_exit(void)
{
	int i;
 
	del_timer(&btn_timer);
	
	//释放所有申请的中断
	for(i=0;i<ARRAY_SIZE(btn_info);i++){
		free_irq(gpio_to_irq(btn_info[i].gpio), &btn_info[i]);
	}
 
	//注销miscdevice	
	misc_deregister(&btn_dev);
}
 
//声明为模块的入口和出口
module_init(btn_init);
module_exit(btn_exit);
 
 
MODULE_LICENSE("GPL");//GPL模块许可证
MODULE_AUTHOR("xin");//作者
MODULE_VERSION("2.0");//版本
MODULE_DESCRIPTION("btn driver!");//描述信息