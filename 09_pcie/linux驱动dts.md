# linux设备驱动：一站式解决probe不跑问题

## 什么是理想驱动

想成为一名合格的驱动工程师，最基本的一个条件是要有对于驱动代码的“审美能力”，那么首先要问自己一个问题：我们心目中理想的驱动是什么样子？而怎么样得到这个答案呢？正如鲁迅所说：Read the fucking source code!

### 大学中的驱动

我们先看一份这样的I2C设备驱动，相信大学中接触过单片机的同学都见过这样的代码：

```text
#define ABC_I2C_ADDR 0X10
#define ABC_IRQ 10

int adc_send(...)
{
    i2c_transfer(adapter, ABC_I2C_ADDR, msg, num);
}

int abc_init(...)
{
    request_irq(ABC_IRQ);
}
```

**大家可以思考一下如果代码这样写会有什么问题？**
大家可以站在IC厂商的角度思考这个问题，作为一个IC厂，你是有很多客户的很多项目的，你也不可能只生产一款IC，那么这就带来了一个问题，你的I2C地址不同型号之间可能是不同的，甚至同一个型号IC的I2C地址也可能根据客户的要求做客制化，中断号就更不用说了，不同客户需要的中断号基本肯定是不一样的。那么如果你在这份驱动的基础上要兼容一个新项目势必会写出这样的代码：

```text
#ifdef BOARD_A
#define ABC_I2C_ADDR 0X10
#define ABC_IRQ 10

#elif defined(BOARD_B)
#define ABC_I2C_ADDR 0X10
#define ABC_IRQ 11

int adc_send(...)
{
    i2c_transfer(adapter, ABC_I2C_ADDR, msg, num);
}

int abc_init(...)
{
    request_irq(ABC_IRQ);
}
```

难道每多一个项目你都要这么砌一次砖么？有的人可能会说：我可以把宏定义都放在头文件里，这样逻辑代码就不会每次都改了。那么接着思考下面这种情况：在同一个主板上要同时驱动两个I2C器件该怎么办？难道要这样写代码么？那三个器件，四个器件呢？

```text
#ifdef BOARD_A
#define ABC_I2C_ADDR1 0X10
#define ABC_IRQ1 10
#define ABC_I2C_ADDR2 0X11
#define ABC_IRQ2 12

#elif defined(BOARD_B)
#define ABC_I2C_ADDR 0X10
#define ABC_IRQ 11

int adc_send(...)
{
    i2c_transfer(adapter, ABC_I2C_ADDR1, msg, num);
    i2c_transfer(adapter, ABC_I2C_ADDR2, msg, num);
}

int abc_init(...)
{
    request_irq(ABC_IRQ1);
    request_irq(ABC_IRQ2);
}
```

### 问题出在哪里？

看完了上面两个例子，你相信已经对什么是理想驱动有了一个初步的答案。linux中其实没有什么驱动移植的概念，一份好的驱动天然就应该是跨平台的，拿I2C设备举例，你的接收和发送数据， 数据的处理，对于中断的响应，这些逻辑天生其实就和硬件平台没有关系，你兼容一个项目其实理论上是不需要动到这些逻辑代码的，我们应该把硬件相关的信息剔除出去，实现代码的“高内聚，低耦合”，如下图：

![img](linux驱动dts.assets/v2-369480360ea58ae66bce7a375116d5eb_720w.webp)

![img](linux驱动dts.assets/v2-5ef6bc83416c7ab74d6d99462ab1c665_720w.webp)

### 总线设备驱动模型

现在我们把硬件信息从驱动中分离出去了，那么问题来了，在驱动代码实际跑起来的时候我们总归还是要获取这些硬件信息的，应该怎么获取？像下图一样，让驱动挨个问设备你的地址是多少，你的中断号是多少么？

![img](linux驱动dts.assets/v2-22f9aa7b7e575ff1f3616a57597099e6_720w.webp)


我们其实希望实现一种匹配机制，让驱动清楚地知道我是驱动谁的，让设备知道我是被谁驱动的，总线设备驱动模型就应运而生了：

![img](linux驱动dts.assets/v2-4f9820656cd16574f7077acdd0d929bd_720w.webp)


这种匹配机制就是"虚拟总线" **platform bus**

我们看一下引入总线设备驱动模型之后的驱动代码：

```text
static struct resource dm9000_resource1[] = {
       {
              .start = 0x20100000,
              .end   = 0x20100000 + 1,
              .flags = IORESOURCE_MEM
       },{
              .start = 0x20100000 + 2,
              .end   = 0x20100000 + 3,
              .flags = IORESOURCE_MEM
       },{
              .start = IRQ_PF15,
              .end   = IRQ_PF15,
              .flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE
       }
};

static struct resource dm9000_resource2[] = {
       {
              .start = 0x20200000,
              .end   = 0x20200000 + 1,
              .flags = IORESOURCE_MEM
       }…
};

…
static struct platform_device dm9000_device1 = {
       .name           = "dm9000",
       .id             = 0,
       .num_resources  = ARRAY_SIZE(dm9000_resource1),
       .resource       = dm9000_resource1,
};

…
static struct platform_device dm9000_device2 = {
       .name           = "dm9000",
       .id             = 1,
       .num_resources  = ARRAY_SIZE(dm9000_resource2),
       .resource       = dm9000_resource2,
};

static struct platform_device *ip0x_devices[] __initdata = {
       &dm9000_device1,
       &dm9000_device2,
…
};

static int __init ip0x_init(void)
{
       platform_add_devices(ip0x_devices, ARRAY_SIZE(ip0x_devices));
       …
}
```

### 为什么引入dts

当然了，引入总线设备驱动模型之后革命还没有完全胜利，要知道linux是一个操作系统，他是要兼容成百上千的硬件平台的，那么就会有成百上千这种描述硬件信息的.c文件，相当于device还是需要手动注册的，人们就想通过某种方式让系统自动注册device,于是就有了dts（设备树）。所以我们明白了dts的使命就是硬件信息的载体，扮演“总线设备驱动模型”中的设备这个角色。对于dts的介绍本文就不过多赘述，网上有很多介绍的资料：[http://www.wowotech.net/device_model/why-dt.html](https://link.zhihu.com/?target=http%3A//www.wowotech.net/device_model/why-dt.html)

## dts -> device

1. .dts文件和.dtsi文件通过编译生成dtbo.img最终烧录到系统的dtbo分区中，这个时候他只是扁平化的二进制文件，并没有形成C语言的数据结构。
2. 系统开机后会调用**unflatten_device_tree()**将扁平化的二进制数据解析成 struct device_node,具体的对应关系例子如下：

```text
&device_node1 {
    #address-cells = <1>;
    #size-cells = <0>;

    status = "ok";
    device_node2@2{
        compatible = "device_node1";
        reg = <0x2>;
        status = "ok";
        interrupt-parent = <&tlmm>;
        interrupts = <65 0x2>;
    };
    device_node3@3{
        compatible = "device_node2";
        reg = <0x3>;
        status = "ok";
        interrupt-parent = <&tlmm>;
        interrupts = <65 0x2>;
    };

};
```

会解析成：

![img](linux驱动dts.assets/v2-3b2350b57d4abe03e6b0acf032798282_720w.webp)


也就是说只要dts修改是生效的，那么一对大括号里面的内容就会生成一个**device_node**，注意是**device_node**并不是**device**,dts变成device_node是无条件的，但device_node需要满足一定的条件才可以变成device,而只有变成了device才有了和driver匹配的资格

### device_node -> device

函数调用如下：

```text
board_dt_populate
    |-of_platform_populate
        |-of_platform_bus_create
```

1. 将”/”也就是根节点下的有compatible属性的device_node注册成platform_device。
2. 在1的基础上，如果device_node的compatible属性是"simple-bus"，"simple-mfd"，"isa"，(关于这几个字符串随着内核升级可能会有变化，可以在源码中搜索 **of_default_bus_match_table** )则把他们的子节点也注册成platform device。
3. 在步骤2中由dts自动注册的**platform_device**就注册完了，在步骤2中注册的**platform_device**一般就对应我们实际使用的总线控制器例如i2c adapter,i2c adapter的子节点就是我们实际要驱动的i2c设备例如touch panel。

## 回到正题：如何排查probe函数不跑

**首先我们要清楚probe函数被调用的条件：**
\1. driver和device要在同一级总线下：这里说的同一级总线指的是platform bus或者i2c bus。例如platform device只会和 platform driver匹配，i2c driver只会和i2c device 匹配。 2. driver和device的**compatible**属性要相同。

**假如我们发现我们驱动的probe函数没有跑，我们该怎么办呢？**

\1. 确认device和driver是否都分别注册成功，怎么确认呢？用adb查看，目录是：**/sys/bus/…/driver or devices**

\2. 如果driver和device都存在，那么肯定是compatibal属性出了问题。

\3. 如果是driver不存在(当然这种情况很少，一般只要driver_register函数成功调用了，一般是在的)，那么修改DEFAULT_CONSOLE_LOGLEVEL等级，在log中搜索自己的driver名字。

\4. 如果是device不存在(大部分是这种情况)

4.1 确认自己的dts修改是否生效，例如高通会有好几份参考设计的dts，有可能你改的不是实际使用的那一份，怎么确认呢？目录在 **/sys/firmware/devicetree/base/**

4.2 如果是platform device，那么你只需要检查自己的dts节点是否是根目录的子节点，或者是 “simple-bus”这类节点的子节点

4.3 如果是i2c device这一类的device，那么就有两种情况，一种是总线节点就不存在，那么就是平台的总线驱动出了问题，一种是唯独你这个device没有注册出来，这种情况下在log中搜索对应的总线，例如spi,i2c都会看到一些蛛丝马迹。

参考：

[宋宝华：让天堂的归天堂，让尘土的归尘土——谈Linux的总线、设备、驱动模型](https://link.zhihu.com/?target=https%3A//mp.weixin.qq.com/s%3F__biz%3DMzAwMDUwNDgxOA%3D%3D%26mid%3D2652666359%26idx%3D1%26sn%3Deb9eec9db2ec0f43773dc7f777b83f80%26chksm%3D810f3f6ab678b67c9d6c98bb42af68b617c933d0e0ef114613e6262ae651c148316e928dcc2b%26mpshare%3D1%26scene%3D1%26srcid%3D0725ODtp4wYOKmBv08T7WkhL%26sharer_sharetime%3D1627216045091%26sharer_shareid%3Ddf13686831fea9c5cb927c067fbb536f%26version%3D3.1.10.3010%26platform%3Dwin%23rd)

[Device Tree（四）：文件结构解析](https://link.zhihu.com/?target=http%3A//www.wowotech.net/device_model/dt-code-file-struct-parse.html)



# Linux driver dts使用，实例驱动编写

Device Tree后，许多硬件的细节可以直接透过它传递给Linux，而不再需要在kernel中进行大量的冗余编码。
Device Tree由一系列被命名的结点（node）和属性（property）组成，而结点本身可包含子结点。所谓属性，其实就是成对出现的name和value。在Device Tree中，可描述的信息包括（原先这些信息大多被hard code到kernel中）：
CPU的数量和类别
内存基地址和大小
总线和桥
外设连接
中断控制器和中断使用情况
GPIO控制器和GPIO使用情况
Clock控制器和Clock使用情况

DTS (device tree source)
.dts文件是一种ASCII 文本格式的Device Tree描述，易于阅读。在ARM Linux系统一个.dts文件对应一个ARM的machine，一般放置在内核的arch/arm/boot/dts/目录。由于一个SoC可能对应多个machine（一个SoC可以对应多个产品和电路板），势必这些.dts文件需包含许多共同的部分，Linux内核为了简化，把SoC公用的部分或者多个machine共同的部分一般提炼为.dtsi，类似于C语言的头文件。其他的machine对应的.dts就include这个.dtsi。譬如，对于VEXPRESS而言，vexpress-v2m.dtsi就被vexpress-v2p-ca9.dts所引用， vexpress-v2p-ca9.dts有如下一行：
/include/ “vexpress-v2m.dtsi”，.dtsi也可以include其他的.dtsi，譬如几乎所有的ARM SoC的.dtsi都引用了skeleton.dtsi。
.dts（或者其include的.dtsi）基本元素即为前文所述的结点和属性：

本文以Amlogic S905D平台
dts文件引用的头文件：
release_n_7.1_20170804\common\arch\arm\boot\dts\include\dt-bindings
|->clk
|->clock
|->dma
|->gpio
|->input
|->interrupt-controller
|->mfd
|->pinctrl
|->pwm
|->reset
|->sound
|->thermal

解析dts文件的相关函数，定义在of.h文件里：
release_n_7.1_20170804\common\include\linux\of.h

常用函数原型：

```c
static inline bool of_property_read_bool(const struct device_node *np, 
                        const char *propname)

static inline int of_property_read_u8(const struct device_node *np, 
                        const char *propname, u8 *out_value)

static inline int of_property_read_u16(const struct device_node *np, 
                        const char *propname, u16 *out_value)

static inline int of_property_read_u32(const struct device_node *np, 
                        const char *propname, u32 *out_value)

static inline int of_property_read_string_array(struct device_node *np,
                        const char *propname, const char **out_strs,
                        size_t sz)

static inline int of_property_count_strings(struct device_node *np,
                        const char *propname)

static inline int of_property_read_string_index(struct device_node *np,
                        const char *propname,
                        int index, const char **output)

```

#### dts文件添加node:

```c
//dts文件以“/”为根目录，以树形展开，要注意思大括号的匹配。
/* add by song for dvb widgets */
    dvb_widgets {
        compatible = "amlogic, dvb_widgets"; //platform_driver 指定的匹配表。
        status = "okay"; //设备状态
        dw_name = "dvb-widgets"; //字符串属性
        dw_num = <8>; //数值属性
        ant_power-gpio = <&gpio  GPIODV_15  GPIO_ACTIVE_HIGH>; //gpio 描述
        loops-gpio = <&gpio  GPIODV_13  GPIO_ACTIVE_HIGH>; //gpio 描述
        ch34-gpio = <&gpio  GPIODV_12  GPIO_ACTIVE_HIGH>; //gpio 描述
    };
```

#### 驱动demo

```c

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/device.h>

#include <asm/irq.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <uapi/linux/input.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/amlogic/gpio-amlogic.h>
#include <linux/amlogic/sd.h>
#include <linux/amlogic/iomap.h>
#include <dt-bindings/gpio/gxbb.h>
#include <linux/amlogic/cpu_version.h>
#include <linux/amlogic/pm.h>
#include <linux/of_address.h>

struct dw_dev {
    unsigned int ant_power_pin;
    unsigned int ant_overload_pin;
    unsigned int loops_pin;
    unsigned int ch3_4_pin;
};

struct dw_dev pdw_dev;


static int dvb_widgets_suspend(struct platform_device *pdev,  pm_message_t pm_status)
{
    pr_dbg("%s\n",__FUNCTION__);
    return 0;
}

static int dvb_widgets_resume(struct platform_device *pdev)
{
    pr_dbg("%s\n",__FUNCTION__);
    return 0;
}

static void dvb_widgets_shutdown(struct platform_device *pdev)
{
    pr_dbg("%s\n",__FUNCTION__);
}


static int dvb_widgets_remove(struct platform_device *pdev)
{
    pr_dbg("%s\n",__FUNCTION__);

#ifdef D_SUPPORT_CLASS_INTERFACE    
    class_unregister(&dvb_widgets_class);
#endif

    return 0;
}

static int dvb_widgets_probe(struct platform_device *pdev)
{
    const char *str = NULL；
    int dw_num = 0;
    bool ant_power_overload_one_ping;
    int error = -EINVAL;

    pr_dbg("%s\n",__FUNCTION__);

    //判断节点是否存在
    if (!pdev->dev.of_node) {
        pr_dbg("dvb_widgets pdev->dev.of_node is NULL!\n");
        error = -EINVAL;
        goto get_node_fail;
    }

    // read string
    error = of_property_read_string(pdev->dev.of_node, "dw_name", &str);
    pr_dbg("dw_name:%s\n",str);

    // read u32
    error = of_property_read_u32(pdev->dev.of_node, "dw_num", &dw_num);
    if (error) {
        pr_err("Filed to get  dw_num\n");
    }else{
        pr_dbg("dw_num:%d\n", dw_num);
    }

    // read bool 如果dts文件定义的有ant_power_overload_one_ping此属性:true
    ant_power_overload_one_ping = of_property_read_bool(pdev->dev.of_node, "ant_power_overload_one_ping");
    if (ant_power_overload_one_ping) {
        pr_dbg("ant_power_overload_one_ping : true\n");
    } else {
        pr_dbg("ant_power_overload_one_ping : false\n");
    }


#ifdef CONFIG_OF
    //GPIO读取
    error = of_property_read_string(pdev->dev.of_node, "loops-gpio", &str);
    if (!error) {
        pdw_dev.loops_pin =
            desc_to_gpio(of_get_named_gpiod_flags(pdev->dev.of_node,
                              "loops-gpio", 0, NULL));
        pr_dbg("%s: %s\n", "loops-gpio", str);
    } else {
        pdw_dev.loops_pin = -1;
        pr_dbg("cannot find loops-gpio \n");
    }

    error = of_property_read_string(pdev->dev.of_node, "ch34-gpio", &str);
    if (!error) {
        pdw_dev.ch3_4_pin = 
            desc_to_gpio(of_get_named_gpiod_flags(pdev->dev.of_node,
                            "ch34-gpio", 0, NULL));
        pr_dbg("%s: %s\n", "ch34-gpio", str);
    } else {
        pdw_dev.ch3_4_pin = -1;
        pr_dbg("cannot find ch3_4_gpio\n");
    }

    error = of_property_read_string(pdev->dev.of_node, "ant_power-gpio", &str);
    if (!error) {
        pdw_dev.ant_power_pin =
            desc_to_gpio(of_get_named_gpiod_flags(pdev->dev.of_node,
                            "ant_power-gpio", 0, NULL));
        pr_dbg("ant_power-gpio\n");
    } else {
        pdw_dev.ant_power_pin = -1;
        pr_dbg("cannt find ant_power-gpio\n");
    }

    //申请GPIO
    gpio_request(pdw_dev.ant_power_pin, MODULE_NAME);
    gpio_request(pdw_dev.loops_pin, MODULE_NAME);
    gpio_request(pdw_dev.ch3_4_pin, MODULE_NAME);

    //设置GPIO 上拉状态
    gpio_set_pullup(pdw_dev.ant_power_pin, 1);
    gpio_set_pullup(pdw_dev.loops_pin, 1);
    gpio_set_pullup(pdw_dev.ch3_4_pin, 1);

    //设置GPIO输出电平
    gpio_direction_output(pdw_dev.ant_power_pin, 1); //输出高电平
    gpio_direction_output(pdw_dev.loops_pin, 0); //输出低电平
    gpio_direction_output(pdw_dev.ch3_4_pin, 0); //输出低电平

#else
#endif

    return 0;

get_node_fail:  
    return error;
}




#ifdef CONFIG_OF 
static const struct of_device_id dvb_widgets_match[] = {
    { .compatible  = "amlogic, dvb_widgets"},
    {},
};
#else
#define dvb_widgets_match NULL
#endif


static struct platform_driver dvb_widgets_driver = {
    .probe = dvb_widgets_probe,
    .remove = dvb_widgets_remove,
    .shutdown = dvb_widgets_shutdown,
    .suspend = dvb_widgets_suspend,
    .resume = dvb_widgets_resume,
    .driver = { 
        .name = MODULE_NAME, //设备驱动名
        .of_match_table =   dvb_widgets_match,  
    },
};

static int __init dvb_widgets_init(void)
{
    pr_dbg("%s\n",__FUNCTION__);
    return platform_driver_register(&dvb_widgets_driver);
}

static void __exit dvb_widgets_exit(void)
{
    pr_dbg("%s\n",__FUNCTION__);
    platform_driver_unregister(&dvb_widgets_driver);
}

module_init(dvb_widgets_init);
module_exit(dvb_widgets_exit);


MODULE_AUTHOR("Song YuLong");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AML DVB Widgets Driver.");
MODULE_AUTHOR("GOOD, Inc.");

```



# Linux驱动开发 - Linux 设备树学习 - DTS语法

## 1 什么是设备树？

设备树(Device Tree)，将这个词分开就是“设备”和“树”，描述设备树的文件叫做 DTS(DeviceTree Source)，这个 DTS 文件采用树形结构描述板级设备，也就是开发板上的设备信息，比如CPU 数量、 内存基地址、 IIC 接口上接了哪些设备、 SPI 接口上接了哪些设备等等

![img](linux驱动dts.assets/v2-eb72cdca33713d6683c31745bd8e423e_720w.webp)

在图中，树的主干就是系统总线，IIC 控制器、 GPIO 控制器、 SPI 控制器等都是接到系统主线上的分支。IIC 控制器有分为 IIC1 和 IIC2 两种，其中 IIC1 上接了 FT5206 和 AT24C02这两个 IIC 设备， IIC2 上只接了 MPU6050 这个设备。

## 2 DTS、DTB和DTC

- 设备树源文件扩展名为.dts，在移植 Linux 的时候却一直在使用.dtb 文件，那么 DTS 和 DTB 这两个文件是什么关系呢？ **DTS 是设备树源码文件， DTB 是将DTS 编译以后得到的二进制文件。**
- 将.c 文件编译为.o 需要用到 gcc 编译器，那么将.dts 编译为.dtb需要什么工具呢？需要用到 **DTC 工具**！ DTC 工具源码在 Linux 内核的 scripts/dtc 目录下，scripts/dtc/Makefile 文件内容如下：

```text
hostprogs-y	:= dtc
always		:= $(hostprogs-y)

dtc-objs	:= dtc.o flattree.o fstree.o data.o livetree.o treesource.o \
		   srcpos.o checks.o util.o
dtc-objs	+= dtc-lexer.lex.o dtc-parser.tab.o
```

可以看出，DTC 工具依赖于 dtc.c、 flattree.c、 fstree.c 等文件，最终编译并链接出 DTC 这个主机文件。如果要编译 DTS 文件的话只需要进入到 Linux 源码根目录下，然后执行如下命令：

```text
make all
```

或者：

```text
make dtbs
```

“make all”命令是编译 Linux 源码中的所有东西，包括 zImage， .ko 驱动模块以及设备树，如果只是编译设备树的话建议使用“make dtbs”命令。

基于 ARM 架构的 SOC 有很多种，一种 SOC 又可以制作出很多款板子，每个板子都有一个对应的 DTS 文件，那么如何确定编译哪一个 DTS 文件呢？以 I.MX6ULL 这款芯片对应的板子为例来看一下，打开 arch/arm/boot/dts/Makefile，有如下内容：

```text
dtb-$(CONFIG_SOC_IMX6UL) += \
	imx6ul-14x14-ddr3-arm2.dtb \
	imx6ul-14x14-ddr3-arm2-emmc.dtb	\
...
dtb-$(CONFIG_SOC_IMX6ULL) += \
...
	imx6ull-14x14-nand-4.3-480x272-c.dtb \
	imx6ull-14x14-nand-vga.dtb \
	imx6ull-14x14-nand-hdmi.dtb \
	imx6ull-alientek-emmc.dtb \
	imx6ull-alientek-nand.dtb \
	imx6ull-14x14-evk-usb-certi.dtb \
	imx6ull-9x9-evk.dtb \
...
dtb-$(CONFIG_SOC_IMX6SLL) += \
	imx6sll-lpddr2-arm2.dtb \
	imx6sll-lpddr3-arm2.dtb \
...
```

当选 中 I.MX6ULL 这个 SOC 以后(CONFIG_SOC_IMX6ULL=y)，所有使用到I.MX6ULL 这个 SOC 的板子对应的.dts 文件都会被编译为.dtb。如果我们使用 I.MX6ULL 新做了一个板子，只需要新建一个此板子对应的.dts 文件，然后将对应的.dtb 文件名添加到 dtb-

$(CONFIG_SOC_IMX6ULL)下，这样在编译设备树的时候就会将对应的.dts 编译为二进制的.dtb文件。



## 3 DTS 语法

### 3.1 dtsi 头文件

和 C 语言一样，设备树也支持头文件，设备树的头文件扩展名为.dtsi。在 imx6ull-alientekemmc.dts 中有如下所示内容：

```text
#include <dt-bindings/input/input.h>
#include "imx6ull.dtsi"
```

在.dts 设备树文件中，可以通过“#include”来引用.h、 .dtsi 和.dts 文件。



一般.dtsi 文件用于描述 SOC 的内部外设信息，比如 CPU 架构、主频、外设寄存器地址范围，比如 UART、 IIC 等等。比如 imx6ull.dtsi 就是描述 I.MX6ULL 这颗 SOC 内部外设情况信息的，内容如下：

```text
#include <dt-bindings/clock/imx6ul-clock.h>
#include <dt-bindings/gpio/gpio.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>
#include "imx6ull-pinfunc.h"
#include "imx6ull-pinfunc-snvs.h"
#include "skeleton.dtsi"

/ {
	aliases {
		can0 = &flexcan1;
		can1 = &flexcan2;
......
	};

	cpus {
		#address-cells = <1>;
		#size-cells = <0>;

		cpu0: cpu@0 {
			compatible = "arm,cortex-a7";
			device_type = "cpu";
......
		};
	};

	intc: interrupt-controller@00a01000 {
		compatible = "arm,cortex-a7-gic";
		#interrupt-cells = <3>;
		interrupt-controller;
		reg = <0x00a01000 0x1000>,
		      <0x00a02000 0x100>;
	};

	clocks {
		#address-cells = <1>;
		#size-cells = <0>;

		ckil: clock@0 {
			compatible = "fixed-clock";
			reg = <0>;
			#clock-cells = <0>;
			clock-frequency = <32768>;
			clock-output-names = "ckil";
		};
......
	};

	soc {
		#address-cells = <1>;
		#size-cells = <1>;
		compatible = "simple-bus";
		interrupt-parent = <&gpc>;
		ranges;

		busfreq {
			compatible = "fsl,imx_busfreq";
......
		};

		pmu {
			compatible = "arm,cortex-a7-pmu";
			interrupts = <GIC_SPI 94 IRQ_TYPE_LEVEL_HIGH>;
			status = "disabled";
		};
```

cpu0 这个设备节点信息，这个节点信息描述了I.MX6ULL 这颗 SOC 所使用的 CPU 信息，比如架构是 cortex-A7，频率支持 996MHz、 792MHz、528MHz、396MHz 和 198MHz 等等。在 imx6ull.dtsi 文件中不仅仅描述了 cpu0 这一个节点信息，I.MX6ULL 这颗 SOC 所有的外设都描述的清清楚楚，比如 ecspi1~4、 uart1~8、 usbphy1~2、 i2c1~4等等

### 3.2 设备节点

设备树是采用树形结构来描述板子上的设备信息的文件，每个设备都是一个节点，叫做设备节点，每个节点都通过一些属性信息来描述节点信息，属性就是键—值对。以下是从imx6ull.dtsi 文件中缩减出来的设备树文件内容：

```text
/ {
	aliases {
		can0 = &flexcan1;
	};

	cpus {
		#address-cells = <1>;
		#size-cells = <0>;

		cpu0: cpu@0 {
			compatible = "arm,cortex-a7";
			device_type = "cpu";
			reg = <0>;
		};
	};

	intc: interrupt-controller@00a01000 {
		compatible = "arm,cortex-a7-gic";
		#interrupt-cells = <3>;
		interrupt-controller;
		reg = <0x00a01000 0x1000>,
		      <0x00a02000 0x100>;
	};
}
```

**第 1 行**，“/”是根节点，每个设备树文件只有一个根节点。imx6ull.dtsi和 imx6ull-alientek-emmc.dts 这两个文件都有一个“/”根节点，这两个“/”根节点的内容会合并成一个根节点。

**第 2、 6 和 17 行**， aliases、 cpus 和 intc 是三个子节点，在设备树中节点命名格式如下：

```text
node-name@unit-address
```

其中“node-name”是节点名字，为 ASCII 字符串，节点名字应该能够清晰的描述出节点的功能，比如“uart1”就表示这个节点是 UART1 外设。“unit-address”一般表示设备的地址或寄存器首地址，如果某个节点没有地址或者寄存器的话“unit-address”可以不要，比如“cpu@0”、“interrupt-controller@00a01000”。

另外的命名方式，节点命名却如下所示：

```text
cpu0:cpu@0
```

上述命令并不是“node-name@unit-address”这样的格式，而是用“：”隔开成了两部分，“：”前面的是节点标签(label)，“：”后面的才是节点名字，格式如下所示：

```text
label: node-name@unit-address
```

**引入 label 的目的就是为了方便访问节点，可以直接通过&label 来访问这个节点，比如通过&cpu0 就可以访问“cpu@0”这个节点，而不需要输入完整的节点名字。**再比如节点 “intc:interrupt-controller@00a01000”，节点 label 是 intc，而节点名字就很长了，为“ interruptcontroller@00a01000”。很明显通过&intc 来访问“interrupt-controller@00a01000”这个节点要方便很多

**第 10 行， cpu0 也是一个节点，只是 cpu0 是 cpus 的子节点。 **

每个节点都有不同属性，不同的属性又有不同的内容，**属性都是键值对**，值可以为空或任意的字节流。**设备树源码中常用的几种数据形式如下所示：**

**①、字符串**

```text
compatible = "arm,cortex-a7";
```

上述代码设置 compatible 属性的值为字符串“arm,cortex-a7”

**②、 32 位无符号整数**

```text
reg = <0>;
```

上述代码设置 reg 属性的值为 0， reg 的值也可以设置为一组值，比如：

```text
reg = <0 0x123456 100>;
```

**③、字符串列表**

属性值也可以为字符串列表，字符串和字符串之间采用“,”隔开，如下所示：

```text
compatible = "fsl,imx6ull-gpmi-nand", "fsl, imx6ul-gpmi-nand";
```

上述代码设置属性 compatible 的值为“fsl,imx6ull-gpmi-nand”和“fsl, imx6ul-gpmi-nand”。

### 3.3 标准属性

**节点是由一堆的属性组成，节点都是具体的设备**，不同的设备需要的属性不同，用户可以自定义属性。除了用户自定义属性，有很多属性是标准属性， Linux 下的很多外设驱动都会使用这些标准属性，本节我们就来学习一下几个常用的标准属性。

1、compatible 属性

compatible 属性也叫做“兼容性”属性，这是非常重要的一个属性！compatible 属性的值是一个字符串列表， compatible 属性用于将设备和驱动绑定起来。字符串列表用于选择设备所要使用的驱动程序， compatible 属性的值格式如下所示：

```text
"manufacturer,model"
```

- **其中 manufacturer 表示厂商， model 一般是模块对应的驱动名字**。
- 比如 imx6ull-alientekemmc.dts 中 sound 节点是 I.MX6U-ALPHA 开发板的音频设备节点， I.MX6U-ALPHA 开发板上的音频芯片采用的欧胜(WOLFSON)出品的 WM8960， sound 节点的 compatible 属性值如下：

```text
compatible = "fsl,imx6ul-evk-wm8960","fsl,imx-audio-wm8960";
```

属性值有两个，分别为“fsl,imx6ul-evk-wm8960”和“fsl,imx-audio-wm8960”，其中“fsl”表示厂商是飞思卡尔，“imx6ul-evk-wm8960”和“imx-audio-wm8960”表示驱动模块名字。

sound这个设备首先使用第一个兼容值在 Linux 内核里面查找，看看能不能找到与之匹配的驱动文件，如果没有找到的话就使用第二个兼容值查。

一般驱动程序文件都会有一个**OF 匹配表**，此 OF 匹配表保存着一些 compatible 值，如果设备节点的 compatible 属性值和 OF 匹配表中的任何一个值相等，那么就表示设备可以使用这个驱动。比如在文件 imx-wm8960.c 中有如下内容

```text
tatic const struct of_device_id imx_wm8960_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-wm8960", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_wm8960_dt_ids);

static struct platform_driver imx_wm8960_driver = {
	.driver = {
		.name = "imx-wm8960",
		.pm = &snd_soc_pm_ops,
		.of_match_table = imx_wm8960_dt_ids,
	},
	.probe = imx_wm8960_probe,
	.remove = imx_wm8960_remove,
};
```

数组 imx_wm8960_dt_ids 就是 imx-wm8960.c 这个驱动文件的匹配表，此匹配表只有一个匹配值“fsl,imx-audio-wm8960”。如果在设备树中有哪个节点的 compatible 属性值与此相等，那么这个节点就会使用此驱动文件。

wm8960 采用了 platform_driver 驱动模式。此行设置.of_match_table 为 imx_wm8960_dt_ids，也就是设置这个 platform_driver 所使用的OF 匹配表。

2、model 属性

model 属性值也是一个字符串，一般 model 属性描述设备模块信息，比如名字什么的，比如：

```text
model = "wm8960-audio";
```

### 3、status 属性

**status 属性是和设备状态有关的**， status 属性值也是字符串，字符串是设备的状态信息，可选的状态如表

![img](linux驱动dts.assets/v2-68f4253326f8b97140e27dc1df4f92cb_720w.webp)

### 4、#address-cells 和#size-cells 属性

这两个属性的值都是无符号 32 位整形， #address-cells 和#size-cells 这两个属性可以用在任何拥有子节点的设备中，**用于描述子节点的地址信息。**

```text
#address-cells 属性值决定了子节点 reg 属性中地址信息所占用的字长(32 位)
#size-cells 属性值决定了子节点 reg 属性中长度信息所占的字长(32 位)。   
```

\#address-cells 和#size-cells 表明了子节点应该如何编写 reg 属性值，一般 reg 属性都是和地址有关的内容，和地址相关的信息有两种：**起始地址和地址长度， reg 属性的格式一为：**

```text
reg = <address1 length1 address2 length2 address3 length3……>
```

每个“address length”组合表示一个地址范围，其中**address 是起始地址**，**length 是地址长度**， #address-cells 表明 address 这个数据所占用的字长， #size-cells 表明 length 这个数据所占用的字长，比如:

```text
spi4 {
    compatible = "spi-gpio";
    #address-cells = <1>;
    #size-cells = <0>;

    gpio_spi: gpio_spi@0 {
        compatible = "fairchild,74hc595";
        reg = <0>;
    };
};

aips3: aips-bus@02200000 {
        compatible = "fsl,aips-bus", "simple-bus";
        #address-cells = <1>;
        #size-cells = <1>;

        dcp: dcp@02280000 {
            compatible = "fsl,imx6sl-dcp";
            reg = <0x02280000 0x4000>;
    };
};
```

- 第 3， 4 行，节点 spi4 的**#address-cells = <1>， #size-cells = <0>**，说明 spi4 的子节点 reg 属性中起始地址所占用的字长为 1，地址长度所占用的字长为 0。
- 第 8 行，子节点 gpio_spi: gpio_spi@0 的 reg 属性值为 <0>，因为父节点设置了#addresscells = <1>， #size-cells = <0>，因此 addres=0，没有 length 的值，相当于设置了起始地址，而没有设置地址长度。
- 第 14， 15 行，设置 aips3: aips-bus@02200000 节点#address-cells = <1>， #size-cells = <1>，说明 aips3: aips-bus@02200000 节点起始地址长度所占用的字长为 1，地址长度所占用的字长也为 1。
- 第 19 行，子节点 dcp: dcp@02280000 的 reg 属性值为<0x02280000 0x4000>，因为父节点设置了#address-cells = <1>， #size-cells = <1>， address= 0x02280000， length= 0x4000，相当于设置了起始地址为 0x02280000，地址长度为 0x40000。

### 5、reg 属性

reg 属性的值一般是(address， length)对。 **reg 属性一般用于描述设备地址空间资源信息，一般都是某个外设的寄存器地址范围信息，**比如在 imx6ull.dtsi 中有如下内容：

```text
uart1: serial@02020000 {
    compatible = "fsl,imx6ul-uart",
    	"fsl,imx6q-uart", "fsl,imx21-uart";
    reg = <0x02020000 0x4000>;
    interrupts = <GIC_SPI 26 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&clks IMX6UL_CLK_UART1_IPG>,
    	<&clks IMX6UL_CLK_UART1_SERIAL>;
    clock-names = "ipg", "per";
    status = "disabled";
};
```

上述代码是节点 uart1， uart1 节点描述了 I.MX6ULL 的 UART1 相关信息。其中 uart1 的父节点 aips1: aips-bus@02000000 设置了#address-cells = <1>、 #sizecells = <1>，因此 reg 属性中address=0x02020000， length=0x4000。

查阅《I.MX6ULL 参考手册》可知， I.MX6ULL 的 UART1 寄存器首地址为 0x02020000，但是 UART1 的地址长度(范围)并没有 0x4000 这么多，这里我们重点是获取 UART1 寄存器首地址。

6、ranges 属性

ranges属性值可以为空或者按照(child-bus-address,parent-bus-address,length)格式编写的数字矩阵， ranges 是一个地址映射/转换表， ranges 属性每个项目由子地址、父地址和地址空间长度这三部分组成：

- child-bus-address：子总线地址空间的物理地址，由父节点的#address-cells 确定此物理地址所占用的字长。
- parent-bus-address： 父总线地址空间的物理地址，同样由父节点的#address-cells 确定此物理地址所占用的字长。
- length： 子地址空间的长度，由父节点的#size-cells 确定此地址长度所占用的字长。

如果 ranges 属性值为空值，说明子地址空间和父地址空间完全相同，不需要进行地址转换 。

### 7、name 属性

name 属性值为字符串， **name 属性用于记录节点名字**， name 属性已经被弃用，不推荐使用name 属性，一些老的设备树文件可能会使用此属性。

### 8、device_type 属性

device_type 属性值为字符串，此属性只能用于 cpu 节点或者 memory 节点。imx6ull.dtsi 的 cpu0 节点用到了此属性，内容如下所示：

```text
cpu0: cpu@0 {
    compatible = "arm,cortex-a7";
    device_type = "cpu";
    reg = <0>;
```

## 4 向节点追加或修改内容

产品开发过程中可能面临着频繁的需求更改，比如第一版硬件上有一个 IIC 接口的六轴芯片 MPU6050，第二版硬件又要把这个 MPU6050 更换为 MPU9250 等。一旦硬件修改了，就要同步的修改设备树文件，毕竟设备树是描述板子硬件信息的文件。

假设现在有个六轴芯片fxls8471， fxls8471 要接到 I.MX6U-ALPHA 开发板的 I2C1 接口上，那么相当于需要在 i2c1 这个节点上添加一个 fxls8471 子节点。先看一下 I2C1 接口对应的节点，打开文件 imx6ull.dtsi 文件，找到如下所示内容：

```text
i2c1: i2c@021a0000 {
    #address-cells = <1>;
    #size-cells = <0>;
    compatible = "fsl,imx6ul-i2c", "fsl,imx21-i2c";
    reg = <0x021a0000 0x4000>;
    interrupts = <GIC_SPI 36 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&clks IMX6UL_CLK_I2C1>;
    status = "disabled";
};
```

现在要在 i2c1 节点下创建一个子节点，这个子节点就是 fxls8471，最简单的方法就是在 i2c1 下直接添加一个名为 fxls8471 的子节点，

如下所示：示

```text
i2c1: i2c@021a0000 {
    #address-cells = <1>;
    #size-cells = <0>;
    compatible = "fsl,imx6ul-i2c", "fsl,imx21-i2c";
    reg = <0x021a0000 0x4000>;
    interrupts = <GIC_SPI 36 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&clks IMX6UL_CLK_I2C1>;
    status = "disabled";
    
    //fxls8471 子节点
	fxls8471@1e {
        compatible = "fsl,fxls8471";
        reg = <0x1e>;
	};
};
```

但是这样会有个问题！ i2c1 节点是定义在 imx6ull.dtsi 文件中的，而 imx6ull.dtsi 是设备树头文件，其他所有使用到 I.MX6ULL这颗 SOC 的板子都会引用 imx6ull.dtsi 这个文件。直接在 i2c1 节点中添加 fxls8471 就相当于在其他的所有板子上都添加了 fxls8471 这个设备，样写肯定是不行的。

I.MX6U-ALPHA 开发板使用的设备树文件为 imx6ull-alientek-emmc.dts，因此我们需要在imx6ull-alientek-emmc.dts 文件中完成数据追加的内容，方式如下：

```text
&i2c1 {
	/* 要追加或修改的内容 */
};
```

&i2c1 表示要访问 i2c1 这个 label 所对应的节点，也就是 imx6ull.dtsi 中的“i2c1:i2c@021a0000”。

**花括号内就是要向 i2c1 这个节点添加的内容，包括修改某些属性的值。**打开 imx6ull-alientek-emmc.dts，找到如下所示内容：

```text
&i2c1 {
	clock-frequency = <100000>;
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_i2c1>;
	status = "okay";

	mag3110@0e {
		compatible = "fsl,mag3110";
		reg = <0x0e>;
		position = <2>;
	};

	fxls8471@1e {
		compatible = "fsl,fxls8471";
        reg = <0x1e>;
        position = <0>;
        interrupt-parent = <&gpio5>;
        interrupts = <0 8>;
    };
};
```

- 属性“clock-frequency”就表示 i2c1 时钟为 100KHz。“clock-frequency”就是新添加的属性。
- 将 status 属性的值由原来的 disabled 改为 okay。
- i2c1 子节点 mag3110，
- i2c1 子节点 fxls8471

这个就是向节点追加或修改内容，重点就是通过&label 来访问节点，然后直接在里面编写要追加或者修改的内容。

## 5 设备树在系统中的体现

Linux 内核启动的时候会解析设备树中各个节点的信息，并且在根文件系统的/proc/devicetree 目录下根据节点名字创建不同文件夹

![img](linux驱动dts.assets/v2-3c83702ba66ebb8435b8f3236ee43cf2_720w.webp)

目录/proc/device-tree 目录下的内容， /proc/device-tree 目录下是根节点“/”的所有属性和子节点

1、根节点“/”各个属性

根节点属性属性表现为一个个的文件，比如图 中“#address-cells”、“#size-cells”、“compatible”、“model”和“name”这 5 个文件，它们在设备树中就是根节点的 5个属性。既然是文件那么肯定可以查看其内容，输入 cat 命令来查看 model和 compatible 这两个文件的内容

![img](linux驱动dts.assets/v2-0743aebfba94a97600c88f022f11e720_720w.webp)

2、根节点“/”各子节点

各个文件夹就是根节点“/”的各个子节点，比如“aliases”、“ backlight”、“ chosen”和“ clocks”等等

/proc/device-tree 目录就是设备树在根文件系统中的体现，同入/proc/device-tree/soc 目录中就可以看到 soc 节点的所有子节点

![img](linux驱动dts.assets/v2-518230ef002a96019402f821cecde12a_720w.webp)

和根节点“/”一样，图中的所有文件分别为 soc 节点的属性文件和子节点文件夹。查看一下这些属性文件的内容是否和 imx6ull.dtsi 中 soc 节点的属性值相同，也可以进入“busfreq”这样的文件夹里面查看 soc 节点的子节点信息。

> 转载于：https://blog.csdn.net/kakaka666/article/details/130141252?spm=1001.2014.3001.5502

发布于 2023-06-25 22:21・IP 属地湖南



# linux 驱动probe 被调用流程分析

前言： 对于linux platform device 和driver，一个driver可对应多个device，通过名字进行匹配，调用驱动里边实现的probe函数，本文以一个i2c设备为例，从驱动的i2c_add_driver()开始看源码以及用比较笨的打log的方式分析如何一步一步调用的probe()函数。

分析的代码基于linux kernel msm-4.9。

/****************************************/

从module_init()开始看，

定义位置：kernel/msm-4.9/include/linux/module.h

源码里对该函数的说明:

```c
/** 
 * module_init() - driver initialization entry point 
 * @x: function to be run at kernel boot time or module insertion 
 * 
 * module_init() will either be called during do_initcalls() (if 
 * builtin) or at module insertion time (if a module).  There can only 
 * be one per module.    
 */  
```

作为每个驱动的入口函数，若代码有编译进kernel的话在开机kernel 启动（或模块被动态加载的时候）的时候被do_initcalls()调用,

从kernel如何一步一步走到do_initcalls()进而调用module_init(),调用链如下:

start_kernel();//kernel的第一个函数

rest_init();

kernel_init();

kernel_init_freeable();

do_basic_setup();

do_initcalls();//

do_initcall_level();

//......
module_init();
————————————————接着通过module_init()调用你在驱动里边实现的init函数:

对于一个i2c设备来说,做法如下，

```cpp
static struct i2c_driver your_driver = {  
    .driver = {  
        .owner  = THIS_MODULE,  
        .name   = YOUR_DRIVER_NAME_UP,  
        .of_match_table = your_driver_of_match,  
    },  
    .probe    = your_probe_func,  
    .remove  = your_i2c_drv_remove,  
    .id_table   = your_i2c_drv_id_table,  
    //.suspend  = your_i2c_drv_suspend,  
    //.resume      = your_i2c_drv_resume,  
};  
static int __int your_init_func(void)  
{  
    //…  
    i2c_add_driver(&your_i2c_driver);  
    //…  
}  
modul_init(your_init_func);  
```

将 your_probe_func()地址装进你实现的struct i2c_driver的.probe 成员，

接下来，从i2c_add_driver()如何调用到你的probe函数，调用链如下:

```c
i2c_register_driver();
driver_register();
bus_add_driver();
driver_attach();
__driver_attach (for your device);
driver_probe_device();
really_probe();
i2c_device_probe (this is what dev->bus->probe is for an i2c driver);
your_probe_func();
```

接下来一个一个函数分析他们到底干了什么:

先看i2c_register_driver()，

定义位置：kernel/msm-4.9/drivers/i2c/i2c-core.c

```c
int i2c_register_driver(struct module *owner, struct i2c_driver *driver)  
{  
    int res;  
  
    /* Can't register until after driver model init */  
    if (WARN_ON(!is_registered)) {  
        printk("unlikely(WARN_ON(!i2c_bus_type.p and return -EAGAIN.\n");  
        return -EAGAIN;  
    }  
          
  
    /* add the driver to the list of i2c drivers in the driver core */  
    driver->driver.owner = owner;  
    driver->driver.bus = &i2c_bus_type;//这边添加了bus type  
    INIT_LIST_HEAD(&driver->clients);  
  
    /* When registration returns, the driver core 
     * will have called probe() for all matching-but-unbound devices. 
     */  
    res = driver_register(&driver->driver);  //这边走进去
    if (res){  
        printk("driver_register return res : %d\n", res);  
        return res;  
    }  
    
    pr_debug("driver [%s] registered\n", driver->driver.name);  
    printk(KERN_INFO "======>lkhlog %s\n",driver->driver.name);    
  
    /* iWalk the adapters that are already present */  
    i2c_for_each_dev(driver, __process_new_driver);  
  
    return 0;  
}  
 
type定义如下:
struct bus_type i2c_bus_type = {  
    .name       = "i2c",  
    .match      = i2c_device_match,  
    .probe      = i2c_device_probe,//后面会回调这个函数，在上面的调用链里有提到  
    .remove     = i2c_device_remove,  
    .shutdown   = i2c_device_shutdown,  
};  
```

添加了bus_type,然后调用driver_register(),

接着看driver_register()，

定义位置：kernel/msm-4.9/drivers/base/driver.c

```c
/** 
 * driver_register - register driver with bus 注册总线驱动，和总线类型联系起来 
 * @drv: driver to register 
 * 
 * We pass off most of the work to the bus_add_driver() call, 
 * since most of the things we have to do deal with the bus 
 * structures. 
 */  
int driver_register(struct device_driver *drv)  
	{  
	    int ret;  
	    struct device_driver *other;  
	  
	    BUG_ON(!drv->bus->p);  
	    //……  
	    other = driver_find(drv->name, drv->bus);//确认该驱动是否已经注册过  
	    printk(KERN_WARNING "======>lkh driver_find, other : %d", other);  
	     //……  
	    ret = bus_add_driver(drv);//主要从这儿走进去  
	    printk(KERN_WARNING "======>lkh bus_add_driver, ret : %d", ret);  
	    if (ret)  
	        return ret;  
	    ret = driver_add_groups(drv, drv->groups);  
	    if (ret) {  
	        printk(KERN_WARNING "======>lkh bus_remove_driver");  
	        bus_remove_driver(drv);  
	        return ret;  
	    }  
	    kobject_uevent(&drv->p->kobj, KOBJ_ADD);  
	  
	    return ret;  
	} 
```

接着看 bus_add_driver(),

定义位置：kernel/msm-4.9/drivers/base/bus.c

```c
/** 
 * bus_add_driver - Add a driver to the bus. 
 * @drv: driver. 
 */  
int bus_add_driver(struct device_driver *drv)  
{  
  
  
    bus = bus_get(drv->bus);//重新获取到前边装进去的bus  
    if (!bus) {   
        printk(KERN_ERR "======>lkh return -EINVAL\n");  
        return -EINVAL;  
    }  
  
    printk(KERN_ERR "======>lkh bus_add_driver\n");  
  
    pr_debug("bus: '%s': add driver %s\n", bus->name, drv->name);  
  
    priv = kzalloc(sizeof(*priv), GFP_KERNEL);  
    if (!priv) {  
        error = -ENOMEM;  
        goto out_put_bus;  
    }  
    klist_init(&priv->klist_devices, NULL, NULL);  
    priv->driver = drv;//driver_private 里边指针指向device_driver  
    drv->p = priv;  
    //device_driver也有指针指向driver_private，这样就可通过其中一个获取到另外一个  
    priv->kobj.kset = bus->p->drivers_kset;  
    error = kobject_init_and_add(&priv->kobj, &driver_ktype, NULL,  
                     "%s", drv->name);  
    if (error)  
        goto out_unregister;  
  
    klist_add_tail(&priv->knode_bus, &bus->p->klist_drivers);  
    if (drv->bus->p->drivers_autoprobe) {  
        printk(KERN_ERR "======>lkh drv->bus->p->drivers_autoprobe == true, name : %s\n", drv->name);  
        if (driver_allows_async_probing(drv)) {  
            pr_debug("bus: '%s': probing driver %s asynchronously\n",  
                drv->bus->name, drv->name);  
            printk(KERN_ERR "======>lkh bus: '%s': probing driver %s asynchronously\n",  
                drv->bus->name, drv->name);  
            async_schedule(driver_attach_async, drv);  
        } else {  
            printk(KERN_ERR "======>lkh enter driver_attach, name : %s\n", drv->name);  
            error = driver_attach(drv);//这边走进去  
            printk(KERN_ERR "======>lkh driver_attach, error : %d\n", error);  
            if (error)  
                goto out_unregister;  
        }  
    }  
    printk(KERN_ERR "======>lkh bus_add_driver 2, name : %s \n", drv->name);  
    //若前边driver_attach()返回没有错误的话，  
    //这边会进去创建相关节点，链接  
    module_add_driver(drv->owner, drv);  
    //……
} 
```

```
接着看driver_attach(),直接看函数说明就能明白了，
定义位置：kernel/msm-4.9/drivers/base/dd.c
```

```c
/** 
 * driver_attach - try to bind driver to devices. 
 * @drv: driver. 
 * 
 * Walk the list of devices that the bus has on it and try to 
 * match the driver with each one.  If driver_probe_device() 
 * returns 0 and the @dev->driver is set, we've found a 
 * compatible pair. 
 */  
int driver_attach(struct device_driver *drv)  
{  
    return bus_for_each_dev(drv->bus, NULL, drv, __driver_attach);  
}  
```

```
在bus_for_each_dev()里边，将会遍历总线上的每个设备，并调用__driver_attach() 函数，如下：
定义位置：kernel/msm-4.9/drivers/base/bus.c
```

```c
/** 
 * bus_for_each_dev - device iterator. 
 * @bus: bus type. 
 * @start: device to start iterating from. 
 * @data: data for the callback. 
 * @fn: function to be called for each device. 
 * 
 * Iterate over @bus's list of devices, and call @fn for each, 
 * passing it @data. If @start is not NULL, we use that device to 
 * begin iterating from. 
 * 
 */  
int bus_for_each_dev(struct bus_type *bus, struct device *start,  
             void *data, int (*fn)(struct device *, void *))  
{  
   //……          
    klist_iter_init_node(&bus->p->klist_devices, &i,  
                 (start ? &start->p->knode_bus : NULL));  
  
    printk(KERN_WARNING "======>lkh while\n");  
    //获取每一个device，并调用__driver_attach  
    while ((dev = next_device(&i)) && !error){  
        error = fn(dev, data);  //__driver_attach(dev,data)  
        //printk(KERN_WARNING "======>lkh while enter\n");  
          
    //}  
  
    klist_iter_exit(&i);  
    printk(KERN_WARNING "======>lkh bus_for_each_dev end \n");  
    return error;  
}  
```

```
接着走进__driver_attach()继续看，
定义位置：kernel/msm-4.9/drivers/base/dd.c
```

```c
static int __driver_attach(struct device *dev, void *data)  
{  
    struct device_driver *drv = data;  
    int ret;  
  
    //调用 i2c_device_match()，匹配设备和驱动  
    ret = driver_match_device(drv, dev);  
    //……  
    if (!dev->driver){  
        printk(KERN_DEBUG "======>lkh enter driver_probe_device \n");  
        driver_probe_device(drv, dev);//这边走进去  
    }     
    device_unlock(dev);  
    if (dev->parent)  
        device_unlock(dev->parent);  
  
    return 0;  
} 
```

```
先看一下driver_match_device(),
定义位置：kernel/msm-4.9/drivers/base/base.h\
 kernel/msm-4.9/drivers/i2c/i2c-core.c
```

```c
static inline int driver_match_device(struct device_driver *drv,  
                      struct device *dev)  
{  
    return drv->bus->match ? drv->bus->match(dev, drv) : 1;  
}  
  
  
static int i2c_device_match(struct device *dev, struct device_driver *drv)  
{  
	    struct i2c_client   *client = i2c_verify_client(dev);  
	    struct i2c_driver   *driver;  
	  
	  
	    /* Attempt an OF style match */  
	    //在这儿匹配，三种匹配方式：  
	    //Compatible match has highest priority       
	    //Matching type is better than matching name  
	    //Matching name is a bit better than not  
	    if (i2c_of_match_device(drv->of_match_table, client))  
	        return 1;  
	  
	    /* Then ACPI style match */  
	    if (acpi_driver_match_device(dev, drv))  
	        return 1;  
	  
	    driver = to_i2c_driver(drv);  
	  
	    /* Finally an I2C match */  
	    if (i2c_match_id(driver->id_table, client))  
	        return 1;  
	  
	    return 0;  
	}  
```

```
这个i2c_device_match()就是前边在函数i2c_register_driver()里边装进driver->driver.bus的:
```

```c
driver->driver.bus = &i2c_bus_type;  
struct bus_type i2c_bus_type = {  
    .name       = "i2c",  
    .match      = i2c_device_match,  
    .probe      = i2c_device_probe,  
    .remove     = i2c_device_remove,  
    .shutdown   = i2c_device_shutdown,  
};  
```

```
若匹配成功，那么就走进driver_probe_device()里边了，
定义位置：kernel/msm-4.9/drivers/base/dd.c
```

```c
/** 
 * driver_probe_device - attempt to bind device & driver together 
 */  
int driver_probe_device(struct device_driver *drv, struct device *dev)  
{  
    int ret = 0;  
  
    printk(KERN_DEBUG "======>lkh driver_probe_device enter\n");  
  
    //检测设备是否已经注册  
    if (!device_is_registered(dev))  
        return -ENODEV;  
    //……
    pr_debug("bus: '%s': %s: matched device %s with driver %s\n",  
         drv->bus->name, __func__, dev_name(dev), drv->name);  
    pm_runtime_barrier(dev);  
    ret = really_probe(dev, drv);//这边走进去  
    //……
    return ret;  
} 
```

```
若设备已经注册，那么调用really_probe():
定义位置：kernel/msm-4.9/drivers/base/dd.c
```

```c
static int really_probe(struct device *dev, struct device_driver *drv)  
{  
     // ……
re_probe:  
    //设备与驱动匹配，device里边的driver装上了其对应的driver  
    dev->driver = drv;  
  
  
    /* 
	     * Ensure devices are listed in devices_kset in correct order 
	     * It's important to move Dev to the end of devices_kset before 
	     * calling .probe, because it could be recursive and parent Dev 
	     * should always go first 
	     */  
	    devices_kset_move_last(dev);  
	  
	    if (dev->bus->probe) {  
	        ret = dev->bus->probe(dev);//调用i2c_device_probe()  
	        if (ret)  
	            goto probe_failed;  
	    } else if (drv->probe) {  
	        ret = drv->probe(dev);
	        if (ret)  
	            goto probe_failed;  
	    }  
	    //……
	}
```



```
在i2c_device_probe()里边，将调用your_probe_func()，
定义位置: kernel/msm-4.9/drivers/i2c/i2c-core.c
```

```c
static int i2c_device_probe(struct device *dev)  
{  
    struct i2c_client   *client = i2c_verify_client(dev);  
    struct i2c_driver   *driver;  
    int status;  
  
    if (!client)  
        return 0;  
  
	    driver = to_i2c_driver(dev->driver);  
	    // ……
	    /* 
	     * When there are no more users of probe(), 
	     * rename probe_new to probe. 
	     */  
	    if (driver->probe_new)  
	        status = driver->probe_new(client);  
	    else if (driver->probe)//调用your_probe_func()  
	        status = driver->probe(client,  
	                       i2c_match_id(driver->id_table, client));  
	      
	    else  
	        status = -EINVAL;  
	  
	    if (status)  
	        goto err_detach_pm_domain;  
	  
	    return 0;  
	  
	err_detach_pm_domain:  
	    dev_pm_domain_detach(&client->dev, true);  
	err_clear_wakeup_irq:  
	    dev_pm_clear_wake_irq(&client->dev);  
	    device_init_wakeup(&client->dev, false);  
	    return status;  
	} 
```



至此，就调用到了你在自己驱动里边实现的your_probe_func()里边了，
关于probe函数，在里边主要做的主要工作如下：

a,给设备上电；
       主要用到的函数为：regulator_get()     
                       regulator_set_voltage()         
       具体上多少电，在哪一个电路上电，和你在dtsi文件的配置有关，而dtsi如何来配置，
       也取决于设备在硬件上接哪路电路供电以及芯片平台对配置选项的定义。
   b,初始化设备；

   c,创建相关节点接口；

            主要用到的函数为: sysfs_create_group();
            需要实现static struct attribute_group 的一个结构体以及你需要的接口，如:
————————————————

```c
static DEVICE_ATTR(enable,  S_IRUGO|S_IWUSR|S_IWGRP, your_deriver_enable_sho
w, your_deriver_enable_store);  
static DEVICE_ATTR(delay,   S_IRUGO|S_IWUSR|S_IWGRP, your_deriver_delay_show
,  your_deriver_delay_store);  
static DEVICE_ATTR(debug,   S_IRUGO|S_IWUSR|S_IWGRP, your_deriver_debug_show
,  your_deriver_debug_store);  
static DEVICE_ATTR(wake,    S_IWUSR|S_IWGRP,         NULL,            your_d
eriver_wake_store);  
static DEVICE_ATTR(rawdata, S_IRUGO|S_IWUSR|S_IWGRP, your_deriver_data_show, 
  NULL);  
static DEVICE_ATTR(dump,    S_IRUGO|S_IWUSR|S_IWGRP, your_deriver_dump_show, 
  NULL);  
  
static struct attribute *your_deriver_attributes[] = {  
    &dev_attr_enable.attr,  
    &dev_attr_delay.attr,  
    &dev_attr_debug.attr,  
    &dev_attr_wake.attr,  
    &dev_attr_rawdata.attr,  
    &dev_attr_dump.attr,  
    NULL  
};  
  
  
static struct attribute_group your_driver_attribute_group = {  
    .attrs = your_driver_attributes  
};  
  
sysfs_create_group(&p_data->input_dev->dev.kobj, &your_driver_attribute_group);  
```



```
创建完节点后,就可以在linux应用层空间通过操作节点来调用驱动里边的对应函数了。
如，对节点enable,delay的read（或cat），write（或echo），就对应到其驱动实现的
```



```
your_deriver_enable_show, your_deriver_enable_store
your_deriver_delay_show,  your_deriver_delay_store
```



