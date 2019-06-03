# leader-follower-server

#### 项目介绍
一个领导者-追随者模型的echo server， 使用epoll + 线程池， 多个线程同时监听同一个epoll的fd，epoll_wait同一个事件集，一个线程只epoll_wait（leader）一个事件, 接着处理（folower）这个事件, 因为使用epoll的EPOLLONESHOT，一个事件fd只能被一个线程使用，这样避免了一个事件fd同时被多个线程使用的情况.

优化：
多个线程同时监听同一个epoll的fd，这样会造成peoll内部锁的竞争， 为了减少同一个epfd内部锁的竞争，将epfd进行分组，线程池分组监听分组的epfd, 同时另外用一个线程专门负责accept, 工作线程只负责数据的处理。
