# Linux内核4.14版本——DMA Engine框架分析(1)_概述

## 1. 前言

本文是DMA Engine framework分析文章的第一篇，主要介绍DMA controller的概念、术语（从硬件的角度，大部分翻译自kernel的document[1]）。之后，会分别从Provider（DMA controller驱动）和Consumer（其它驱动怎么使用DMA传输数据）两个角度，介绍Linux DMA engine有关的技术细节。

## 2. DMA Engine硬件介绍

DMA是Direct Memory Access的缩写，顾名思义，就是绕开CPU直接访问memory的意思。在计算机中，相比CPU，memory和外设的速度是非常慢的，因而在memory和memory（或者memory和设备）  处理一些实时事件。因此，工程师们就设计出来一种专门用来搬运数据的器件----DMA控制器，协助CPU进行数据搬运，如下图所示：

![dma](DMA Engine框架分析.assets/b275fbf918f5684367b16cf1479168d3.gif)

图片1 DMA示意图
      思路很简单，因而大多数的DMA controller都有类似的设计原则，归纳如下[1]。

> 注1：得益于类似的设计原则，Linux kernel才有机会使用一套framework去抽象DMA engine有关的功能。

DMA 的原理就是 CPU 将需要迁移的数据的位置告诉给 DMA，包括源地址，目的地址以及需要迁移的长度，然后启动 DMA 设备，DMA 设备收到命令之后，就去完成相应的操作，最后通过中断反馈给老板 CPU，结束。

  在实现 DMA 传输时，是 DMA 控制器掌控着总线，也就是说，这里会有一个控制权转让的问题，我们当然知道，计算机中最大的 BOSS 就是 CPU，这个 DMA 暂时掌管的总线控制权当前也是 CPU 赋予的，在 DMA 完成传输之后，会通过中断通知 CPU 收回总线控制权。

  一个完整的 DMA 传输过程必须经过 DMA 请求、DMA 响应、DMA 传输、DMA 结束这四个阶段。

>+ DMA 请求：CPU 对 DMA 控制器初始化，并向 I/O 接口发出操作命令，I/O 接口提出 DMA 请求
>+ DMA 响应：DMA 控制器对 DMA 请求判别优先级以及屏蔽位，向总线裁决逻辑提出总线请求，当 CPU 执行完成当前的总线周期之后即可释放总线控制权。此时，总线裁决逻辑输出总线应答，表示 DMA 已经就绪，通过 DMA 控制器通知 I/O 接口开始 DMA 传输。
>+ DMA 传输：在 DMA 控制器的引导下，在存储器和外设之间进行数据传送，在传送过程中不需要 CPU 的参与。
>+ DMA 结束：当完成既定操作之后，DMA 控制器释放总线控制权，并向 I/O 接口发出结束信号，当 I/O 接口收到结束信号之后，一方面停止 I/O 设备的工作，另一方面向 CPU 提出中断请求，使 CPU 从不介入状态解脱，并执行一段检查本次 DMA 传输操作正确性的代码。最后带着本次操作的结果以及状态继续执行原来的程序。

### 2.1 DMA channels

一个DMA controller可以“同时”进行的DMA传输的个数是有限的，这称作DMA channels。当然，这里的channel，只是一个逻辑概念，因为：

**鉴于总线访问的冲突，以及内存一致性的考量，从物理的角度看，不大可能会同时进行两个（及以上）的DMA传输。因而DMA channel不太可能是物理上独立的通道；** 

**很多时候，DMA channels是DMA controller为了方便，抽象出来的概念，让consumer以为独占了一个channel，实际上所有channel的DMA传输请求都会在DMA controller中进行仲裁，进而串行传输；**

**因此，软件也可以基于controller提供的channel（我们称为“物理”channel），自行抽象更多的“逻辑”channel，软件会管理这些逻辑channel上的传输请求。实际上很多平台都这样做了，在DMA Engine framework中，不会区分这两种channel（本质上没区别）。**

### 2.2 DMA request lines

由图片1的介绍可知，DMA传输是由CPU发起的：CPU会告诉DMA控制器，帮忙将xxx地方的数据搬到xxx地方。CPU发完指令之后，就当甩手掌柜了。而DMA控制器，除了负责怎么搬之外，还要决定一件非常重要的事情（特别是有外部设备参与的数据传输）：**何时可以开始数据搬运？**

因为，CPU发起DMA传输的时候，并不知道当前是否具备传输条件，例如source设备是否有数据、dest设备的FIFO是否空闲等等。那谁知道是否可以传输呢？设备！因此，需要DMA传输的设备和DMA控制器之间，会有几条物理的连接线（称作DMA request，DRQ），用于通知DMA控制器可以开始传输了。

  这就是DMA request lines的由来，通常来说，每一个数据收发的节点（称作endpoint），和DMA controller之间，就有一条DMA request line（memory设备除外）。

  最后总结：**DMA channel是Provider（提供传输服务），DMA request line是Consumer（消费传输服务）。在一个系统中DMA request line的数量通常比DMA channel的数量多，因为并不是每个request line在每一时刻都需要传输。**

### 2.3 传输参数

#### 2.3.1 transfer size

 在最简单的DMA传输中，只需为DMA controller提供一个参数----transfer size，它就可以欢快的工作了：**在每一个时钟周期，DMA controller将1byte的数据从一个buffer搬到另一个buffer，直到搬完“transfer size”个bytes即可停止。**

#### 2.3.2  transfer width

不过这在现实世界中往往不能满足需求，因为有些设备可能需要在一个时钟周期中，传输指定bit的数据，例如：**memory之间传输数据的时候，希望能以总线的最大宽度为单位（32-bit、64-bit等），以提升数据传输的效率；而在音频设备中，需要每次写入精确的16-bit或者24-bit的数据；等等。**

因此，为了满足这些多样的需求，我们需要为DMA controller提供一个额外的参数----transfer width。

```c
enum dma_slave_buswidth {
  DMA_SLAVE_BUSWIDTH_UNDEFINED = 0,
  DMA_SLAVE_BUSWIDTH_1_BYTE = 1,
  DMA_SLAVE_BUSWIDTH_2_BYTES = 2,
  DMA_SLAVE_BUSWIDTH_3_BYTES = 3,
  DMA_SLAVE_BUSWIDTH_4_BYTES = 4,
  DMA_SLAVE_BUSWIDTH_8_BYTES = 8,
  DMA_SLAVE_BUSWIDTH_16_BYTES = 16,
  DMA_SLAVE_BUSWIDTH_32_BYTES = 32,
  DMA_SLAVE_BUSWIDTH_64_BYTES = 64,
};
```

#### 2.3.3 burst size

 另外，当传输的源或者目的地是memory的时候，为了提高效率，DMA controller不愿意每一次传输都访问memory，而是在内部开一个buffer，将数据缓存在自己buffer中：

+ **memory是源的时候，一次从memory读出一批数据，保存在自己的buffer中，然后再一点点（以时钟为节拍），传输到目的地；**

+ **memory是目的地的时候，先将源的数据传输到自己的buffer中，当累计一定量的数据之后，再一次性的写入memory。**

这种场景下，DMA控制器内部可缓存的数据量的大小，称作burst size----另一个参数。

#### 2.3.4 DMA transfer way

+ **block DMA方式**
+ **Scatter-gather DMA方式**

在DMA传输数据的过程中，要求源物理地址和目标物理地址必须是连续的。但是在某些计算机体系中，如IA架构，连续的存储器地址在物理上不一定是连续的，所以DMA传输要分成多次完成。

    如果在传输完一块物理上连续的数据后引起一次中断，然后再由主机进行下一块物理上连续的数据传输，那么这种方式就为block DMA方式。

    Scatter-gather DMA使用一个链表描述物理上不连续的存储空间，然后把链表首地址告诉DMA master。DMA master在传输完一块物理连续的数据后，不用发起中断，而是根据链表来传输下一块物理上连续的数据，直到传输完毕后再发起一次中断。

  很显然，scatter-gather DMA方式比block DMA方式效率高。

> 注2：具体怎么支持，和硬件实现有关，这里不再多说（只需要知道有这个事情即可，编写DMA controller驱动的时候，自然会知道怎么做）。 

## 3. DMA 控制器与 CPU 怎样分时使用内存

外围设备可以通过 DMA 控制器直接访问内存，与此同时，CPU 可以继续执行程序逻辑，通常采用以下三种方法实现 DMA 控制机与 CPU 分时使用内存：

- 停止 CPU 访问内存；
- 周期挪用；
- DMA 与 CPU 交替访问内存。

### 3.1 停止 CPU 访问内存

当外围设备要求传送一批数据时，由 DMA 控制器发一个停止信号给 CPU，要求 CPU 放弃对地址总线、数据总线和有关控制总线的使用权，DMA 控制器获得总线控制权之后，开始进行数据传输，在一批数据传输完毕之后，DMA 控制器通知 CPU 可以继续使用内存，并把总线控制权交还给 CPU。图（a）就是这样的一个传输图，很显然，在这种 DMA 传输过程中，CPU 基本处于不工作状态或者保持状态。

+ 优点：控制简单，是用于数据传输率很高的设备进行成组的传输。
+ 缺点：在 DMA 控制器访问内存阶段，内存效能没有充分发挥，相当一部分的内存周期是空闲的，这是因为外围设备传送两个数据之间的间隔一般大于内存存储间隔，即使是再告诉的 I/O 存储设备也是如此（就是内存读写速率比 SSD 等都快）。
  

![img](DMA Engine框架分析.assets/2019112311113747.JPG)

### 3.2 周期挪用

当 I/O 设备没有 DMA 请求时，CPU 按照程序要求访问内存；一旦 I/O 设备有 DMA 请求，则由 I/O 设备挪用一个或者几个内存周期，这种传送方式如下图所示：

![在这里插入图片描述](DMA Engine框架分析.assets/20191123111459474.JPG)

 I/O 设备要求 DMA 传送时可能遇到两种情况：

   第一种：此时 CPU 不需要访问内存，例如，CPU 正在执行乘法指令，由于乘法指令执行时间长，此时 I/O 访问与 CPU 访问之间没有冲突，则 I/O 设备挪用一两个内存周期是对 CPU 执行没有任何的影响。

第二种：I/O 设备要求访问内存时，CPU 也需要访问内存，这就产生了访问内存冲突，在这种情况下 I/O 设备优先访问，因为 I/O 访问有时间要求，前一个 I/O 数据必须在下一个访问请求来到之前存储完毕。显然，在这种情况下，I/O 设备挪用一两个内存周期，意味着 CPU 延缓了对指令的执行，或者更为明确的讲，在 CPU 执行访问指令的过程中插入了 DMA 请求。周期挪用内存的方式与停止 CPU 访问内存的方式对比，这种方式，既满足了 I/O 数据的传送，也发挥了 CPU 和内存的效率，是一种广泛采用的方法，但是 I/O 设备每一周期挪用都有申请总线控制权，建立总线控制权和归还总线控制权的过程，所以不适合传输小的数据。这种方式比较适用于 I/O 设备读写周期大于内存访问周期的情况。

### 3.3 DMA 和 CPU 交替访问内存

 如果 CPU 的工作周期比内存的存储周期长很多，此时采用交替访问内存的方法可以使 DMA 传送和 CPU 同时发挥最高效率。这种传送方式的时间图如下：

![在这里插入图片描述](DMA Engine框架分析.assets/20191123111704598.JPG)

此图是 DMA 和 CPU 交替访问内存的消息时间图，假设 CPU 的工作周期为 1.2us，内存的存储周期小于 0.6us，那么一个 CPU 周期可以分为 C1 和 C2 两个分周期，其中 C1 专供 DMA 控制器访问内存，C2 专供 CPU 访问内存。这种方式不需要总线使用权的申请、建立和归还的过程，总线的使用权是通过 C1 和 C2 分时机制决定的。CPU 和 DMA 控制器各自有自己的访问地址寄存器、数据寄存器和读写信号控制寄存器。在 C1 周期中，如果 DMA 控制器有访问请求，那么可以将地址和数据信号发送到总线上。在 C2 周期内，如果 CPU 有访问内存的请求，同样将请求的地址和数据信号发送到总线上。事实上，这是用 C1 C2 控制的一个多路转换器，这种总线控制权的转移几乎不需要什么时间，所以对于 DMA 来讲是很高效率的。

这种传送方式又称为“透明 DMA”方式，其由来是由于 DMA 传送对于 CPU 来讲是透明的，没有任何感觉。在透明 DMA 方式下 CPU 不需要停止主程序的运行，也不进入等待状态，是一种很高效率的工作方式，当然，相对应的硬件设计就会更加复杂。

以上是传统意义的 DMA：是一种完全由硬件执行 I/O 交换的工作方式。这种方式中，DMA 控制器和 CPU 完全接管对总线的控制，数据交换不经过 CPU，而直接在内存和 I/O 设备之间进行。DMA 工作时，由 DMA 控制器向内存发起地址和控制信号，进行地址修改，对传输的数据进行统计，并以中断的形式通知 CPU 数据传输完成。

## 4. DMA 导致的问题

DMA 不仅仅只会带来效率的提升，同样，它也会带来一些问题，最明显的就是缓存一致性问题。想象一下，现代的 CPU 都是自带一级缓存、二级缓存甚至是三级缓存，当 CPU 访问内存某个地址时，暂时先将新的值写入到缓存中，但是没有更新外部内存的数据，假设此时发生了 DMA 请求且操作的就是这一块在缓存中更新了而外部内存没有更新的内存地址，这样 DMA 读到的就是非最新数据；相同的，如果外部设备通过 DMA 将新值写入到内存中，但是 CPU 访问得到的确实缓存中的数据，这样也会导致拿到的不是最新的数据。
![在这里插入图片描述](DMA Engine框架分析.assets/20191123112152285.JPG)

![在这里插入图片描述](DMA Engine框架分析.assets/20191223192500912.JPG)

为了能够正确进行 DMA 操作，必须进行必要的 Cache 操作，Cache 的操作主要分为 invalidate（作废）和 writeback（回写），有时候也会二者一同使用。如果 DMA 使用了 Cache，那么 Cache 一致性问题是必须要考虑的，解决的最简单的办法就是禁止 DMA 目标地址范围的 Cache 功能，但是这样会牺牲掉一定的性能。

**因此，在 DMA 是否使用 cache 的问题上，可以根据 DMA 缓冲区的期望保留时间长短来决策。DMA 被区分为：一致性 DMA 映射和流式 DMA 映射。**

+ **一致性 DMA 映射：申请的缓存区会被以非缓存的形式映射，一致性映射具有很长的生命周期，在这段时间内占用映射寄存器，即使不再使用也不会释放，一般情况下，一致性 DMA 的生命周期会被设计为驱动的生命周期（也就是在 init 里面注册，在 exit 里面释放）。**

+ **流式 DMA 映射：实现比较复杂，表现特征为使用周期很短，它的实现中会主动保持缓存的一致性。在使用方法上，流式 DMA 还需要指定内核数据的流向，不然会导致不可预期的后果。不过很多的现代处理能能够自己来保证 CPU 和 DMA 控制器之间的 cache 一致性问题，比如 ARM 的 ACP 功能，这样像dma_map_single函数只是返回物理地址，而dma_unmap_single则什么都不做，这样极大的提升了系统性能。**

### 4.1 流式 DMA映射 和一致性 DMA映射区别

 一致性 DMA映射，采用的是系统预留的一段 DMA 内存用于 DMA 操作，这一段内核在系统启动阶段就已经预留完毕，比如 arm64 平台会在 dts 文件中写明系统预留的 DMA 内存段位于何处，并且会被标志为用于 dma 一致性内存申请，如果你有关注 DMA 的一致性映射操作 API 就会发现，一致性 DMA 不会去使用别的地方申请的内存，它都是通过dma_alloc_coherent自我申请内存，然后驱动自己填充数据，最后被提交给 DMA 控制器。

流式 DMA映射 ，它可以是随意的内存交给 DMA 进行处理，不需要从系统预留的 DMA 位置进行内存申请，任何普通的 kmalloc 申请的内存都能交给 DMA 控制器进行操作。

二者是如何做到缓存一致性的：

>+ 一致性 DMA ，在 DMA 内存申请的过程中，首先进行一个 ioremap_nocache 的映射，然后调用函数 dma_cache_wback_inv 保证缓存已经刷新到位之后，后面使用这一段内存时不存在一二级缓存；
>
>+  流式 DMA ，不能直接禁止缓存，因为流式 DMA 可以使用系统中的任意地址范围的地址，CPU 总不能将系统所有的地址空间都禁止缓存，这不科学，那么为了实现缓存一致性，流式 DMA 需要不断的对缓存进行失效操作，告诉 CPU 这一段缓存是不可信的，必须从内存中重新获取。一致性 DMA 就是直接将缓存禁止，而流式 DMA 则是将缓存失效刷新。

### 4.2 Linux内核中流式、一致性映射的API

[DMA 的内核编程 API](https://www.byteisland.com/dma-与-scatterlist-技术简介/)，描述了Linux内核中DMA流式、一致性映射的一些API。

## 5 参考文献

[1] Documentation/dmaengine/provider.txt

[2] Documentation\dmaengine\client.txt

[3] Documentation\dmaengine\dmatest.txt

[4] Documentation\crypto\async-tx-api.txt

[5] Documentation\DMA-API-HOWTO.txt

[6] Documentation\DMA-API.txt

[7] Linux DMA Engine framework(1) - DMA Introduction_Hacker_Albert的博客-CSDN博客

[8] DMA 与 scatterlist 技术简介 – 字节岛技术分享
————————————————
版权声明：本文为CSDN博主「风雨兼程8023」的原创文章，遵循CC 4.0 BY-SA版权协议，转载请附上原文出处链接及本声明。
原文链接：https://blog.csdn.net/yangguoyu8023/article/details/121852348



# Linux内核4.14版本——DMA Engine框架分析(2)_功能介绍及解接口分析（slave client driver）

drivers\[dma](https://so.csdn.net/so/search?q=dma&spm=1001.2101.3001.7020)\dmaengine.c

include\linux\dmaengine.h

## 1. 前言

从我们的直观感受来说，DMA并不是一个复杂的东西，要做的事情也很单纯直白。因此Linux kernel对它的抽象和实现，也应该简洁、易懂才是。不过现实却不甚乐观（个人感觉），Linux kernel dmaengine framework的实现，真有点晦涩的感觉。为什么会这样呢？

如果一个软件模块比较复杂、晦涩，要么是设计者的功力不够，要么是需求使然。当然，我们不敢对Linux kernel的那些大神们有丝毫怀疑和不敬，只能从需求上下功夫了：难道Linux kernel中的driver对DMA的使用上，有一些超出了我们日常的认知范围？

要回答这些问题并不难，将dmaengine framework为consumers提供的功能和API梳理一遍就可以了，这就是本文的目的。当然，也可以借助这个过程，加深对DMA的理解，以便在编写那些需要DMA传输的driver的时候，可以更游刃有余。

## 2.Slave-DMA API和Async TX API

从方向上来说，DMA传输可以分为4类：memory到memory、memory到device、device到memory以及device到device。Linux kernel作为CPU的代理人，从它的视角看，外设都是slave，因此称这些有device参与的传输（MEM2DEV、DEV2MEM、DEV2DEV）为Slave-DMA传输。而另一种memory到memory的传输，被称为Async TX。

为什么强调这种差别呢？因为Linux为了方便基于DMA的memcpy、memset等操作，在dma engine之上，封装了一层更为简洁的API（如下面图片1所示），这种API就是Async TX API（以async_开头，例如async_memcpy、async_memset、async_xor等）。
![dma_api](DMA Engine框架分析.assets/a545219e55213410109c1ce03e362991.gif)

 最后，因为memory到memory的DMA传输有了比较简洁的API，没必要直接使用dma engine提供的API，最后就导致dma engine所提供的API就特指为Slave-DMA API（把mem2mem剔除了）。

本文主要介绍dma engine为consumers提供的功能和API，因此就不再涉及Async TX API了（具体可参考本站后续的文章。

> 注1：Slave-DMA中的“slave”，指的是参与DMA传输的设备。而对应的，“master”就是指DMA controller自身。一定要明白“slave”的概念，才能更好的理解kernel dma engine中有关的术语和逻辑。

## 3. dma engine的使用步骤

>  注2：本文大部分内容翻译自kernel document[1]，喜欢读英语的读者可以自行参考。

对设备驱动的编写者来说，要基于dma engine提供的Slave-DMA API进行DMA传输的话，需要如下的操作步骤：

> 1）申请一个DMA channel。
> 2）根据设备（slave）的特性，配置DMA channel的参数。
> 3）要进行DMA传输的时候，获取一个用于识别本次传输（transaction）的描述符（descriptor）。
> 4）将本次传输（transaction）提交给dma engine并启动传输。
> 5）等待传输（transaction）结束。
> 然后，重复3~5即可。

上面5个步骤，除了3有点不好理解外，其它的都比较直观易懂，具体可参考后面的介绍。

### 3.1 申请DMA channel

 任何consumer（文档[1]中称作client，也可称作slave driver，意思都差不多，不再特意区分）在开始DMA传输之前，都要申请一个DMA channel（有关DMA channel的概念，请参考[2]中的介绍）。

DMA channel（在kernel中由“struct dma_chan”数据结构表示）由provider（或者是DMA controller）提供，被consumer（或者client）使用。对consumer来说，不需要关心该数据结构的具体内容（我们会在dmaengine provider的介绍中在详细介绍）。

consumer可以通过如下的API申请DMA channel(drivers\dma\dmaengine.c)：

```c
/**
 * dma_request_chan - try to allocate an exclusive slave channel
 * @dev:	pointer to client device structure
 * @name:	slave channel name
 *
 * Returns pointer to appropriate DMA channel on success or an error pointer.
 */
struct dma_chan *dma_request_chan(struct device *dev, const char *name);
```

该接口会返回绑定在指定设备（dev）上名称为name的dma channel。dma engine的provider和consumer可以使用device tree、ACPI或者struct dma_slave_map类型的match table提供这种绑定关系，具体可参考XXXX章节的介绍。

最后，申请得到的dma channel可以在不需要使用的时候通过下面的API释放掉：

```c
void dma_release_channel(struct dma_chan *chan)
{
	mutex_lock(&dma_list_mutex);
	WARN_ONCE(chan->client_count != 1,
		  "chan reference count %d != 1\n", chan->client_count);
	dma_chan_put(chan);
	/* drop PRIVATE cap enabled by __dma_request_channel() */
	if (--chan->device->privatecnt == 0)
		dma_cap_clear(DMA_PRIVATE, chan->device->cap_mask);
	mutex_unlock(&dma_list_mutex);
}
```

### 3.2 配置DMA channel的参数

driver申请到一个为自己使用的DMA channel之后，需要根据自身的实际情况，以及DMA controller的能力，对该channel进行一些配置。可配置的内容由struct dma_slave_config数据结构表示（具体可参考4.1小节的介绍）。driver将它们填充到一个struct dma_slave_config变量中后，可以调用如下API将这些信息告诉给DMA controller:

```c
static inline int dmaengine_slave_config(struct dma_chan *chan,
					  struct dma_slave_config *config)
{
	if (chan->device->device_config)
		return chan->device->device_config(chan, config);
 
	return -ENOSYS;
}
```

```c
struct dma_slave_config {
	enum dma_transfer_direction direction;
	phys_addr_t src_addr;
	phys_addr_t dst_addr;
	enum dma_slave_buswidth src_addr_width;
	enum dma_slave_buswidth dst_addr_width;
	u32 src_maxburst;
	u32 dst_maxburst;
	u32 src_port_window_size;
	u32 dst_port_window_size;
	bool device_fc;
	unsigned int slave_id;
};
```

### 3.3 获取传输描述（tx descriptor）

DMA传输属于异步传输，在启动传输之前，slave driver需要将此次传输的一些信息（例如src/dst的buffer、传输的方向等）提交给dma engine（本质上是dma controller driver），dma engine确认okay后，返回一个描述符（由struct dma_async_tx_descriptor抽象）。此后，slave driver就可以以该描述符为单位，控制并跟踪此次传输。

struct dma_async_tx_descriptor数据结构可参考4.2小节的介绍。根据传输模式的不同，slave driver可以使用下面三个API获取传输描述符（具体可参考Documentation/dmaengine/client.txt[1]中的说明）：

```c
struct dma_async_tx_descriptor *dmaengine_prep_slave_sg(
        struct dma_chan *chan, struct scatterlist *sgl,
        unsigned int sg_len, enum dma_data_direction direction,
        unsigned long flags);
 
struct dma_async_tx_descriptor *dmaengine_prep_dma_cyclic(
        struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
        size_t period_len, enum dma_data_direction direction);
 
struct dma_async_tx_descriptor *dmaengine_prep_interleaved_dma(
        struct dma_chan *chan, struct dma_interleaved_template *xt,
        unsigned long flags);
```

dmaengine_prep_slave_sg用于在“scatter gather buffers”列表和总线设备之间进行DMA传输，参数如下：
注3：有关scatterlist 我们在[3][2]中有提及，后续会有专门的文章介绍它，这里暂且按下不表。

> chan，本次传输所使用的dma channel。
>
> sgl，要传输的“scatter gather buffers”数组的地址；
> sg_len，“scatter gather buffers”数组的长度。
>
> direction，数据传输的方向，具体可参考enum dma_data_direction （include/linux/dma-direction.h）的定义。
>
> flags，可用于向dma controller driver传递一些额外的信息，包括（具体可参考enum dma_ctrl_flags中以DMA_PREP_开头的定义）：
> DMA_PREP_INTERRUPT，告诉DMA controller driver，本次传输完成后，产生一个中断，并调用client提供的回调函数（可在该函数返回后，通过设置struct dma_async_tx_descriptor指针中的相关字段，提供回调函数，具体可参考4.2小节的介绍）；
> DMA_PREP_FENCE，告诉DMA controller driver，后续的传输，依赖本次传输的结果（这样controller driver就会小心的组织多个dma传输之间的顺序）；
> DMA_PREP_PQ_DISABLE_P、DMA_PREP_PQ_DISABLE_Q、DMA_PREP_CONTINUE，PQ有关的操作，TODO。

dmaengine_prep_dma_cyclic常用于音频等场景中，在进行一定长度的dma传输（buf_addr&buf_len）的过程中，每传输一定的byte（period_len），就会调用一次传输完成的回调函数，参数包括：

> chan，本次传输所使用的dma channel。
>
> buf_addr、buf_len，传输的buffer地址和长度。
>
> period_len，每隔多久（单位为byte）调用一次回调函数。需要注意的是，buf_len应该是period_len的整数倍。
>
> direction，数据传输的方向。

dmaengine_prep_interleaved_dma可进行不连续的、交叉的DMA传输，通常用在图像处理、显示等场景中，具体可参考struct dma_interleaved_template结构的定义和解释（这里不再详细介绍，需要用到的时候，再去学习也okay）。

### 3.4 启动传输

通过3.3章节介绍的API获取传输描述符之后，client driver可以通过dmaengine_submit接口将该描述符放到传输队列上，然后调用dma_async_issue_pending接口，启动传输。

dmaengine_submit的原型如下：

```c
dma_cookie_t dmaengine_submit(struct dma_async_tx_descriptor *desc)
```

参数为传输描述符指针，返回一个唯一识别该描述符的cookie，用于后续的跟踪、监控。

dma_async_issue_pending的原型如下：

```c
void dma_async_issue_pending(struct dma_chan *chan);
```

 参数为dma channel,无返回值。

>  注4：由上面两个API的特征可知，kernel dma engine鼓励client driver一次提交多个传输，然后由kernel（或者dma controller driver）统一完成这些传输。

### 3.5 等待传输结束

传输请求被提交之后，client driver可以通过回调函数获取传输完成的消息，当然，也可以通过dma_async_is_tx_complete等API，测试传输是否完成。不再详细说明了。

 最后，如果等不及了，也可以使用dmaengine_pause、dmaengine_resume、dmaengine_terminate_xxx等API，暂停、终止传输，具体请参考kernel document[1]以及source code。

## 4.重要数据结构说明

### 4.1 struct dma_slave_config

中包含了完成一次DMA传输所需要的所有可能的参数，其定义如下：

```c
/* include/linux/dmaengine.h */
 
struct dma_slave_config {
        enum dma_transfer_direction direction;
        phys_addr_t src_addr;
        phys_addr_t dst_addr;
        enum dma_slave_buswidth src_addr_width;
        enum dma_slave_buswidth dst_addr_width;
        u32 src_maxburst;
        u32 dst_maxburst;
        bool device_fc;
        unsigned int slave_id;
};
```

> direction，指明传输的方向，包括（具体可参考enum dma_transfer_direction的定义和注释）：
>     DMA_MEM_TO_MEM，memory到memory的传输；
>     DMA_MEM_TO_DEV，memory到设备的传输；
>     DMA_DEV_TO_MEM，设备到memory的传输；
>     DMA_DEV_TO_DEV，设备到设备的传输。
>     注5：controller不一定支持所有的DMA传输方向，具体要看provider的实现。
>     注6：参考第2章的介绍，MEM to MEM的传输，一般不会直接使用dma engine提供的API。
>
> src_addr，传输方向是dev2mem或者dev2dev时，读取数据的位置（通常是固定的FIFO地址）。对mem2dev类型的channel，不需配置该参数（每次传输的时候会指定）；
> dst_addr，传输方向是mem2dev或者dev2dev时，写入数据的位置（通常是固定的FIFO地址）。对dev2mem类型的channel，不需配置该参数（每次传输的时候会指定）；
> src_addr_width、dst_addr_width，src/dst地址的宽度，包括1、2、3、4、8、16、32、64（bytes）等（具体可参考enum dma_slave_buswidth 的定义）。
>
> src_maxburst、dst_maxburst，src/dst最大可传输的burst size（可参考[2]中有关burst size的介绍），单位是src_addr_width/dst_addr_width（注意，不是byte）。
>
> device_fc，当外设是Flow Controller（流控制器）的时候，需要将该字段设置为true。CPU中有关DMA和外部设备之间连接方式的设计中，决定DMA传输是否结束的模块，称作flow controller，DMA controller或者外部设备，都可以作为flow controller，具体要看外设和DMA controller的设计原理、信号连接方式等，不在详细说明(感兴趣的同学可参考[4]中的介绍)。
>
> slave_id，外部设备通过slave_id告诉dma controller自己是谁（一般和某个request line对应）。很多dma controller并不区分slave，只要给它src、dst、len等信息，它就可以进行传输，因此slave_id可以忽略。而有些controller，必须清晰地知道此次传输的对象是哪个外设，就必须要提供slave_id了（至于怎么提供，可dma controller的硬件以及驱动有关，要具体场景具体对待）。
> 

### 4.2 struct dma_async_tx_descriptor

传输描述符用于描述一次DMA传输（类似于一个文件句柄）。client driver将自己的传输请求通过3.3中介绍的API提交给dma controller driver后，controller driver会返回给client driver一个描述符。

client driver获取描述符后，可以以它为单位，进行后续的操作（启动传输、等待传输完成、等等）。也可以将自己的回调函数通过描述符提供给controller driver。

传输描述符的定义如下：

```c
struct dma_async_tx_descriptor {
         dma_cookie_t cookie;
         enum dma_ctrl_flags flags; /* not a 'long' to pack with cookie */
         dma_addr_t phys;
         struct dma_chan *chan;
         dma_cookie_t (*tx_submit)(struct dma_async_tx_descriptor *tx);
         int (*desc_free)(struct dma_async_tx_descriptor *tx);
         dma_async_tx_callback callback;
         void *callback_param;
         struct dmaengine_unmap_data *unmap;
#ifdef CONFIG_ASYNC_TX_ENABLE_CHANNEL_SWITCH
         struct dma_async_tx_descriptor *next;
         struct dma_async_tx_descriptor *parent;
         spinlock_t lock;
#endif
};
```

cookie，一个整型数，用于追踪本次传输。一般情况下，dma controller driver会在内部维护一个递增的number，每当client获取传输描述的时候（参考3.3中的介绍），都会将该number赋予cookie，然后加一。

> 注7：有关cookie的使用场景，我们会在后续的文章中再详细介绍。

> flags， DMA_CTRL_开头的标记，包括：
> DMA_CTRL_REUSE，表明这个描述符可以被重复使用，直到它被清除或者释放；
> DMA_CTRL_ACK，如果该flag为0，表明暂时不能被重复使用。
>
> phys，该描述符的物理地址？？不太懂！
>
> chan，对应的dma channel。
>
> tx_submit，controller driver提供的回调函数，用于把改描述符提交到待传输列表。通常由dma engine调用，client driver不会直接和该接口打交道。
>
> desc_free，用于释放该描述符的回调函数，由controller driver提供，dma engine调用，client driver不会直接和该接口打交道。
>
> callback、callback_param，传输完成的回调函数（及其参数），由client driver提供。
>
> 后面其它参数，client driver不需要关心，暂不描述了。

## 5 参考文章

[1] Documentation/dmaengine/client.txt

# Linux内核4.14版本——DMA Engine框架分析(3)_dma controller驱动

## 1. 前言

本文将从provider的角度，介绍怎样在[linux](https://so.csdn.net/so/search?q=linux&spm=1001.2101.3001.7020) kernel dmaengine的框架下，编写dma controller驱动。

## 2. dma controller驱动的软件框架

设备驱动的本质是描述并抽象硬件，然后为consumer提供操作硬件的友好接口。dma controller驱动也不例外，它要做的事情无外乎是：

> 1）抽象并控制DMA控制器。
>
> 2）管理DMA channel（可以是物理channel，也可以是虚拟channel，具体可参考[1]中的介绍），并向client driver提供友好、易用的接口。
>
> 3）以DMA channel为操作对象，响应client driver（consumer）的传输请求，并控制DMA controller，执行传输。

当然，按照惯例，为了统一提供给consumer的API，并减少DMA controller driver的开发难度（从论述题变为填空题），dmaengine framework提供了一套controller driver的开发框架，主要思路是（参考图片1）：

![dma驱动框架](DMA Engine框架分析.assets/230a67ca0573effee44e072667aebf6a.gif)

> 1）使用struct dma_device抽象DMA controller，controller driver只要填充该结构中必要的字段，就可以完成dma controller的驱动开发。
>
> 2）使用struct dma_chan（图片1中的DCn）抽象物理的DMA channel（图片1中的CHn），物理channel和controller所能提供的通道数一一对应。
>
> 3）基于物理的DMA channel，使用struct virt_dma_cha抽象出虚拟的dma channel（图片1中的VCx）。多个虚拟channel可以共享一个物理channel，并在这个物理channel上进行分时传输。
>
> 4）基于这些数据结构，提供一些便于controller driver开发的API，供driver使用。

上面三个数据结构的描述，可参考第3章的介绍。然后，我们会在第4章介绍相关的API、controller driver的开发思路和步骤以及dmaengine中和controller driver有关的重要流程。

## 3. 主要数据结构描述

### 3.1 struct dma_device

用于抽象dma controller的struct dma_device是一个庞杂的数据结构（具体可参考include/linux/dmaengine.h中的代码），不过真正需要dma controller driver关心的内容却不是很多，主要包括：

 注1：为了加快对dmaengine framework的理解和掌握，这里只描述一些简单的应用场景，更复杂的场景，只有等到有需求的时候，再更深入的理解。

> channels，一个链表头，用于保存该controller支持的所有dma channel（struct dma_chan，具体可参考3.2小节）。在初始化的时候，dma controller driver首先要调用INIT_LIST_HEAD初始化它，然后调用list_add_tail将所有的channel添加到该链表头中。
>
> cap_mask，一个bitmap，用于指示该dma controller所具备的能力（可以进行什么样的DMA传输），例如（具体可参考enum dma_transaction_type的定义）：
>     DMA_MEMCPY，可进行memory copy；
>     DMA_MEMSET，可进行memory set；
>     DMA_SG，可进行scatter list传输；
>     DMA_CYCLIC，可进行cyclic类[2]的传输；
>     DMA_INTERLEAVE，可进行交叉传输[2]；
>     等等，等等（各种奇奇怪怪的传输类型，不看不知道，一看吓一跳！！）。
> 另外，该bitmap的定义，需要和后面device_prep_dma_xxx形式的回调函数对应（bitmap中支持某个传输类型，就必须提供该类型对应的回调函数）。
>
> src_addr_widths，一个bitmap，表示该controller支持哪些宽度的src类型，包括1、2、3、4、8、16、32、64（bytes）等（具体可参考enum dma_slave_buswidth 的定义）。
> dst_addr_widths，一个bitmap，表示该controller支持哪些宽度的dst类型，包括1、2、3、4、8、16、32、64（bytes）等（具体可参考enum dma_slave_buswidth 的定义）。
>
> directions，一个bitmap，表示该controller支持哪些传输方向，包括DMA_MEM_TO_MEM、DMA_MEM_TO_DEV、DMA_DEV_TO_MEM、DMA_DEV_TO_DEV，具体可参考enum dma_transfer_direction的定义和注释，以及[2]中相关的说明。
>
> max_burst，支持的最大的burst传输的size。有关burst传输的概念可参考[1]。
>
> descriptor_reuse，指示该controller的传输描述可否可重复使用（client driver可只获取一次传输描述，然后进行多次传输）。
>
> device_alloc_chan_resources/device_free_chan_resources，client driver申请/释放[2] dma channel的时候，dmaengine会调用dma controller driver相应的alloc/free回调函数，以准备相应的资源。具体要准备哪些资源，则需要dma controller driver根据硬件的实际情况，自行决定（这就是dmaengine framework的流氓之处，呵呵~）。
>
> device_prep_dma_xxx，同理，client driver通过dmaengine_prep_xxx API获取传输描述符的时候，damengine则会直接回调dma controller driver相应的device_prep_dma_xxx接口。至于要在这些回调函数中做什么事情，dma controller driver自己决定就是了（真懒啊！）。
>
> device_config，client driver调用dmaengine_slave_config[2]配置dma channel的时候，dmaengine会调用该回调函数，交给dma controller driver处理。
>
> device_pause/device_resume/device_terminate_all，同理，client driver调用dmaengine_pause、dmaengine_resume、dmaengine_terminate_xxx等API的时候，dmaengine会调用相应的回调函数。
>
> device_issue_pending，client driver调用dma_async_issue_pending启动传输的时候，会调用调用该回调函数。

总结：dmaengine对dma controller的抽象和封装，只是薄薄的一层：仅封装出来一些回调函数，由dma controller driver实现，被client driver调用，dmaengine本身没有太多的操作逻辑。

### 3.2 struct dma_chan

struct dma_chan用于抽象dma channel，其内容为：

```c
struct dma_chan {
        struct dma_device *device;
        dma_cookie_t cookie;
        dma_cookie_t completed_cookie;
 
        /* sysfs */
        int chan_id;
        struct dma_chan_dev *dev;
 
        struct list_head device_node;
        struct dma_chan_percpu __percpu *local;
        int client_count;
        int table_count;
 
        /* DMA router */
        struct dma_router *router;
        void *route_data;
 
        void *private;
};
```

需要dma controller driver关心的字段包括：

> device，指向该channel所在的dma controller。
>
> cookie，client driver以该channel为操作对象获取传输描述符时，dma controller driver返回给client的最后一个cookie。
>
> completed_cookie，在这个channel上最后一次完成的传输的cookie。dma controller driver可以在传输完成时调用辅助函数dma_cookie_complete设置它的value。
>
> device_node，链表node，用于将该channel添加到dma_device的channel列表中。
>
> router、route_data，TODO。

### 3.3 struct virt_dma_cha

 struct virt_dma_chan用于抽象一个虚拟的dma channel，多个虚拟channel可以共用一个物理channel，并由软件调度多个传输请求，将多个虚拟channel的传输串行地在物理channel上完成。该数据结构的定义如下：

```c
/* drivers/dma/virt-dma.h */
 
struct virt_dma_desc {
        struct dma_async_tx_descriptor tx;
        /* protected by vc.lock */
        struct list_head node;
};
 
struct virt_dma_chan {
        struct dma_chan chan;
        struct tasklet_struct task;
        void (*desc_free)(struct virt_dma_desc *);
 
        spinlock_t lock;
 
        /* protected by vc.lock */
        struct list_head desc_allocated;
        struct list_head desc_submitted;
        struct list_head desc_issued;
        struct list_head desc_completed;
 
        struct virt_dma_desc *cyclic;
 
};
```

> chan，一个struct dma_chan类型的变量，用于和client driver打交道（屏蔽物理channel和虚拟channel的差异）。
>
> task，一个tasklet，用于等待该虚拟channel上传输的完成（由于是虚拟channel，传输完成与否只能由软件判断）。
>
> desc_allocated、desc_submitted、desc_issued、desc_completed，四个链表头，用于保存不同状态的虚拟channel描述符（struct virt_dma_desc，仅仅对struct dma_async_tx_descriptor[2]做了一个简单的封装）。
> 

## 4. dmaengine向dma controller driver提供的API汇整

damengine直接向dma controller driver提供的API并不多（大部分的逻辑交互都位于struct dma_device结构的回调函数中），主要包括：

**1）struct dma_device变量的注册和注销接口**

```c
/* include/linux/dmaengine.h */
int dma_async_device_register(struct dma_device *device);
void dma_async_device_unregister(struct dma_device *device);
```

> dma controller driver准备好struct dma_device变量后，可以调用dma_async_device_register将它（controller）注册到kernel中。该接口会对device指针进行一系列的检查，然后对其做进一步的初始化，最后会放在一个名称为dma_device_list的全局链表上，以便后面使用。
>
> dma_async_device_unregister，注销接口。

**2）cookie有关的辅助接口，位于“drivers/dma/dmaengine.h”中，包括**

```c
static inline void dma_cookie_init(struct dma_chan *chan)
static inline dma_cookie_t dma_cookie_assign(struct dma_async_tx_descriptor *tx)
static inline void dma_cookie_complete(struct dma_async_tx_descriptor *tx)
static inline enum dma_status dma_cookie_status(struct dma_chan *chan,
        dma_cookie_t cookie, struct dma_tx_state *state)
```

> 由于cookie有关的操作，有很多共性，dmaengine就提供了一些通用实现：
>
> void dma_cookie_init，初始化dma channel中的cookie、completed_cookie字段。
>
> dma_cookie_assign，为指针的传输描述（tx）分配一个cookie。
>
> dma_cookie_complete，当某一个传输（tx）完成的时候，可以调用该接口，更新该传输所对应channel的completed_cookie字段。
>
> dma_cookie_status，获取指定channel（chan）上指定cookie的传输状态。

**3）依赖处理接口**

```c
void dma_run_dependencies(struct dma_async_tx_descriptor *tx);
```

 由前面的描述可知，client可以同时提交多个具有依赖关系的dma传输。因此当某个传输结束的时候，dma controller driver需要检查是否有依赖该传输的传输，如果有，则传输之。这个检查并传输的过程，可以借助该接口进行（dma controller driver只需调用即可，省很多事）。
**4）device tree有关的辅助接口**

```c
extern struct dma_chan *of_dma_simple_xlate(struct of_phandle_args *dma_spec,
                struct of_dma *ofdma);
extern struct dma_chan *of_dma_xlate_by_chan_id(struct of_phandle_args *dma_spec,
                struct of_dma *ofdma);
```

上面两个接口可用于将client device node中有关dma的字段解析出来，并获取对应的dma channel。后面实际开发的时候会举例说明。

**5）虚拟dma channel有关的API**

后面会有专门的文章介绍虚拟dma，这里不再介绍。

## 5. 编写一个dma controller driver的方法和步骤

上面啰嗦了这么多，相信大家还是似懂非懂（很正常，我也是，dmaengine framework特点就是框架简单，细节复杂）。到底怎么在dmaengine的框架下编写dma controller驱动呢？现在看来，只靠这篇文章，可能达不到目的了，这里先罗列一下基本步骤，后续我们会结合实际的开发过程，进一步的理解和掌握。

编写一个dma controller driver的基本步骤包括（不考虑虚拟channel的情况）：

> 1）定义一个struct dma_device变量，并根据实际的硬件情况，填充其中的关键字段。
>
>  2）根据controller支持的channel个数，为每个channel定义一个struct dma_chan变量，进行必要的初始化后，将每个channel都添加到struct dma_device变量的channels链表中。
>
> 3）根据硬件特性，实现struct dma_device变量中必要的回调函数（device_alloc_chan_resources/device_free_chan_resources、device_prep_dma_xxx、device_config、device_issue_pending等等）。
>
> 4）调用dma_async_device_register将struct dma_device变量注册到kernel中。
>
> 5）当client driver申请dma channel时（例如通过device tree中的dma节点获取），dmaengine core会调用dma controller driver的device_alloc_chan_resources函数，controller driver需要在这个接口中奖该channel的资源准备好。
>
> 6）当client driver配置某个dma channel时，dmaengine core会调用dma controller driver的device_config函数，controller driver需要在这个函数中将client想配置的内容准备好，以便进行后续的传输。
>
> 7）client driver开始一个传输之前，会把传输的信息通过dmaengine_prep_slave_xxx接口交给controller driver，controller driver需要在对应的device_prep_dma_xxx回调中，将这些要传输的内容准备好，并返回给client driver一个传输描述符。
>
> 8）然后，client driver会调用dmaengine_submit将该传输提交给controller driver，此时dmaengine会调用controller driver为每个传输描述符所提供的tx_submit回调函数，controller driver需要在这个函数中将描述符挂到该channel对应的传输队列中。
>
>  9）client driver开始传输时，会调用dma_async_issue_pending，controller driver需要在对应的回调函数（device_issue_pending）中，依次将队列上所有的传输请求提交给硬件。
>
> 10）等等。

## 6. 参考文献

[1] linux内核之dmaengine_heliangbin87的专栏-CSDN博客_dma_async_issue_pending

[2] Documentation/dmaengine/provider.txt

[3] Documentation\dmaengine\client.txt

[4] Documentation\dmaengine\dmatest.txt

[5] Documentation\crypto\async-tx-api.txt

[6] Documentation\DMA-API-HOWTO.txt

[7] Documentation\DMA-API.txt

# Linux内核4.14版本——DMA Engine框架分析(4)_dmatest.c分析

## dmatest 代码分析
dmatest是内核的一个测试dma模块的代码，代码位置位于内核的./drivers/dma/dmatest.c，关于dmatest模块的使用可以参考内核提供的文档./Documentation/dmaengine/dmatest.txt，这里只简单地介绍一下。

## 编译加载模块

配置支持位置：

```
Device Drivers -> DMA Engine support -> DMA Test client
```

或者直接修改配置文件`CONFIG_DMATEST=m`，编译成模块。

可以直接加载 dmatest.ko，然后echo相关参数到/sys/module/dmatest/parameters/下，echo 1 > /sys/module/dmatest/parameters/run 驱动就会执行测试过程。
![在这里插入图片描述](DMA Engine框架分析.assets/20201016162507490.png)

## 代码分析

### 模块注册

先看模块的初始化过程：

![在这里插入图片描述](DMA Engine框架分析.assets/20201016162540536.png)

初始过程里面用到了两个结构体，我们来看一下。

`struct dmatest_info`：

![在这里插入图片描述](DMA Engine框架分析.assets/20201016162557339.png)

struct dmatest_params ：

![在这里插入图片描述](https://i2.wp.com/img-blog.csdnimg.cn/20201016162617445.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3dlaXhpbl8zODg3ODUxMA==,size_16,color_FFFFFF,t_70#pic_center)

相关的模块参数就一开始有定义，具体就不多说了，是何上面的结构体基本对应的。

![在这里插入图片描述](https://i2.wp.com/img-blog.csdnimg.cn/20201016162633153.png?x-oss-process=image/watermark,type_ZmFuZ3poZW5naGVpdGk,shadow_10,text_aHR0cHM6Ly9ibG9nLmNzZG4ubmV0L3dlaXhpbl8zODg3ODUxMA==,size_16,color_FFFFFF,t_70#pic_center)

![在这里插入图片描述](DMA Engine框架分析.assets/20201016162648228.png)

这里的run参数指定加载模块时知否直接执行测试：

![在这里插入图片描述](DMA Engine框架分析.assets/2020101616270341.png)

这里的wait参数指定加载模块执行测试用例时是否需要等待测试线程执行结束：

![在这里插入图片描述](DMA Engine框架分析.assets/20201016162713768.png)

### 通道申请

注册过程中，会调用run_threaded_test()进行通道申请，海思的hiedmacv310.c中只注册了DMA_MEMCPY，DMA_SLAVE，DMA_CYCLIC，所以这个模块在这里只能测试DMA_MEMCPY。至于各种传输类型的dma，可以查看内核的文档./Documentation/dmaengine/，后面有机会再介绍一下，这里略过。

![在这里插入图片描述](DMA Engine框架分析.assets/20201016162727457.png)

run_threaded_test()调用具体的request_channels()函数进行通道申请 ：

![在这里插入图片描述](DMA Engine框架分析.assets/20201016162737591.png)

### 申请通道附加资源

上面申请通道完成后，调用dmatest_add_channel()，这里申请资源来保存申请成功的dma通道指针，并将申请的资源保存到相关链表中：

![在这里插入图片描述](DMA Engine框架分析.assets/20201016162749978.png)

### 创建线程

dmatest_add_channel()调用dmatest_add_threads来创建工作线程，设置线程任务为dmatest_func()后唤醒线程：

![在这里插入图片描述](DMA Engine框架分析.assets/20201016162805171.png)

### 工作任务

dmatest_func()函数比较长：

![在这里插入图片描述](DMA Engine框架分析.assets/20201016162849958.png)

![在这里插入图片描述](DMA Engine框架分析.assets/20201016162934624.png)

![在这里插入图片描述](DMA Engine框架分析.assets/20201016162948427.png)

![在这里插入图片描述](DMA Engine框架分析.assets/20201016163011159.png)

![在这里插入图片描述](DMA Engine框架分析.assets/20201016163026609.png)

![在这里插入图片描述](DMA Engine框架分析.assets/20201016163039515.png)

![在这里插入图片描述](DMA Engine框架分析.assets/2020101616305275.png)

![在这里插入图片描述](DMA Engine框架分析.assets/20201016163111978.png)

回头看一下回调函数的内容，也比较简单，就是设置变量done为true之后唤醒等待队列wait：

![在这里插入图片描述](DMA Engine框架分析.assets/20201016163130143.png)

### 停止传输

协助模块时会调用stop_threaded_test()将传输停止，看一下具体代码实现，遍历链表将所有链表资源进行释放，通道资源进行释放：

![在这里插入图片描述](DMA Engine框架分析.assets/2020101616314362.png)

再调用dmatest_cleanup_channel()进行资源清理：

![在这里插入图片描述](DMA Engine框架分析.assets/20201016163157225.png)

至此，整个dmatest的代码基本分析完成了。

# dma engine 相关代码分析

我们首先看一下内核
Documentation关于DMA相关描述的文档，
Documentation/dmaengine/client.txt是DMA引擎API指南，是我们主要关心的使用指南；
Documentation/dmaengine/provider.txt是DMA引擎控制器文档，我们只做了解即可。

## 1.DMA引擎API指南

注意:对于在async_tx中使用DMA引擎，请参阅: Documentation/crypto/async-tx-api.txt

从DMA使用包括以下步骤:

1. 申请从DMA通道
2. 设置从和控制器的具体参数
3. 获取事务的描述符
4. 提交事务
5. 发出挂起的请求并等待回调通知

## 申请从DMA通道

通道分配在从DMA上下文中略有不同，客户驱动程序通常只需要从一个特定的DMA控制器的通道，甚至在某些情况下需要一个特定的通道。
为了请求通道，使用了dma_request_chan() API。

````c
struct dma_chan *dma_request_chan(struct device *dev, const char *name);
````

它将找到并返回与dev设备关联的name DMA通道。通过DT, ACPI或基础板级文件dma_slave_map匹配表进行匹配。

在调用dma_release_channel()之前，通过这个接口分配的通道是独占的。

## 设置从设备和控制器的具体参数

下一步总是将一些特定的信息传递给DMA驱动程序。从DMA可以使用的大多数通用信息都在struct dma_slave_config中。这允许客户为外设指定DMA方向，DMA地址，总线宽度，DMA突发长度等。

如果一些DMA控制器有更多的参数要发送，那么它们应该尝试在它们的控制器特定结构中嵌入struct dma_slave_config。这为客户端提供了在需要时传递更多参数的灵活性。

```c
int dmaengine_slave_config(struct dma_chan *chan,
                  struct dma_slave_config *config)
```

请参阅dmaengine.h中的dma_slave_config结构定义，以获得结构成员的详细说明。请注意，direction成员将消失，因为它与准备发起调用中给出的指导重复。

## 获取事务的描述符

对于从属使用，DMA引擎支持各种从属传输模式：

- slave_sg ： DMA来自/到外设的分散收集缓冲区列表
- dma_cyclic : 从/向外围设备执行循环DMA操作，直到操作明确停止为止。
- interleaved_dma : 这对于从属以及M2M客户端都是常见的。 对于设备的从属地址，驱动程序可能已经知道fifo。 通过为dma_interleaved_template成员设置适当的值，可以表示各种类型的操作。

此传输API的非NULL返回表示给定事务的“描述符”。

```c
struct dma_async_tx_descriptor *dmaengine_prep_slave_sg(
        struct dma_chan *chan, struct scatterlist *sgl,
        unsigned int sg_len, enum dma_data_direction direction,
        unsigned long flags);

struct dma_async_tx_descriptor *dmaengine_prep_dma_cyclic(
        struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
        size_t period_len, enum dma_data_direction direction);

struct dma_async_tx_descriptor *dmaengine_prep_interleaved_dma(
        struct dma_chan *chan, struct dma_interleaved_template *xt,
        unsigned long flags);
```

在调用dmaengine_prep_slave_sg()之前，外围设备驱动程序应该已为DMA操作映射了分散列表，并且必须保持该分散列表已映射，直到DMA操作完成为止。 散列表必须使用DMA结构设备进行映射。 如果以后需要同步映射，则也必须使用DMA结构设备调用dma_sync_*_for_*()。
因此，正常设置应如下所示：

```c
 nr_sg = dma_map_sg(chan->device->dev, sgl, sg_len);
    if (nr_sg == 0)
        /* error */
   
    desc = dmaengine_prep_slave_sg(chan, sgl, nr_sg, direction, flags);
```

一旦获得描述符，就可以添加回调信息，然后必须提交描述符。 某些DMA引擎驱动程序可能会在成功准备和提交之间保持自旋锁，因此，将这两个操作紧密配对非常重要。

注意：

> 尽管async_tx API指定完成回调例程无法提交任何新操作，但从属/循环DMA并非如此。
>
> 对于从DMA，在调用回调函数之前，后续事务可能无法提交，因此允许从DMA回调准备和提交新事务。
>
> 对于循环DMA，回调函数可能希望通过dmaengine_terminate_async()终止DMA。
>
> 因此，重要的是DMA引擎驱动程序在调用可能导致死锁的回调函数之前放弃所有锁。
>
> 注意，回调将始终从DMA引擎tasklet调用，而不是从中断上下文调用。

## 提交事务

准备好描述符并添加回调信息后，必须将其放在DMA引擎驱动程序挂起队列中。

```c
dma_cookie_t dmaengine_submit(struct dma_async_tx_descriptor *desc)
```

这将返回一个cookie，可用于通过本文档未涵盖的其他DMA引擎调用来检查DMA引擎活动的进度。

dmaengine_submit()将不会启动DMA操作，它只会将其添加到挂起的队列中。 为此，请参阅步骤5，ma_async_issue_pending。

## 发出挂起的请求并等待回调通知

可以通过调用issue_pending API来激活挂起队列中的事务。 如果通道空闲，则队列中的第一个事务开始，随后的队列排队。

完成每个DMA操作后，将启动下一个入队列并触发Tasklet。 然后，tasklet将调用客户端驱动程序完成回调例程进行通知（如果已设置）。

```c
void dma_async_issue_pending(struct dma_chan *chan);
```

其他API

### 1. terminate

```c
int dmaengine_terminate_sync(struct dma_chan *chan)
int dmaengine_terminate_async(struct dma_chan *chan)
int dmaengine_terminate_all(struct dma_chan *chan) /* DEPRECATED */
```

这将导致DMA通道的所有活动停止，并且可能会丢弃DMA FIFO中尚未完全传输的数据。 任何不完整的传输都不会调用回调函数。

此功能有两个变体。

dmaengine_terminate_async()可能不会等到DMA完全停止或任何正在运行的完整回调完成后才开始。 但是可以从原子上下文或从完整的回调中调用dmaengine_terminate_async()。 必须先调用dmaengine_synchronize()，然后才能安全释放DMA传输访问的内存或释放从完整回调中访问的资源。

dmaengine_terminate_sync()将等待传输以及所有正在运行的完整回调在返回之前完成。 但是，不得从原子上下文或完整的回调中调用该函数。

dmaengine_terminate_all()已弃用，不应在新代码中使用。

### 2. pause

```c
int dmaengine_pause(struct dma_chan *chan)
```

这将暂停DMA通道上的活动，而不会丢失数据。

### 3. resume

```c
int dmaengine_resume(struct dma_chan *chan)
```

恢复先前暂停的DMA通道。 恢复当前未暂停的频道是无效的。

### 4. complete

```c
enum dma_status dma_async_is_tx_complete(struct dma_chan *chan,
        dma_cookie_t cookie, dma_cookie_t *last, dma_cookie_t *used)
```

这可用于检查通道的状态。 请参阅include/linux/dmaengine.h中的文档以获取有关此API的更完整说明。

可以将其与dma_async_is_complete()和dmaengine_submit()返回的cookie结合使用，以检查特定DMA事务的完成。

注意：

```c
并非所有的DMA引擎驱动程序都可以为正在运行的DMA通道返回可靠的信息。 建议DMA引擎用户在使用此API之前暂停或停止（通过dmaengine_terminate_all()）通道。
```

### 5. synchronize

`void dmaengine_synchronize(struct dma_chan *chan)`

将DMA通道的终止于当前上下文同步。

此函数应在dmaengine_terminate_async()之后使用，以将DMA通道的终止同步到当前上下文。 该函数将等待传输以及所有正在运行的完整回调在返回之前完成。

如果使用dmaengine_terminate_async()停止DMA通道，则必须先调用此函数，然后才能安全释放先前提交的描述符访问的内存或释放先前提交的描述符的完整回调中访问的任何资源。

如果在dmaengine_terminate_async()和此函数之间调用了dma_async_issue_pending()，则此函数的行为是不确定的。

## 2.DMA引擎控制器文档

## 硬件介绍

大多数从DMA控制器具有相同的通用操作原理。

它们具有给定数量的通道用于DMA传输，以及给定数量的请求行。

请求和通道几乎是正交的。通道可用于为多个请求提供服务。为简化起见，通道是将要进行复制的实体，并请求涉及哪些端点。

请求线实际上对应于从DMA合格设备到控制器本身的物理线。每当设备将要开始传输时，它将通过声明该请求行来声明DMA请求（DRQ）。

一个非常简单的DMA控制器将只考虑一个参数：传输大小。在每个时钟周期，它将一个字节的数据从一个缓冲区传输到另一个缓冲区，直到达到传输大小为止。

由于从设备可能需要在单个周期内传输特定数量的位，因此在现实世界中效果不佳。例如，当执行简单的内存复制操作时，我们可能希望传输尽可能多的物理总线所允许的数据，但是我们的音频设备可能具有更窄的FIFO，要求一次将数据精确地写入16或24位。这就是为什么大多数（如果不是全部）DMA控制器都可以使用称为传输宽度的参数来进行调整的原因。

此外，无论何时将RAM用作源或目标，某些DMA控制器都可以将对内存的读取或写入分组到一个缓冲区中，因此您不必进行大量的小内存访问（这不是很有效），而是可以几个更大的转移。这是使用称为突发大小的参数完成的，该参数定义了在不将控制器拆分为较小的子传输的情况下允许执行的单次读取/写入的次数。

这样，理论上的DMA控制器将只能执行涉及单个连续数据块的传输。但是，我们通常不进行某些传输，并且希望将数据从非连续缓冲区复制到连续缓冲区，这称为分散收集。

至少对于mem2dev传输，DMAEngine需要支持分散收集。因此，这里有两种情况：要么我们有一个不支持它的非常简单的DMA控制器，而我们必须在软件中实现它，要么有一个更高级的DMA控制器，它以硬件分散性实现了-收集。

后者通常使用一组块进行编程以进行传输，并且每当传输开始时，控制器就会遍历该集合，并执行我们在此处进行的编程。

该集合通常是一个表或一个链表。然后，您将表的地址及其元素数或列表的第一项推入DMA控制器的一个通道，并且每当断言DRQ时，它将遍历该集合以知道在哪里获取数据来自。

无论哪种方式，此集合的格式都完全取决于您的硬件。每个DMA控制器将需要不同的结构，但是对于每个块，所有DMA控制器都至少需要源地址和目标地址，是否应增加这些地址以及我们前面看到的三个参数：突发大小，传输宽度和转印尺寸。

最后一件事是通常情况下，从设备默认情况下不会发出DRQ，并且每当您愿意使用DMA时，必须首先在从设备驱动程序中启用它。

这些只是一般的内存到内存（也称为mem2mem）或内存到设备（mem2dev）的传输。大多数设备通常支持dmaengine支持的其他类型的传输或内存操作，这将在本文档的后面进行详细介绍。

## Linux中的DMA支持

从历史上看，DMA控制器驱动程序是使用异步TX API实现的，以卸载诸如内存复制，XOR，加密等之类的操作，基本上是任何内存到内存的操作。

随着时间的流逝，内存到设备传输的需求不断增加，并且dmaengine得以扩展。 如今，异步TX API被编写为dmaengine之上的一层，并充当客户端。 尽管如此，dmaengine在某些情况下仍可容纳该API，并做出了一些设计选择以确保其兼容。

有关Async TX API的更多信息，请参阅Documentation/crypto/async-tx-api.txt中的相关文档文件。

## DMA引擎注册

### struct dma_device初始化

与其他任何内核框架一样，整个DMAEngine注册都依赖于驱动程序填充结构并针对该框架进行注册。 在我们的例子中，该结构是dma_device。

您需要在驱动程序中做的第一件事就是分配此结构。 任何常用的内存分配器都可以执行，但是您还需要在其中初始化一些字段：

- channels : 例如，应使用INIT_LIST_HEAD宏将其初始化为列表
- src_addr_widths : 应包含支持的源传输宽度的位掩码
- dst_addr_widths : 应包含支持的目标传输宽度的位掩码
- directions : 应该包含受支持的从站方向的位掩码（例如，不包括mem2mem传输）
- residue_granularity :
  - 报告给dma_set_residue的转移残基的粒度。
  - 可以是：
    - 描述符（Descriptor） ： 设备不支持任何类型的残留物报告。 框架将仅知道完成了特定的事务描述符。
    - 区段（Segment）：设备能够报告已传输的块
    - 突发（Burst）：设备能够报告已传输的突发
- dev : 应该持有指向与当前驱动程序实例关联的结构设备的指针。

### 支持的事务类型

接下来，您需要设置设备（和驱动程序）支持的事务类型。

我们的dma_device结构具有一个名为cap_mask的字段，该字段保存受支持的各种事务类型，您需要使用dma_cap_set函数修改此掩码，并根据支持的事务类型将各种标志用作参数。

所有这些功能均在include/linux/dmaengine.h中的dma_transaction_type枚举中定义。

当前，可用的类型为：

- DMA_MEMCPY : 设备能够执行内存到内存拷贝。
- DMA_XOR : 该设备能够在存储区上执行XOR操作。用于加速XOR密集型任务，例如RAID5。
- DMA_XOR_VAL : 该设备能够使用XOR算法对内存缓冲区执行奇偶校验。
- DMA_PQ : 该设备能够执行RAID6 P + Q计算，P是简单的XOR，Q是Reed-Solomon算法。
- DMA_PQ_VAL : 该设备能够使用RAID6 P + Q算法针对内存缓冲区执行奇偶校验。
- DMA_INTERRUPT : 该设备能够触发虚拟传输，从而产生周期性中断。由客户端驱动程序用于注册回调，该回调将通过DMA控制器中断定期调用。
- DMA_SG : 该设备支持内存到内存的分散收集传输。尽管普通的memcpy看起来像是散点聚集转移的特殊情况，只有一个要转移的块，但是在mem2mem转移情况下，它是一种独特的事务类型。
- DMA_PRIVATE : 设备仅支持从属传输，因此不适用于异步传输。
- DMA_ASYNC_TX : 不得由设备设置，如果需要，将由框架设置。
- DMA_SLAVE : 设备可以处理设备到内存的传输，包括分散收集传输。在mem2mem情况下，我们有两种不同的类型来处理要复制的单个块或它们的集合，而在这里，我们只有一个应该处理这两种类型的事务类型。如果要传输单个连续的内存缓冲区，只需构建一个仅包含一项的分散列表。
- DMA_CYCLIC : 该设备可以处理循环传输。循环传输是块收集将在其自身上循环的传输，最后一项指向第一个。它通常用于音频传输，您要在单个环形缓冲区上进行操作，并在其中填充音频数据。
- DMA_INTERLEAVE : 设备支持交错传输。这些传输可以将数据从不连续的缓冲区传输到不连续的缓冲区，这与DMA_SLAVE相对，即将数据从不连续的数据集传输到连续的目标缓冲区。它通常用于2D内容传输，在这种情况下，您要将部分未压缩的数据直接传输到显示器以进行打印。

这些各种类型还将影响源地址和目标地址随时间变化的方式。

每次传输后，指向RAM的地址通常会增加（或增加）。 如果是环形缓冲区，它们可能会循环（DMA_CYCLIC）。 指向设备寄存器（例如FIFO）的地址通常是固定的。

### 设备操作

既然我们描述了我们能够执行的操作，我们的dma_device结构还需要一些函数指针才能实现实际的逻辑。

我们必须在那里填充并因此必须实现的功能显然取决于您报告为受支持的事务类型。

- device_alloc_chan_resources/device_free_chan_resources
  - 每当驱动程序在与该驱动程序关联的通道上第一次/最后一次调用dma_request_channel或dma_release_channel时，将调用这些函数。
  - 他们负责分配/释放所有必需的资源，以使该通道对您的驱动程序有用。
  - 这些功能可以休眠。
- device_prep_dma_*
  - 这些功能与您先前注册的功能匹配。
  - 这些函数全部采用与准备传输有关的缓冲区或分散列表，并应从中创建硬件描述符或硬件描述符列表。
  - 可以从中断上下文中调用这些函数
  - 您可能要做的任何分配都应使用GFP_NOWAIT标志，以免潜在进入睡眠状态，而又不会耗尽应急池。
  - 驱动程序应尝试在探测时预先分配在传输设置过程中可能需要的所有内存，以避免给nowait分配器造成很大压力。
  - 它应返回dma_async_tx_descriptor结构的唯一实例，该实例进一步表示此特定传输。
  - 可以使用功能dma_async_tx_descriptor_init初始化此结构。
  - 您还需要在此结构中设置两个字段：
    - flags ： 它可以由驱动程序本身修改，还是它应该始终是参数中传递的标志吗
    - tx_submit ： 指向必须实现的函数的指针，该函数应该将当前事务描述符推入挂起的队列，等待issue_pending被调用。
  - 在这种结构中，可以初始化函数指针callback_result，以便通知提交者事务已完成。 在较早的代码中，已使用函数指针回调。 但是，它不为交易提供任何内容，因此将不推荐使用。 传递给callback_result的定义为dmaengine_result的结果结构具有两个字段：
    - result : 这提供了由dmaengine_tx_result定义的传输结果。 成功或某些错误情况。
    - residue : 为支持残留的对象提供传输的残留字节。
- device_issue_pending
  - 获取挂起队列中的第一个事务描述符，然后开始传输。 无论何时完成转移，它都应移至列表中的下一个事务。
  - 可以在中断上下文中调用此函数。
- device_tx_status
  - 应该报告给定通道上剩余的字节数
  - 只应关心作为参数传递的事务描述符，而不是给定通道上当前活动的事务描述符
  - tx_state参数可能为NULL
  - 应该使用dma_set_residue进行报告
  - 如果是周期性传输，则应仅考虑当前期间。
  - 可以在中断上下文中调用此函数。
- device_config
  - 使用参数作为参数重新配置通道
  - 此命令不应同步执行，也不应在当前排队的传输中执行，而只能在后续传输中执行
  - 在这种情况下，该函数将接收dma_slave_config结构指针作为参数，该指针将详细说明要使用的配置。
  - 即使该结构包含一个Direction字段，但不赞成使用此字段，而推荐给prep_ *函数使用direction参数
  - 此调用仅对于从属操作是必需的。 不应为memcpy操作设置或期望将其设置为。 如果驱动程序同时支持这两种驱动程序，则应将此调用仅用于从属操作，而不用于memcpy操作。
- device_pause
  - 暂停通道上的传输
  - 该命令应在通道上同步运行，立即暂停给定通道的工作
- device_resume
  - 恢复通道上的传输
  - 该命令应在通道上同步运行，立即恢复给定通道的工作
- device_terminate_all
  - 中止通道上所有待处理和正在进行的传输
  - 对于中止的传输，不应调用完整的回调
  - 可以从原子上下文或在描述符的完整回调中调用。 绝对不能休眠。 驱动程序必须能够正确处理此问题。
  - 终止可能是异步的。 驱动程序不必等到当前活动的传输完全停止。 请参阅device_synchronize。
- device_synchronize
  - 必须将通道的终止与当前上下文同步。
  - 必须确保DMA控制器不再访问先前提交的描述符的内存。
  - 必须确保先前提交的描述符的所有完整回调都已完成运行，并且没有一个计划运行。
  - 可能休眠

### 杂项说明

- dma_run_dependencies
  - 应该在异步TX传输结束时调用，在从属传输情况下可以忽略。
  - 在将其标记为完成之前，请确保已运行相关操作。
- dma_cookie_t
  - 它是DMA事务ID，将随着时间的推移而增加。
  - 自从引入virt-dma并将其抽象化以来，它不再具有实际意义。
- DMA_CTRL_ACK
  - 如果清除，则描述符只能由提供者重用，直到客户端确认收到，即有机会建立任何依赖关系链
  - 可以通过调用async_tx_ack()来确认
  - 如果设置，并不意味着描述符可以重用
- DMA_CTRL_REUSE
  - 如果设置，描述符在完成后可以重用。 如果设置了此标志，则提供程序不应释放它。
  - 应该通过调用设置DMA_CTRL_REUSE的dmaengine_desc_set_reuse()来准备描述符以供重用。
  - dmaengine_desc_set_reuse() 仅在容量支持的通道上行可重用描述符时成功。
  - 因此，如果设备驱动程序要在两次传输之间跳过ma_map_sg()和dma_unmap_sg())，则由于未使用DMA数据，它可以在传输完成后立即重新提交传输。
  - 描述符可以通过几种方式释放
    - 通过调用dmaengine_desc_clear_reuse()并提交最后的txn来清除DMA_CTRL_REUSE
    - 明确调用dmaengine_desc_free()，只有在已设置DMA_CTRL_REUSE的情况下，此操作才能成功
    - 终止通道

一般设计说明：

> 您将看到的大多数DMAEngine驱动程序都基于类似的设计，该处理程序在处理程序中处理传输中断的结束，但是将大多数工作推迟到Tasklet，包括在先前的传输结束时开始新的传输。
>
> 但是，这是一个效率很低的设计，因为延迟不仅是中断延迟，而且是Tasklet的调度延迟，这将使通道之间处于空闲状态，这将减慢全局传输速率。
>
> 您应该避免这种做法，而不是在tasklet中选择新的转移，而是将该部分移到中断处理程序中，以使空闲窗口更短（无论如何我们还是无法避免）。



## 3.代码分析

接下来就从dmatest.c的代码开始着手分析。

## 申请从DMA通道

申请通道使用dma_request_channel()函数进行申请，比如dmatest.c里面的下面这段代码：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165116972.png)

看一下函数原型，dma_request_channel()是一个宏定义，最后调用的是__dma_request_channel()函数，申请通道时遍历已注册设备的dma设备列表，并找到具体的候选者：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165130891.png)

看一下find_candidate()是如何寻找候选者的，先通过private_candidate()找到候选通道，找到之后设置设置为私有，并尝试找到dma通道的父驱动模块：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165143343.png)

再看一下private_candidate()函数，主要就是匹配能力集以及遍历所有通道，并找到到空闲的通道，最后调用filter函数进行过滤，并返回找到的通道：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165155460.png)

这个时候申请通道就完成了。

## 设置从设备和控制器的具体参数

在dmatest这里不需要进行从设备和控制器的具体参数。

## 获取事务的描述符

这个时候需要将要传输的目的地址、源地址、传输长度等数据告诉申请到的通道，用来准备传输描述符，比如下面这个device_prep_dma_memcpy()：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165208100.png)

我们看一下这个函数具体是做了什么操作，这个函数是dma控制器注册时赋值的函数，当前控制器代码是海思提供的hiedmacv310，对应的函数是hiedmac_prep_dma_memcpy()，是控制器注册时候赋值的，后面再分析一下注册的代码，这里看hiedmac_prep_dma_memcpy()：

![在这里插入图片描述](DMA Engine框架分析.assets/2020101916522133.png)

可以看到这个函数最后调用了vchan_tx_prep()去准备一个标准的dma传输描述符：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165232402.png)

然后dmatest.c里面的就可以用这个描述符进行下一步了。

## 提交事务

接下来看一下dmatest里面是如何进行提交事务的：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165243586.png)

设置回调函数之后，使用tx_submit()进行提交传输事务，这个tx_submit()是在上一步的vchan_tx_prep()里面准备好的vchan_tx_submit()，可以看到这个函数实际并没有开始传输，只是把描述符增加到submitted链表里：

![在这里插入图片描述](DMA Engine框架分析.assets/2020101916525476.png)

## 发出挂起的请求并等待回调通知

### 发起请求

再看一下如何发起请求，直接调用dma_async_issue_pending()函数：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165305988.png)

我们看一下dma_async_issue_pending()函数，调用的是控制器注册时的函数hiedmac_issue_pending() ：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165324914.png)

我们看一下hiedmac_issue_pending()是如何处理的，物理通道空闲时发起传输：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165336409.png)

调用hiedmac_start()获取物理通道后写寄存器发起传输，具体的写寄存器过程就不分析了。

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165346809.png)

### 中断返回及回调通知

上面交给硬件的dma控制器后，传输完成后就会调用到中断回调函数hiemdacv310_irq()，主要关心的是下一个部分，报告传输完成部分：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165357598.png)

![在这里插入图片描述](DMA Engine框架分析.assets/2020101916541025.png)

我们看一下vchan_cookie_complete()函数，报告描述符传输完成，保存cookie后加到完成链表里，然后调度tasklet：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165420786.png)

dma_cookie_complete()函数比较简单，记录完成的cookie并清理当前cookie：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165430855.png)

再回来看一下tasklet，这个是在vchan_init()时注册的tasklet任务，执行的函数是vchan_complete()，主要工作是获取传输描述符的回调函数，并发起回调：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165440691.png)

dmaengine_desc_get_callback()函数比较简单，就是一个赋值过程，此时这里的tx->callback是dmatest.c里面的dmatest_callback()函数：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165450803.png)

再看dmaengine_desc_callback_invoke()，就是调用注册的回调函数：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165506956.png)

这时候，dmatest_callback()函数被执行，等待队列被唤醒：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165518991.png)

### 判断是否传输成功

回调完成后，再判断一下是否传输成功，如dmatest示例：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165530450.png)

dma_async_is_tx_complete()轮询传输是否完成：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165541156.png)

回调到hiedmac_tx_status()，调用dma_cookie_status()查询是否完成：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165550472.png)

dma_cookie_status()：根据返回的cookie值以及当前在使用cookie值、上一个完成的cookie值来判断传输状态：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165603115.png)

判断逻辑如下图所示：每次传输完成后cookie值都是+1的，应该就是根据返回的cookie以及上一个完成的cookie来判断。

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165613535.png)

## 停止传输

在dmatest传输任务退出或者模块退出时，会调用终止传输的函数，比如模块退出时调用的：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165623767.png)

dmaengine_terminate_all()回调至hiedmac_terminate_all()：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165645566.png)

我们看一下hiedmac_terminate_all()函数，除了将本模块的资源进行释放外，还调用hiedmac_free_txd_list()函数将vchan的资源进行释放：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165656785.png)

看一下hiedmac_free_txd_list()，将vchan的所有描述符移进行释放处理：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165707262.png)

再看一下vchan_get_all_descriptors()获取全部描述符的过程，就是将所有链表内容移到到head：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165716846.png)

vchan_dma_desc_free_list()就是删除节点并释放资源：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165727150.png)

## 释放通道资源

在dmatest模块移除的时候，会调用dma_release_channel()进行通道的释放，如下图所示：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165736658.png)

dma_release_channel()调用dma_chan_put()减少引用计数：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165746236.png)

dma_chan_put()回调至hiedmac_free_chan_resources()：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165754924.png)

hiedmac_free_chan_resources()调用vchan_free_chan_resources()的资源释放函数：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165805741.png)

vchan_free_chan_resources()获取通道的所有描述符后清除reuse标志，并对资源描述符进行释放：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165816457.png)

至此，一个完整的传输过程代码算是分析完成了。

## 4. HIEDMAC控制器注册

## 驱动的注册

hiedmac注册的是平台驱动，匹配表需要与设备树进行对应，否则驱动注册成功，但设备树解析失败的话控制器也没有办法正常使用。

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165830581.png)

设备树内容如下：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165840211.png)

## probe探测函数

hiedmacv310_probe-1：解析设备树以及设置DMA_MEMCPY能力集。

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165851277.png)

看一下设备树解析的过程：

get_of_probe-1：主要获取时钟、复位等，并对寄存器空间进行ioremap映射。

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165902866.png)

get_of_probe-2：获取硬件终中断号并映射，以及获取硬件支持的dma通道数、dma请求数。

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165916882.png)

当前海思平台支持的dma通道数与dma请求数看下图：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165926750.png)

hiedmacv310_probe-2：设置DMA_CLAVE能力集，申请中断，并注册中断回调为hiemdacv310_irq()函数。涉及到的函数部分在上面已经分析过了，这里就不做分析了。

![在这里插入图片描述](DMA Engine框架分析.assets/2020101916593811.png)

hiedmacv310_probe-3：初始物理通道，并注册memcpy,slave虚拟通道。

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165948527.png)

看一下注册虚拟设备的过程hiedmac_init_virt_channels()函数，主要是调用vchan_init()函数去初始化虚拟通道：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019165958618.png)

vchan_init()函数主要是初始化一个tasklet，执行任务vchan_complete()函数，函数具体内容上面有分析过：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019170007800.png)

至于dma_async_device_register()函数，暂时就不细看了，注册设备后增加到dma_device_list链表后：

![在这里插入图片描述](DMA Engine框架分析.assets/20201019170357116.png)

那么代码就暂时分析到这里了。

# Linux内核4.14版本——DMA Engine框架分析(5) -DMA mapping

## 1. 前言

这是一篇指导驱动工程师如何使用DMA API的文档，为了方便理解，文档中给出了伪代码的例程。另外一篇文档dma-api.txt给出了相关API的简明描述，有兴趣也可以看看那一篇，这两份文档在DMA API的描述方面是一致的。

## 2. 从CPU角度看到的地址和从DMA控制器看到的地址有什么不同？

 在DMA API中涉及好几个地址的概念（物理地址、虚拟地址和总线地址），正确的理解这些地址是非常重要的。

   内核通常使用的地址是虚拟地址。我们调用kmalloc()、vmalloc()或者类似的接口返回的地址都是虚拟地址，保存在”void *”的变量中。

虚拟内存系统（TLB、页表等）将虚拟地址（程序角度）翻译成物理地址（CPU角度），物理地址保存在“phys_addr_t”或“resource_size_t”的变量中。对于一个硬件设备上的寄存器等设备资源，内核是按照物理地址来管理的。通过/proc/iomem，你可以看到这些和设备IO 相关的物理地址。当然，驱动并不能直接使用这些物理地址，必须首先通过ioremap()接口将这些物理地址映射到内核虚拟地址空间上去。
 I/O设备使用第三种地址：“总线地址”。如果设备在MMIO地址空间中有若干的寄存器，或者该设备足够的智能，它可以通过DMA执行读写系统内存的操作，这些情况下，设备使用的地址就是总线地址。在某些系统中，总线地址与CPU物理地址相同，但一般来说它们不是。iommus和host bridge可以在物理地址和总线地址之间进行映射。

从设备的角度来看，DMA控制器使用总线地址空间，不过可能仅限于总线空间的一个子集。例如：即便是一个系统支持64位地址内存和64 位地址的PCI bar，但是DMA可以不使用全部的64 bit地址，通过IOMMU的映射，PCI设备上的DMA可以只使用32位DMA地址。

我们用下面这样的系统结构来说明各种地址的概念：

![img](DMA Engine框架分析.assets/20180419165650226)

在PCI设备枚举（初始化）过程中，内核了解了所有的IO device及其对应的MMIO地址空间（MMIO是物理地址空间的子集），并且也了解了是PCI主桥设备将这些PCI device和系统连接在一起。PCI设备会有BAR（base address register），表示自己在PCI总线上的地址，CPU并不能通过总线地址A（位于BAR范围内）直接访问总线上的PCI设备，PCI host bridge会在MMIO（即物理地址）和总线地址之间进行mapping。因此，对于CPU，它实际上是可以通过B地址（位于MMIO地址空间）访问PCI设备（反正PCI host bridge会进行翻译）。地址B的信息保存在struct resource变量中，并可以通过/proc/iomem开放给用户空间。对于驱动程序，它往往是通过ioremap()把物理地址B映射成虚拟地址C，这时候，驱动程序就可以通过ioread32(C)来访问PCI总线上的地址A了。

 如果PCI设备支持DMA，那么在驱动中我们可以通过kmalloc或者其他类似接口分配一个DMA buffer，并且返回了虚拟地址X，MMU将X地址映射成了物理地址Y，从而定位了DMA buffer在系统内存中的位置。因此，驱动可以通过访问地址X来操作DMA buffer，但是PCI 设备并不能通过X地址来访问DMA buffer，因为MMU对设备不可见，而且系统内存所在的系统总线和PCI总线属于不同的地址空间。

在一些简单的系统中，设备可以通过DMA直接访问物理地址Y，但是在大多数的系统中，有一个IOMMU的硬件block用来将DMA可访问的总线地址翻译成物理地址，也就是把上图中的地址Z翻译成Y。理解了这些底层硬件，你也就知道类似dma_map_single这样的DMA API是在做什么了。驱动在调用dma_map_single这样的接口函数的时候会传递一个虚拟地址X，在这个函数中会设定IOMMU的页表，将地址X映射到Z，并且将返回z这个总线地址。驱动可以把Z这个总线地址设定到设备上的DMA相关的寄存器中。这样，当设备发起对地址Z开始的DMA操作的时候，IOMMU可以进行地址映射，并将DMA操作定位到Y地址开始的DMA buffer。

根据上面的描述我们可以得出这样的结论：Linux可以使用动态DMA 映射（dynamic DMA mapping）的方法，当然，这需要一些来自驱动的协助。所谓动态DMA 映射是指只有在使用的时候，才建立DMA buffer虚拟地址到总线地址的映射，一旦DMA传输完毕，就将之前建立的映射关系销毁。

虽然上面的例子使用IOMMU为例描述，不过本文随后描述的API也可以在没有IOMMU硬件的平台上运行。

顺便说明一点：DMA API适用于各种CPU arch，各种总线类型，DMA mapping framework已经屏蔽了底层硬件的细节。对于驱动工程师而言，你应该使用通用的DMA API（例如dma_map_*() 接口函数），而不是和特定总线相关的API（例如pci_map_*() 接口函数）

 驱动想要使用DMA mapping framework的API，需要首先包含相关头文件：

```c
#include <linux/dma-mapping.h>
```

这个头文件中定义了dma_addr_t这种数据类型，而这种类型的变量可以保存任何有效的DMA地址，不管是什么总线，什么样的CPU arch。驱动调用了DMA API之后，返回的DMA地址（总线地址）就是这种类型的。

## 3. 什么样的系统内存可以被DMA控制器访问到？

 既然驱动想要使用DMA mapping framework提供的接口，我们首先需要知道的就是是否所有的系统内存都是可以调用DMA API进行mapping？还是只有一部分？那么这些可以DMA控制器访问系统内存有什么特点？关于这一点，一直以来有一些不成文的规则，在本文中我们看看是否能够将其全部记录下来。

如果驱动是通过伙伴系统的接口（例如__get_free_page*()）或者类似kmalloc() or kmem_cache_alloc()这样的通用内存分配的接口来分配DMA buffer，那么这些接口函数返回的虚拟地址可以直接用于DMA mapping接口API，并通过DMA操作在外设和dma buffer中交换数据。

使用vmalloc() 分配的DMA buffer可以直接使用吗？最好不要这样，虽然强行使用也没有问题，但是终究是比较麻烦。首先，vmalloc分配的page frame是不连续的，如果底层硬件需要物理内存连续，那么vmalloc分配的内存不能满足硬件要求。即便是底层DMA硬件支持scatter-gather，vmalloc分配出来的内存仍然存在其他问题。我们知道vmalloc分配的虚拟地址和对应的物理地址没有线性关系（kmalloc或者__get_free_page*这样的接口，其返回的虚拟地址和物理地址有一个固定偏移的关系），而在做DMA mapping的时候，需要知道物理地址，有线性关系的虚拟地址很容易可以获取其物理地址，但是对于vmalloc分配的虚拟地址，我们需要遍历页表才可以找到其物理地址。

在驱动中定义的全局变量可以用于DMA吗？如果编译到内核，那么全局变量位于内核的数据段或者bss段。在内核初始化的时候，会建立kernel image mapping，因此全局变量所占据的内存都是连续的，并且VA和PA是有固定偏移的线性关系，因此可以用于DMA操作。不过，在定义这些全局变量的DMA buffer的时候，我们要小心的进行cacheline的对齐，并且要处理CPU和DMA controller之间的操作同步，以避免cache coherence问题。

如果驱动编译成模块会怎么样呢？这时候，驱动中的全局定义的DMA buffer不在内核的线性映射区域，其虚拟地址是在模块加载的时候，通过vmalloc分配，因此这时候如果DMA buffer如果大于一个page frame，那么实际上我们也是无法保证其底层物理地址的连续性，也无法保证VA和PA的线性关系，这一点和编译到内核是不同的。

 通过kmap接口返回的内存可以做DMA buffer吗？也不行，其原理类似vmalloc，这里就不赘述了。

 块设备使用的I/O buffer和网络设备收发数据的buffer是如何确保其内存是可以进行DMA操作的呢？块设备I/O子系统和网络子系统在分配buffer的时候会确保这一点的。

## 4. DMA寻址限制

你的设备有DMA寻址限制吗？不同的硬件平台有不同的配置方式，有的平台没有限制，外设可以访问系统内存的每一个Byte，有些则不可以。例如：系统总线有32个bit，而你的设备通过DMA只能驱动低24位地址，在这种情况下，外设在发起DMA操作的时候，只能访问16M以下的系统内存。如果设备有DMA寻址的限制，那么驱动需要将这个限制通知到内核。如果驱动不通知内核，那么内核缺省情况下认为外设的DMA可以访问所有的系统总线的32 bit地址线。对于64 bit平台，情况类似，不再赘述。

是否有DMA寻址限制是和硬件设计相关，有时候标准总线协议也会规定这一点。例如：PCI-X规范规定，所有的PCI-X设备必须要支持64 bit的寻址。

如果有寻址限制，那么在该外设驱动的probe函数中，你需要询问内核，看看是否有DMA controller可以支持这个外设的寻址限制。虽然有缺省的寻址限制的设定，不过最好还是在probe函数中进行相关处理，至少这说明你已经为你的外设考虑过寻址限制这事了。

 一旦确定了设备DMA寻址限制之后，我们可以通过下面的接口进行设定：

```c
int dma_set_mask_and_coherent(struct device *dev, u64 mask);
```

根据DMA buffer的特性，DMA操作有两种：一种是streaming，DMA buffer是一次性的，用完就算。这种DMA buffer需要自己考虑cache一致性。另外一种是DMA buffer是cache coherent的，软件实现上比较简单，更重要的是这种DMA buffer往往是静态的、长时间存在的。不同类型的DMA操作可能有有不同的寻址限制，也可能相同。如果相同，我们可以用上面这个接口设定streaming和coherent两种DMA 操作的地址掩码。如果不同，可以下面的接口进行设定：

```c
int dma_set_mask(struct device *dev, u64 mask);
 
int dma_set_coherent_mask(struct device *dev, u64 mask);
```

前者是设定streaming类型的DMA地址掩码，后者是设定coherent类型的DMA地址掩码。为了更好的理解这些接口，我们聊聊参数和返回值。dev指向该设备的struct device对象，一般来说，这个struct device对象应该是嵌入在bus-specific 的实例中，例如对于PCI设备，有一个struct pci_dev的实例与之对应，而在这里需要传入的dev参数则可以通过&pdev->dev得到（pdev指向struct pci_dev的实例）。mask表示你的设备支持的地址线信息。如果调用这些接口返回0，则说明一切OK，从该设备到指定mask的内存的DMA操作是可以被系统支持的（包括DMA controller、bus layer等）。

**如果返回值非0，那么说明这样的DMA寻址是不能正确完成的，如果强行这么做将会产生不可预知的后果。驱动必须检测返回值，如果不行，那么建议修改mask或者不使用DMA。也就是说，对上面接口调用失败后，你有三个选择：**

```
1、用另外的mask
2、不使用DMA模式，采用普通I/O模式
3、忽略这个设备的存在，不对其进行初始化
```

一个可以寻址32 bit的设备，其初始化的示例代码如下：

```c
if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32))) {
    dev_warn(dev, "mydev: No suitable DMA available\n");
    goto ignore_this_device;
}
```

 另一个常见的场景是有64位寻址能力的设备。一般来说我们会首先尝试设定64位的地址掩码，但是这时候有可能会失败，从而将掩码降低为32位。内核之所以会在设定64位掩码的时候失败，这并不是因为平台不能进行64位寻址，而仅仅是因为32位寻址比64位寻址效率更高。例如，SPARC64 平台上，PCI SAC寻址比DAC寻址性能更好。

 下面的代码描述了如何确定streaming类型DMA的地址掩码：

```c
int using_dac;
 
if (!dma_set_mask(dev, DMA_BIT_MASK(64))) {
    using_dac = 1;
} else if (!dma_set_mask(dev, DMA_BIT_MASK(32))) {
    using_dac = 0;
} else {
    dev_warn(dev, "mydev: No suitable DMA available\n");
    goto ignore_this_device;
}
```

 设定coherent 类型的DMA地址掩码也是类似的，不再赘述。需要说明的是：coherent地址掩码总是等于或者小于streaming地址掩码，因此，一般来说，我们只要设定了streaming地址掩码成功了，那么使用同样的掩码或者小一些的掩码来设定coherent地址掩码总是会成功，因此这时候我们一般就不检查dma_set_coherent_mask的返回值了，当然，有些设备很奇怪，只能使用coherent DMA，那么这种情况下，驱动需要检查dma_set_coherent_mask的返回值。

## 5.两种类型的DMA mapping

### 5.1 一致性DMA映射（Consistent DMA mappings ）

Consistent DMA mapping有下面两种特点：

> （1）持续使用该DMA buffer（不是一次性的），因此Consistent DMA总是在初始化的时候进行map，在shutdown的时候unmap。
> （2）**CPU和DMA controller在发起对DMA buffer的并行访问的时候不需要考虑cache的影响，也就是说不需要软件进行cache操作，CPU和DMA controller都可以看到对方对DMA buffer的更新。实际上一致性DMA映射中的那个Consistent实际上可以称为coherent，即cache coherent。**

**缺省情况下，coherent mask被设定为低32 bit（0xFFFFFFFF），即便缺省值是OK了，我们也建议你通过接口在驱动中设定coherent mask。**

一般使用Consistent DMA mapping的场景包括：

> （1）网卡驱动和网卡DMA控制器往往是通过一些内存中的描述符（形成环或者链）进行交互，这些保存描述符的memory一般采用Consistent DMA mapping。
> （2）SCSI硬件适配器上的DMA可以主存中的一些数据结构（mailbox command）进行交互，这些保存mailbox command的memory一般采用Consistent DMA mapping。
> （3）有些外设有能力执行主存上的固件代码（microcode），这些保存microcode的主存一般采用Consistent DMA mapping。

上面的这些例子有同样的特性：CPU对memory的修改可以立刻被device感知到，反之亦然。一致性映射可以保证这一点。

需要注意的是：一致性的DMA映射并不意味着不需要memory barrier这样的工具来保证memory order，CPU有可能为了性能而重排对consistent memory上内存访问指令。例如：如果在DMA consistent memory上有两个word，分别是word0和word1，对于device一侧，必须保证word0先更新，然后才有对word1的更新，那么你需要这样写代码：

```c
       desc->word0 = address;
        wmb();
        desc->word1 = DESC_VALID; 
```

只有这样才能保证在所有的平台上，给设备驱动可以正常的工作。

此外，在有些平台上，修改了DMA Consistent buffer后，你的驱动可能需要flush write buffer，以便让device侧感知到memory的变化。这个动作类似在PCI桥中的flush write buffer的动作。

### 5.2 流式DMA映射（streaming DMA mapping）

 流式DMA映射是一次性的，一般是需要进行DMA传输的时候才进行mapping，一旦DMA传输完成，就立刻ummap（除非你使用dma_sync_*的接口，下面会描述）。并且硬件可以为顺序化访问进行优化。

 这里的streaming可以被认为是asynchronous，或者是不属于coherent memory范围的。

一般使用streaming DMA mapping的场景包括：

(1）网卡进行数据传输使用的DMA buffer

（2）文件系统中的各种数据buffer，这些buffer中的数据最终到读写到SCSI设备上去，一般而言，驱动会接受这些buffer，然后进行streaming DMA mapping，之后和SCSI设备上的DMA进行交互。

设计streaming DMA mapping这样的接口是为了充分优化硬件的性能，为了打到这个目标，在使用这些接口的时候，你必须清清楚楚的知道调用接口会发生什么。

无论哪种类型的DMA映射都有对齐的限制，这些限制来自底层的总线，当然也有可能是某些总线上的设备有这样的限制。此外，如果系统中的cache并不是DMA coherent的，而且底层的DMA buffer不合其他数据共享cacheline，这样的系统将工作的更好。

## 6. 如何使用coherent DMA mapping的接口？

### 6.1  分配并映射dma buffer

 为了分配并映射一个较大（page大小或者类似）的coherent DMA memory，你需要调用下面的接口：

```cobol
   dma_addr_t dma_handle;
 
    cpu_addr = dma_alloc_coherent(dev, size, &dma_handle, gfp);
```

DMA操作总是会涉及具体设备上的DMA controller，而dev参数就是执行该设备的struct device对象的。size参数指明了你想要分配的DMA Buffer的大小，byte为单位。dma_alloc_coherent这个接口也可以在中断上下文调用，当然，gfp参数要传递GFP_ATOMIC标记，gfp是内存分配的flag，dma_alloc_coherent仅仅是透传该flag到内存管理模块。

需要注意的是dma_alloc_coherent分配的内存的起始地址和size都是对齐在page上（类似__get_free_pages的感觉，当然__get_free_pages接受的size参数是page order），如果你的驱动不需要那么大的DMA buffer，那么可以选择dma_pool接口，下面会进一步描述。

如果传入非空的dev参数，即使驱动调用了掩码设置接口函数设定了DMA mask，说明该设备可以访问大于32-bit地址空间的地址，一致性DMA映射的接口函数也一般会默认的返回一个32-bit可寻址的DMA buffer地址。要知道dma mask和coherent dma mask是不同的，除非驱动显示的调用dma_set_coherent_mask()接口来修改coherent dma mask，例如大小大于32-bit地址，dma_alloc_coherent接口函数才会返回大于32-bit地址空间的地址。dma pool接口也是如此。

dma_alloc_coherent函数返回两个值，一个是从CPU角度访问DMA buffer的虚拟地址，另外一个是从设备（DMA controller）角度看到的bus address：dma_handle，驱动可以将这个bus address传递给HW。

即便是请求的DMA buffer的大小小于PAGE SIZE，dma_alloc_coherent返回的cpu虚拟地址和DMA总线地址都保证对齐在最小的PAGE_SIZE上，这个特性确保了分配的DMA buffer有这样的特性：如果page size是64K，即便是驱动分配一个小于或者等于64K的dma buffer，那么DMA buffer不会越过64K的边界。

### 6.2 umap并释放dma buffer

当驱动需要umap并释放dma buffer的时候，需要调用下面的接口：

```c
dma_free_coherent(dev, size, cpu_addr, dma_handle);
```

 这个接口函数的dev、size参数上面已经描述过了，而cpu_addr和dma_handle这两个参数就是dma_alloc_coherent() 接口的那两个地址返回值。需要强调的一点就是：和dma_alloc_coherent不同，dma_free_coherent不能在中断上下文中调用。（因为在有些平台上，free DMA的操作会引发TLB维护的操作（从而引发cpu core之间的通信），如果关闭了IRQ会锁死在SMP IPI 的代码中）。

### 6.3 dma pool

 如果你的驱动需非常多的小的dma buffer，那么dma pool是最适合你的机制。这个概念类似kmem_cache，__get_free_pages往往获取的是连续的page frame，而kmem_cache是批发了一大批page frame，然后自己“零售”。dma pool就是通过dma_alloc_coherent接口获取大块一致性的DMA内存，然后驱动可以调用dma_pool_alloc从那个大块DMA内存中分一个小块的dma buffer供自己使用。具体接口描述就不说了，大家可以自行阅读。

## 7.DMA操作方向

由于下面的章节会用到DMA操作方向这个概念，因此我们先简单的描述一下，DMA操作方向定义如下：

```c
DMA_BIDIRECTIONAL
DMA_TO_DEVICE
DMA_FROM_DEVICE
DMA_NONE
```

如果你知道的话，你应该尽可能的提供准确的DMA操作方向。

DMA_TO_DEVICE表示“从内存（dma buffer）到设备”，而 DMA_FROM_DEVICE表示“从设备到内存（dma buffer）”，上面的这些字符定义了数据在DMA操作中的移动方向。

虽然我们强烈要求驱动在知道DMA传输方向的适合，精确的指明是DMA_TO_DEVICE或者DMA_FROM_DEVICE，然而，如果你确实是不知道具体的操作方向，那么设定为DMA_BIDIRECTIONAL也是可以的，表示DMA操作可以执行任何一个方向的的数据搬移。你的平台需要保证这一点可以让DMA正常工作，当然，这也有可能会引入一些性能上的额外开销。
DMA_NONE主要是用于调试。在驱动知道精确的DMA方向之前，可以把它保存在DMA控制数据结构中，在dma方向设定有问题的适合，你可以跟踪dma方向的设置情况，以便定位问题所在

除了潜在的平台相关的性能优化之外，精确地指定DMA操作方向还有另外一个优点就是方便调试。有些平台实际上在创建DMA mapping的时候，页表（指将bus地址映射到物理地址的页表）中有一个写权限布尔值，这个值非常类似于用户程序地址空间中的页保护。当DMA控制器硬件检测到违反权限设置时（这时候dma buffer设定的是MA_TO_DEVICE类型，实际上DMA controller只能是读dma buffer），这样的平台可以将错误写入内核日志，从而方便了debug

只有streaming mappings才会指明DMA操作方向，一致性DMA映射隐含的DMA操作方向是DMA_BIDIRECTIONAL。我们举一个streaming mappings的例子：在网卡驱动中，如果要发送数据，那么在map/umap的时候需要指明DMA_TO_DEVICE的操作方向，而在接受数据包的时候，map/umap需要指明DMA操作方向是DMA_FROM_DEVICE

## 8.如何使用streaming DMA mapping的接口？

 streaming DMA mapping的接口函数可以在中断上下文中调用。streaming DMA mapping有两个版本的接口函数，一个是用来map/umap单个的dma buffer，另外一个是用来map/umap形成scatterlist的多个dma buffer。

### 8.1 map/umap单个的dma buffer

map单个的dma buffer的示例如下：

```cobol
struct device *dev = &my_dev->dev;
dma_addr_t dma_handle;
void *addr = buffer->ptr;
size_t size = buffer->len;
 
dma_handle = dma_map_single(dev, addr, size, direction);
if (dma_mapping_error(dev, dma_handle)) {
    goto map_error_handling;
}
```

umap单个的dma buffer可以使用下面的接口：

```c
dma_unmap_single(dev, dma_handle, size, direction);
```

当调用dma_map_single()返回错误的时候，你应当调用dma_mapping_error()来处理错误。虽然并不是所有的DMA mapping实现都支持dma_mapping_error这个接口（调用dma_mapping_error函数实际上会调用底层dma_map_ops操作函数集中的mapping_error成员函数），但是调用它来进行出错处理仍然是一个好的做法。这样做的好处是可以确保DMA mapping代码在所有DMA实现中都能正常工作，而不需要依赖底层实现的细节。没有检查错误就使用返回的地址可能会导致程序失败，可能会产生kernel panic或者悄悄的损坏你有用的数据。下面列举了一些不正确的方法来检查DMA mapping错误，之所以是错误的方法是因为这些代码对底层的DMA实现进行了假设。顺便说的是虽然这里是使用dma_map_single作为示例，实际上也是适用于dma_map_page()的。

错误示例一：

```c
dma_addr_t dma_handle;
 
dma_handle = dma_map_single(dev, addr, size, direction);
if ((dma_handle & 0xffff != 0) || (dma_handle >= 0x1000000)) {
    goto map_error;
}
```

错误示例二：

```c
dma_addr_t dma_handle;
 
dma_handle = dma_map_single(dev, addr, size, direction);
if (dma_handle == DMA_ERROR_CODE) {
    goto map_error;
}
```

当DMA传输完成的时候，程序应该调用dma_unmap_single()函数umap dma buffer。例如：在DMA完成传输后会通过中断通知CPU，而在interrupt handler中可以调用dma_unmap_single()函数。dma_map_single函数在进行DMA mapping的时候使用的是CPU指针（虚拟地址），这样就导致该函数有一个弊端：不能使用HIGHMEM memory进行mapping。鉴于此，map/unmap接口提供了另外一个类似的接口，这个接口不使用CPU指针，而是使用page和page offset来进行DMA mapping：

```c
struct device *dev = &my_dev->dev;
dma_addr_t dma_handle;
struct page *page = buffer->page;
unsigned long offset = buffer->offset;
size_t size = buffer->len;
 
dma_handle = dma_map_page(dev, page, offset, size, direction);
if (dma_mapping_error(dev, dma_handle)) {
    goto map_error_handling;
}
 
...
 
dma_unmap_page(dev, dma_handle, size, direction);
```

在上面的代码中，offset表示一个指定page内的页内偏移（以Byte为单位）。和dma_map_single接口函数一样，调用dma_map_page()返回错误后需要调用dma_mapping_error() 来进行错误处理，上面都已经描述了，这里不再赘述。当DMA传输完成的时候，程序应该调用dma_unmap_page()函数umap dma buffer。例如：在DMA完成传输后会通过中断通知CPU，而在interrupt handler中可以调用dma_unmap_page()函数。

### 8.2 map/umap多个形成scatterlist的dma buffer

在scatterlist的情况下，你要映射的对象是分散的若干段DMA buffer，示例代码如下：

```c
int i, count = dma_map_sg(dev, sglist, nents, direction);
struct scatterlist *sg;
 
for_each_sg(sglist, sg, count, i) {
    hw_address[i] = sg_dma_address(sg);
    hw_len[i] = sg_dma_len(sg);
}
```

上面的代码中nents说明了sglist中条目的数量（即map多少段dma buffer）。

具体DMA映射的实现是自由的，它可以把scatterlist 中的若干段连续的DMA buffer映射成一个大块的，连续的bus address region。例如：如果DMA mapping是以PAGE_SIZE为粒度进行映射，那么那些分散的一块块的dma buffer可以被映射到一个对齐在PAGE_SIZE，然后各个dma buffer依次首尾相接的一个大的总线地址区域上。这样做的好处就是对于那些不支持（或者支持有限）scatter-gather 的DMA controller，仍然可以通过mapping来实现。dma_map_sg调用识别的时候返回0，当调用成功的时候，返回成功mapping的数目。

一旦调用成功，你需要调用for_each_sg来遍历所有成功映射的mappings（这个数目可能会小于nents）并且使用sg_dma_address() 和 sg_dma_len() 这两个宏来得到mapping后的dma地址和长度。

umap多个形成scatterlist的dma buffer是通过下面的接口实现的：

```c
dma_unmap_sg(dev, sglist, nents, direction);
```

 再次强调，调用dma_unmap_sg的时候要确保DMA操作已经完成。另外，传递给dma_unmap_sg的nents参数需要等于传递给dma_map_sg的nents参数，而不是该函数返回的count。

由于DMA地址空间是共享资源，每一次dma_map_{single,sg}() 的调用都需要有其对应的dma_unmap_{single,sg}()，如果你总是分配dma地址资源而不回收，那么系统将会由于DMA address被用尽而陷入不可用的状态。

### 8.3 sync操作

如果你需要多次访问同一个streaming DMA buffer，并且在DMA传输之间读写DMA Buffer上的数据，这时候你需要小心进行DMA buffer的sync操作，以便CPU和设备（DMA controller）可以看到最新的、正确的数据。

首先用dma_map_{single,sg}()进行映射，在完成DMA传输之后，用：

```c
dma_sync_single_for_cpu(dev, dma_handle, size, direction); 
```

或者：

```c
dma_sync_sg_for_cpu(dev, sglist, nents, direction);
```

来完成sync的操作，以便CPU可以看到最新的数据。

如果，CPU操作了DMA buffer的数据，然后你又想把控制权交给设备上的DMA 控制器，让DMA controller访问DMA buffer，这时候，在真正让HW（指DMA控制器）去访问DMA buffer之前，你需要调用：

```c
dma_sync_single_for_device(dev, dma_handle, size, direction);
or
dma_sync_sg_for_device(dev, sglist, nents, direction);
```

以便device（也就是设备上的DMA控制器）可以看到cpu更新后的数据。此外，需要强调的是：传递给dma_sync_sg_for_cpu() 和 dma_sync_sg_for_device()的ents参数需要等于传递给dma_map_sg的nents参数，而不是该函数返回的count。

 在完成最后依次DMA传输之后，你需要调用DMA unmap函数dma_unmap_{single,sg}()。如果在第一次dma_map_*() 调用和dma_unmap_*()之间，你从来都没有碰过DMA buffer中的数据，那么你根本不需要调用dma_sync_*() 这样的sync操作。

下面的例子给出了一个sync操作的示例：

```c
my_card_setup_receive_buffer(struct my_card *cp, char *buffer, int len)
{
    dma_addr_t mapping;
 
    mapping = dma_map_single(cp->dev, buffer, len, DMA_FROM_DEVICE);
    if (dma_mapping_error(cp->dev, mapping)) {
        goto map_error_handling;
    }
 
    cp->rx_buf = buffer;
    cp->rx_len = len;
    cp->rx_dma = mapping;
 
    give_rx_buf_to_card(cp);
}
 
...
 
my_card_interrupt_handler(int irq, void *devid, struct pt_regs *regs)
{
    struct my_card *cp = devid;
 
    ...
    if (read_card_status(cp) == RX_BUF_TRANSFERRED) {
        struct my_card_header *hp;
 
HW已经完成了传输，在cpu访问buffer之前，cpu需要先sync一下，以便看到最新的数据。
        dma_sync_single_for_cpu(&cp->dev, cp->rx_dma,
                    cp->rx_len,
                    DMA_FROM_DEVICE);
 
sync之后就可以安全的读dma buffer了
        hp = (struct my_card_header *) cp->rx_buf;
        if (header_is_ok(hp)) {
            dma_unmap_single(&cp->dev, cp->rx_dma, cp->rx_len,
                     DMA_FROM_DEVICE);
            pass_to_upper_layers(cp->rx_buf);
            make_and_setup_new_rx_buf(cp);
        } else {
            give_rx_buf_to_card(cp);
        }
    }
}
```

当使用了这套DMA mapping接口后，驱动不应该再使用virt_to_bus() 这个接口了，当然bus_to_virt()也不行。不过，如果你的驱动使用了这些接口怎么办呢？其实这套新的DMA mapping接口没有和virt_to_bus、bus_to_virt()一一对应的接口，因此，为了让你的程序能工作，你需要对驱动程序进行小小的修改：你必须要保存从dma_alloc_coherent()、dma_pool_alloc()以及dma_map_single()接口函数返回的dma address（对于dma_map_sg()这个接口，dma地址保存在scatterlist 中，当然这需要硬件支持dynamic DMA mapping ），并把这个dma address保存在驱动的数据结构中，并且同时/或者保存在硬件的寄存器中。

所有的驱动代码都需要迁移到DMA mapping framework的接口函数上来。目前内核已经计划完全 移除virt_to_bus() 和bus_to_virt() 这两个函数，因为它们已经过时了。有些平台由于不能正确的支持virt_to_bus() 和bus_to_virt()，因此根本就没有提供这两个接口。

## 9.错误处理

DMA地址空间在某些CPU架构上是有限的，因此分配并mapping可能会产生错误，我们可以通过下面的方法来判定是否发生了错误：

（1）检查是否dma_alloc_coherent() 返回了NULL或者dma_map_sg 返回0

（2）检查dma_map_single和dma_map_page返回了dma address（通过dma_mapping_error函数）

```c
   dma_addr_t dma_handle;
 
    dma_handle = dma_map_single(dev, addr, size, direction);
    if (dma_mapping_error(dev, dma_handle)) {
        goto map_error_handling;
    }
```

（3）当在mapping多个page的时候，如果中间发生了mapping error，那么需要对那些已经mapped的page进行unmap的操作。下面的示例代码用dma_map_single函数，对于dma_map_page也一样适用。

示例代码一：

```c
dma_addr_t dma_handle1;
dma_addr_t dma_handle2;
 
dma_handle1 = dma_map_single(dev, addr, size, direction);
if (dma_mapping_error(dev, dma_handle1)) {
    goto map_error_handling1;
}
dma_handle2 = dma_map_single(dev, addr, size, direction);
if (dma_mapping_error(dev, dma_handle2)) {
    goto map_error_handling2;
}
 
...
 
map_error_handling2:
    dma_unmap_single(dma_handle1);
map_error_handling1:
```

示例代码二（如果我们在循环中mapping dma buffer，当在中间出错的时候，一样要unmap所有已经映射的dma buffer）：

```c
dma_addr_t dma_addr;
dma_addr_t array[DMA_BUFFERS];
int save_index = 0;
 
for (i = 0; i < DMA_BUFFERS; i++) {
 
    ...
 
    dma_addr = dma_map_single(dev, addr, size, direction);
    if (dma_mapping_error(dev, dma_addr)) {
        goto map_error_handling;
    }
    array[i].dma_addr = dma_addr;
    save_index++;
}
 
...
 
map_error_handling:
 
for (i = 0; i < save_index; i++) {
 
    ...
 
    dma_unmap_single(array[i].dma_addr);
}
```

如果在网卡驱动的tx回调函数（例如ndo_start_xmit）中出现了DMA mapping失败，那么驱动必须调用dev_kfree_skb() 来是否socket buffer并返回NETDEV_TX_OK 。这表示这个socket buffer由于错误而丢弃掉了。

如果在SCSI driver的queue command回调函数中出现了DMA mapping失败，那么驱动必须返回SCSI_MLQUEUE_HOST_BUSY 。这意味着SCSI子系统稍后会再次重传该command给driver。

## 10.优化数据结构

在很多的平台上，dma_unmap_{single,page}()其实什么也没有做，是空函数。因此，跟踪映射的dma address及其长度基本上就是浪费内存空间。为了方便驱动工程师编写代码方便，我们提供了几个实用工具（宏定义），如果没有它们，驱动程序中将充分ifdef或者类似的一些“work around”。下面我们并不是一个个的介绍这些宏定义，而是给出一些示例代码，驱动工程师可以照葫芦画瓢。

1、DEFINE_DMA_UNMAP_{ADDR,LEN}。在DMA buffer数据结构中使用这个宏定义，具体例子如下：

```c
before:
 
    struct ring_state {
        struct sk_buff *skb;
        dma_addr_t mapping;
        __u32 len;
    };
 
   after:
 
    struct ring_state {
        struct sk_buff *skb;
        DEFINE_DMA_UNMAP_ADDR(mapping);
        DEFINE_DMA_UNMAP_LEN(len);
    };
```

 根据CONFIG_NEED_DMA_MAP_STATE的配置不同，DEFINE_DMA_UNMAP_{ADDR,LEN}可能是定义相关的dma address和长度的成员，也可能是空。

2、dma_unmap_{addr,len}_set()。使用该宏定义来赋值，具体例子如下：

```c
before:
 
    ringp->mapping = FOO;
    ringp->len = BAR;
 
   after:
 
    dma_unmap_addr_set(ringp, mapping, FOO);
    dma_unmap_len_set(ringp, len, BAR);
```

3、dma_unmap_{addr,len}()，使用该宏来访问变量。

```c
before:
 
    dma_unmap_single(dev, ringp->mapping, ringp->len,
             DMA_FROM_DEVICE);
 
   after:
 
    dma_unmap_single(dev,
             dma_unmap_addr(ringp, mapping),
             dma_unmap_len(ringp, len),
             DMA_FROM_DEVICE);
```

 上面的这些代码基本是不需要解释你就会明白的了。另外，我们对于dma address和len是分开处理的，因为在有些实现中，unmaping的操作仅仅需要dma address信息就够了。

## 11.平台移植需要注意的问题

如果你仅仅是驱动工程师，并不负责将linux迁移到某个cpu arch上去，那么后面的内容其实你可以忽略掉了。

1、Struct scatterlist的需求

   如果cpu arch支持IOMMU（包括软件模拟的IOMMU），那么你需要打开CONFIG_NEED_SG_DMA_LENGTH 这个内核选项。

2、ARCH_DMA_MINALIGN

CPU体系结构相关的代码必须要要保证kmalloc分配的buffer是DMA-safe的（kmalloc分配的buffer也是有可能用于DMA buffer），驱动和内核子系统的正确运行都是依赖这个条件的。如果一个cpu arch不是全面支持DMA-coherent的（例如硬件并不保证cpu cache中的数据等于main memory中的数据），那么必须定义ARCH_DMA_MINALIGN。而通过这个宏定义，kmalloc分配的buffer可以保证对齐在ARCH_DMA_MINALIGN上，从而保证了kmalloc分配的DMA Buffer不会和其他的buffer共享一个cacheline。想要了解具体的实例可以参考arch/arm/include/asm/cache.h。

 另外，请注意：ARCH_DMA_MINALIGN 是DMA buffer的对齐约束，你不需要担心CPU ARCH的数据对齐约束（例如，有些CPU arch要求有些数据对象需要64-bit对齐）。

备注：本文基本上是内核文档DMA-API-HOWTO.txt的翻译，如果有兴趣可以参考原文。

## 12.参考文献

[1] [扒开DMA映射的内裤 - 云+社区 - 腾讯云](https://cloud.tencent.com/developer/article/1721336)

[2] Documentation\DMA-API-HOWTO.txt

[3] Documentation\DMA-API.txt



# Linux内核4.14版本——DMA Engine框架分析(6)-实战（测试dma驱动）

## 1.dw-axi-dmac驱动
dw-axi-dmac驱动4.14版本没有，是从5.4版本移植的，基本没有修改，这里不做介绍。linux-5.4\drivers\dma\dw-axi-dmac\dw-axi-dmac-platform.c

## 2. dma的测试程序

### 2.1 内核程序

以模块.ko的模式加载如内核

```c
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/dmaengine.h>
#include <linux/uaccess.h>	/* For copy_to_user/put_user/... */
#include <linux/dma-mapping.h>
 
 
#define DRIVER_NAME 		"axidma"
#define AXIDMA_IOC_MAGIC 			'A'
#define AXIDMA_IOCGETCHN			_IO(AXIDMA_IOC_MAGIC, 0)
#define AXIDMA_IOCCFGANDSTART 		_IO(AXIDMA_IOC_MAGIC, 1)
#define AXIDMA_IOCGETSTATUS 		_IO(AXIDMA_IOC_MAGIC, 2)
#define AXIDMA_IOCRELEASECHN 		_IO(AXIDMA_IOC_MAGIC, 3)
 
struct dma_chan *dmaTest_chan = NULL;
static int dmaTest_Flag = -1;
#define DMA_STATUS_UNFINISHED	0
#define DMA_STATUS_FINISHED		1 
static void *src11;
static u64 src_phys;
 
#define BUF_SIZE  (512)
static void *dst11;
static u64 dst_phys;
 
static int axidma_open(struct inode *inode, struct file *file)
{
	printk("Open: do nothing\n");
	
	return 0;
}
 
static int axidma_release(struct inode *inode, struct file *file)
{
	printk("Release: do nothing\n");
	return 0;
}
 
static ssize_t axidma_write(struct file *file, const char __user *data, size_t len, loff_t *ppos)
{
	printk("Write: do nothing\n");
	return 0;
}
 
static void dma_complete_func(void *status)
{
	printk("dma_complete_func dma_complete!\n");
}
 
static long axidma_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct dma_device *dma_dev;
	struct dma_async_tx_descriptor *tx = NULL;
	dma_cap_mask_t mask;
	dma_cookie_t cookie;
	enum dma_ctrl_flags flags;
	unsigned char *su1, *su2;
	unsigned char count;
 
	switch(cmd)
	{
		case AXIDMA_IOCGETCHN:
		{ 
			dma_cap_zero(mask);
			dma_cap_set(DMA_MEMCPY, mask); 
			dmaTest_chan = dma_request_channel(mask, NULL, NULL);
			if(!dmaTest_chan){
				printk("dma request channel failed\n");
				goto error;
			}
 
			printk("dmaTest_chan = %d\n",dmaTest_chan->chan_id);
			break;
		}
		case AXIDMA_IOCCFGANDSTART:
		{			
			dma_dev = dmaTest_chan->device;
			src11 = dma_alloc_wc(dma_dev->dev, BUF_SIZE, &src_phys, GFP_KERNEL);
			if (NULL == src11)
			{
				printk("can't alloc buffer for src\n");
				return -ENOMEM;
			}
			
			dst11 = dma_alloc_wc(dma_dev->dev, BUF_SIZE, &dst_phys, GFP_KERNEL);
			if (NULL == dst11)
			{
				printk("can't alloc buffer for dst\n");
				return -ENOMEM;
			}
			memset(src11, 0xAA, BUF_SIZE);
			memset(dst11, 0x55, BUF_SIZE);
 
			flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
			tx = dmaengine_prep_dma_memcpy(dmaTest_chan, dst_phys, src_phys, BUF_SIZE, flags);
			if(!tx){
				printk("Failed to prepare DMA memcpy\n");
				goto error;
			}
 
			//printk("\r\nsrc_addr = 0x%llx, dst_addr = 0x%llx,  len = %d\r\n", src_phys, dst_phys, BUF_SIZE);
 
			tx->callback = dma_complete_func;
			dmaTest_Flag = DMA_STATUS_UNFINISHED;
			tx->callback_param = &dmaTest_Flag;
			cookie =  dmaengine_submit(tx);
			if(dma_submit_error(cookie)){
				printk("Failed to dma tx_submit\n");
				goto error;
			}
			dma_async_issue_pending(dmaTest_chan);
			break;
		}
		case AXIDMA_IOCGETSTATUS:
		{
			if (memcmp(src11, dst11, BUF_SIZE) == 0)
			{
				printk("COPY OK\n");
			}
			else
			{
				printk("COPY ERROR\n");
			}
			
			for (su1 = src11, su2 = dst11, count = 0; count < 16; ++su1, ++su2, count++)
			{
				//printk("%x %x\n", *su1, *su2);
			}
			
			break;
		}
		case AXIDMA_IOCRELEASECHN:
		{
			dma_release_channel(dmaTest_chan);
			dmaTest_Flag = DMA_STATUS_UNFINISHED;
		}
		break;
		default:
			printk("Don't support cmd [%d]\n", cmd);
			break;
	}
	return 0;
error:
	return -EFAULT;
}
 
/*
 *    Kernel Interfaces
 */
 
static struct file_operations axidma_fops = {
    .owner        = THIS_MODULE,
    .llseek        = no_llseek,
    .write        = axidma_write,
    .unlocked_ioctl = axidma_unlocked_ioctl,
    .open        = axidma_open,
    .release    = axidma_release,
};
 
static struct miscdevice axidma_miscdev = {
    .minor        = MISC_DYNAMIC_MINOR,
    .name        = DRIVER_NAME,
    .fops        = &axidma_fops,
};
 
static int __init axidma_init(void)
{
    int ret = 0;
 
    ret = misc_register(&axidma_miscdev);
    if(ret) {
        printk (KERN_ERR "cannot register miscdev (err=%d)\n", ret);
		return ret;
    }
 
	return 0;
}
 
static void __exit axidma_exit(void)
{    
    misc_deregister(&axidma_miscdev);
}
 
module_init(axidma_init);
module_exit(axidma_exit);
 
MODULE_AUTHOR("ygy");
MODULE_DESCRIPTION("Axi Dmac Driver");
MODULE_LICENSE("GPL");
```

### 2.2 用户测试程序

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>
 
#define DRIVER_NAME 		"/dev/axidma"
 
#define AXIDMA_IOC_MAGIC 			'A'
#define AXIDMA_IOCGETCHN			_IO(AXIDMA_IOC_MAGIC, 0)
#define AXIDMA_IOCCFGANDSTART 		_IO(AXIDMA_IOC_MAGIC, 1)
#define AXIDMA_IOCGETSTATUS 		_IO(AXIDMA_IOC_MAGIC, 2)
#define AXIDMA_IOCRELEASECHN 		_IO(AXIDMA_IOC_MAGIC, 3)
 
int main(void)
{
	int fd = -1;
	int ret;
	
	/* open dev */
	fd = open(DRIVER_NAME, O_RDWR);
	if(fd < 0){
		printf("open %s failed\n", DRIVER_NAME);
		return -1;
	}
	
	/* get channel */
	ret = ioctl(fd, AXIDMA_IOCGETCHN, NULL);
	if(ret){
		printf("ioctl: get channel failed\n");
		goto error;
	}
 
	ret = ioctl(fd, AXIDMA_IOCCFGANDSTART, NULL);
	if(ret){
		printf("ioctl: config and start dma failed\n");
		goto error;
	}
 
	/* wait finish */
	while(1){
		ret = ioctl(fd, AXIDMA_IOCGETSTATUS, NULL);
		if(ret){
			printf("ioctl: AXIDMA_IOCGETSTATUS dma failed\n");
			goto error;
		}
		sleep(60);
	}
 
	/* release channel */
	ret = ioctl(fd, AXIDMA_IOCRELEASECHN, NULL);
	if(ret){
		printf("ioctl: release channel failed\n");
		goto error;
	}
 
	close(fd);
 
	return 0;
error:
	close(fd);
	return -1;
}
```

# Linux内核4.14版本——DMA Engine框架分析(7)-scatterlist API介绍

## 1.前言

我们在那些需要和用户空间交互大量数据的子系统（例如MMC、Video、Audio等）中，经常看到scatterlist的影子。scatterlist即物理内存的散列表。再通俗一些，就是把一些分散的物理内存，以列表的形式组织起来。那么，也许你会问，有什么用处呢？

## 2. scatterlist产生的背景

我没有去考究scatterlist API是在哪个kernel版本中引入的（年代太久远了），凭猜测，我觉得应该和MMU有关。因为在引入MMU之后，linux系统中的软件将不得不面对一个困扰（下文将以图片1中所示的系统架构为例进行说明）：

> 假设在一个系统中（参考下面图片1）有三个模块可以访问memory：CPU、DMA控制器和某个外设。CPU通过MMU以虚拟地址（VA）的形式访问memory；DMA直接以物理地址（PA）的形式访问memory；Device通过自己的IOMMU以设备地址（DA）的形式访问memory。
>
>    然后，某个“软件实体”分配并使用了一片存储空间（参考下面图片2）。该存储空间在CPU视角上（虚拟空间）是连续的，起始地址是va1（实际上，它映射到了3块不连续的物理内存上，我们以pa1,pa2,pa3表示）。
>
>    那么，如果该软件单纯的以CPU视角访问这块空间（操作va1），则完全没有问题，因为MMU实现了连续VA到非连续PA的映射。
>
>    不过，如果软件经过一系列操作后，要把该存储空间交给DMA控制器，最终由DMA控制器将其中的数据搬移给某个外设的时候，由于DMA控制器只能访问物理地址，只能以“不连续的物理内存块”为单位递交（而不是我们所熟悉的虚拟地址）。
>
>    此时，scatterlist就诞生了：为了方便，我们需要使用一个数据结构来描述这一个个“不连续的物理内存块”（起始地址、长度等信息），这个数据结构就是scatterlist（具体可参考下面第3章的说明）。而多个scatterlist组合在一起形成一个表（可以是一个[struct](https://so.csdn.net/so/search?q=struct&spm=1001.2101.3001.7020) scatterlist类型的数组，也可以是kernel帮忙抽象出来的struct sg_table），就可以完整的描述这个虚拟地址了。
>
>    最后，从本质上说：scatterlist（数组）是各种不同地址映射空间（PA、VA、DA、等等）的媒介（因为物理地址是真实的、实在的存在，因而可以作为通用语言），借助它，这些映射空间才能相互转换（例如从VA转到DA）。

![img](DMA Engine框架分析.assets/8bc2158d698719c7ee486325fd865285.gif)

![img](DMA Engine框架分析.assets/1360089ba5b90f7436026789831e0352.gif)



## 3. scatterlist API介绍

### 3.1 struct scatterlist

struct scatterlist用于描述一个在物理地址上连续的内存块（以page为单位），它的定义位于“include/linux/scatterlist.h”中，如下：

```c
struct scatterlist {
        unsigned long   page_link;
        unsigned int    offset;
        unsigned int    length;
        dma_addr_t      dma_address;
#ifdef CONFIG_NEED_SG_DMA_LENGTH
        unsigned int    dma_length;
#endif
};
```

> page_link，指示该内存块所在的页面。bit0和bit1有特殊用途，因此要求page最低4字节对齐。
> offset，指示该内存块在页面中的偏移（起始位置）。
> length，该内存块的长度。
> dma_address，该内存块实际的起始地址（PA，相比page更接近我们人类的语言）。
> dma_length，相应的长度信息。

### 3.2 struct sg_table

在实际的应用场景中，单个的scatterlist是没有多少意义的，我们需要多个scatterlist组成一个数组，以表示在物理上不连续的虚拟地址空间。通常情况下，使用scatterlist功能的模块，会自行维护这个数组（指针和长度），例如struct mmc_data：

```c
struct mmc_data { 
    … 
        unsigned int            sg_len;         /* size of scatter list */      
        struct scatterlist      *sg;            /* I/O scatter list */          
        s32                     host_cookie;    /* host private data */         
};
```

 另外kernel抽象出来了一个简单的数据结构：struct sg_table，帮忙保存scatterlist的数组指针和长度：

```c
struct sg_table {
        struct scatterlist *sgl;        /* the list */
        unsigned int nents;             /* number of mapped entries */
        unsigned int orig_nents;        /* original size of list */
};
```

其中sgl是内存块数组的首地址，orig_nents是内存块数组的size，nents是有效的内存块个数（可能会小于orig_nents）。
   scatterlist数组中到底有多少有效内存块呢？这不是一个很直观的事情，主要有如下2个规则决定：

> 1）如果scatterlist数组中某个scatterlist的page_link的bit0为1，表示该scatterlist不是一个有效的内存块，而是一个chain（铰链），指向另一个scatterlist数组。通过这种机制，可以将不同的scatterlist数组链在一起，因为scatterlist也称作chain scatterlist。
>
> 2）如果scatterlist数组中某个scatterlist的page_link的bit1为1，表示该scatterlist是scatterlist数组中最后一个有效内存块（后面的就忽略不计了）。

### 3.3 API介绍

理解了scatterlist的含义之后，再去看“include/linux/scatterlist.h”中的API，就容易多了，例如（简单介绍一下，不再详细分析）：

```c
#define sg_dma_address(sg) ((sg)->dma_address)
 
#ifdef CONFIG_NEED_SG_DMA_LENGTH
#define sg_dma_len(sg) ((sg)->dma_length)
#else
#define sg_dma_len(sg) ((sg)->length)
#endif
```

> sg_dma_address、sg_dma_len，获取某一个scatterlist的物理地址和长度。

```c
#define sg_is_chain(sg) ((sg)->page_link & 0x01)
#define sg_is_last(sg) ((sg)->page_link & 0x02)
#define sg_chain_ptr(sg) \
((struct scatterlist *) ((sg)->page_link & ~0x03))
```

> sg_is_chain可用来判断某个scatterlist是否为一个chain，sg_is_last可用来判断某个scatterlist是否是sg_table中最后一个scatterlist。
>
>    sg_chain_ptr可获取chain scatterlist指向的那个scatterlist。

```c
static inline void sg_assign_page(struct scatterlist *sg, struct page *page)
static inline void sg_set_page(struct scatterlist *sg, struct page *page,
unsigned int len, unsigned int offset)
static inline struct page *sg_page(struct scatterlist *sg)
static inline void sg_set_buf(struct scatterlist *sg, const void *buf,
unsigned int buflen)
 
#define for_each_sg(sglist, sg, nr, __i) \
for (__i = 0, sg = (sglist); __i < (nr); __i++, sg = sg_next(sg))
 
static inline void sg_chain(struct scatterlist *prv, unsigned int prv_nents,
struct scatterlist *sgl)
 
static inline void sg_mark_end(struct scatterlist *sg)
static inline void sg_unmark_end(struct scatterlist *sg)
 
static inline dma_addr_t sg_phys(struct scatterlist *sg)
static inline void *sg_virt(struct scatterlist *sg)
```

> sg_assign_page，将page赋给指定的scatterlist（设置page_link字段）。
> sg_set_page，将page中指定offset、指定长度的内存赋给指定的scatterlist（设置page_link、offset、len字段）。
> sg_page，获取scatterlist所对应的page指针。
> sg_set_buf，将指定长度的buffer赋给scatterlist（从虚拟地址中获得page指针、在page中的offset之后，再调用sg_set_page）。
>
> for_each_sg，遍历一个scatterlist数组（sglist）中所有的有效scatterlist（考虑sg_is_chain和sg_is_last的情况）。
>
> sg_chain，将两个scatterlist 数组捆绑在一起。
>
> sg_mark_end、sg_unmark_end，将某个scatterlist 标记（或者不标记）为the last one。
>
> sg_phys、sg_virt，获取某个scatterlist的物理或者虚拟地址。













