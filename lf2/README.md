# lf2

#### 项目介绍
	程序在论坛众亲友的给力指导下, 在公司内核部门的指导之下, 长连接性能最终达到了50万qps, 短连接性能最终达到了7万qps, 测试机器CPU为12核心, 64MB内存, 测试环境为单机lo网口.

   程序基于epoll的EPOLLONESHOT选项, 充分利用了epoll的线程安全特性, 通过独立的监听线程最大化连接建立速率, 通过线程池配合epoll简易的实现了Leader-Follower的程序结构, 对于各类业务逻辑能够普遍适用.

   程序优化过程中, 主要是2个瓶颈点的化解:

   1, epoll线程安全, 所以内部的锁会造成多线程共享epoll fd的瓶颈（内部的锁会造成多线程共享epoll fd引起线程进D状态）, 通过创建多组epoll fd, 减小锁的竞争可以化解瓶颈, 充分利用硬件性能.
    
   2, 短连接建立能力差, 未经过优化网络参数, 只能达到3万/秒的建立能力, 经过参数优化, 可以达到8万/秒的建立能力, 具体参数参考代码里的Readme.

   解决了上述两个问题后的代码, 能够支撑50万qps

  特点：1.创建多组epoll fd，减小锁的竞争，可以同时在不同组的fepoll fd中取出并处理事件
		2.一次只能处理一个事件
		3.监听事件与处理事件在不同一个线程内，有一个专门的监听连接线程，
		  把已连接的事件放入不同组的fepoll fd中监听

#短连接优化方法(实测可达到7-8万/秒的accept速率):

/proc/sys/net/core/somaxconn = 4096

/proc/sys/net/ipv4/之下的:
    tcp_max_syn_backlog = 8192
    tcp_syn_retries= 5
    tcp_synack_retries = 5
    tcp_abort_on_overflow=0
    tcp_tw_reuse=1
    tcp_tw_recycle=1
    tcp_timestamps=1
    tcp_syncookies=1


是memcached的架构, 我今天也思考过为什么要在一个epoll fd下开N个线程, 而不是一个epoll fd一个线程, 最终的结论是:一个epoll fd一个线程的伸缩性差, 因为一个epoll fd下的所有socket是串行的, 有一个慢就没得选择只能都等着.

而leader-follower的多个线程共享一个epoll fd, 优点就是一个fd卡了, 其他fd可以使用其他线程工作.

如果正常来说, 我认为一个线程一个epoll和多个线程一个epoll的性能是一样的.



epoll 1线程(listen+accept) + n线程(epoll_wait+处理) 模型 （200万QPS实现echo server方案）
1、对应开源产品：无
2、基本原理：
1）1个线程监听端口并accept新fd，把fd的监听事件round robin地注册到n个worker线程的epoll句柄上
2）如果worker线程是多个线程共享一个epoll句柄，那么事件需要设置EPOLLONESHOT，避免在读写fd的时候，事件在其他线程被触发
3）worker线程epoll_wait获得读写事件并处理之
3、echo server测试：200万QPS（因为资源有限，测试client和server放在同一个物理机上，实际能达到的上限应该还会更多）
4、优点：
1）减少竞争。在第四和第五种模型中，worker需要去争着完成accept，这里有竞争。而在这种模型中，就消除了这种竞争
5、缺点：
1）负载均衡。这种模型的连接分配，又回到了由master分配的模式，依然会存在负载不均衡的问题。可以让若干个线程共享一个epoll句柄，从而把任务分配均衡到多个线程，实现全局更好的负载均衡效果


