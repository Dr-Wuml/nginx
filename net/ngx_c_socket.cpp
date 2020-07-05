#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
//#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"
#include "ngx_global.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

//构造函数
CSocket::CSocket()
{
	//配置信息
	
    m_ListenPortCount = 1;          //默认监听一个端口
    m_worker_connections = 1;       //epoll最大连接数
    m_RecyConnectionWaitTime = 60;  //延时回收时长
    
    //epoll相关
    m_epollhandle = -1 ;            //epoll返回的句柄
    //m_pconnections = NULL;          //连接池
    //m_pfree_connections = NULL;     //连接池中的空闲连接
    //m_pread_events = NULL;          //读事件数组
    //m_pwrite_events = NULL;         //写事件数组
    
    //一些和网络通讯有关的常用变量值
    m_iLenPkgHeader = sizeof(COMM_PKG_HEADER);         //包头长度
    m_iLenMsgHeader = sizeof(STRUC_MSG_HEADER);        //消息头长度
    
    //m_iRecvMsgQueueCount = 0;    //收消息队列
    //pthread_mutex_init(&m_recvMessageQueueMutex, NULL); //互斥量初始化
    
    //队列相关
    m_iSendMsgQueueCount     = 0;      //发消息队列大小
    m_total_recyconnection_n = 0;      //待释放连接队列大小
    m_cur_size_              = 0;      //当前计时队列大小
    m_timer_value_           = 0;      //当前计时队列头部时间值
    m_iDiscardSendPkgCount    = 0;      //待发送数据包需要丢弃的数量
    
    //时间相关
    m_onlineUserCount        = 0;      //用户在线人数
    m_lastprintTime          = 0;      //上次统计信息打印时间
    return ;
}

//析构函数
CSocket::~CSocket()
{
	//监听端口内存释放
    std::vector<lpngx_listening_t>::iterator pos;
    for(pos = m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); pos++)
    {
        delete (*pos);
    }
    m_ListenSocketList.clear();
    return;
}

//接收消息队列释放函数
/*void CSocket::clearMsgRecvQueue()
{
	char *sTmpMempoint;
	CMemory *pMemory = CMemory::GetInstance();
	while(!m_MsgRecvQueue.empty())
	{
		sTmpMempoint = m_MsgRecvQueue.front();
		m_MsgRecvQueue.pop_front();
		pMemory->FreeMemory(sTmpMempoint);
	}
	return;
}*/

//监听端口配置
void CSocket::ReadConf()
{
    CConfig *pConfig         = CConfig::GetInstance();
    /*取得需要监听的端口个数*/
    m_ListenPortCount        = pConfig->GetIntDefault("ListenPortCount", m_ListenPortCount);
    m_worker_connections     = pConfig->GetIntDefault("worker_connections",m_worker_connections);
    m_RecyConnectionWaitTime = pConfig->GetIntDefault("Sock_RecyConnectionWaitTime",m_RecyConnectionWaitTime);
    
    m_ifkickTimeCount        = pConfig->GetIntDefault("Sock_WaitTimeEnable",0);
    m_iWaitTime              = pConfig->GetIntDefault("Sock_MaxWaitTime",m_iWaitTime);
    m_iWaitTime              = (m_iWaitTime > 5)?m_iWaitTime:5;                          //最少5s
    m_ifTimeOutKick          = pConfig->GetIntDefault("Sock_TimeOutKick",0);
    
    m_floodAkEnable          = pConfig->GetIntDefault("Sock_FloodAttackKickEnable",0);
    m_floodTimeInterval      = pConfig->GetIntDefault("Sock_FloodTimeInterval",100);
    m_floodKickCount         = pConfig->GetIntDefault("Sock_FloodKickCounter",10);
    
    return;
}


//初始化函数，成功返回true，失败返回false
bool CSocket::Initialize()
{
	ReadConf();
    if(ngx_open_listening_sockets() == false)
    {
    	return false;	
    }
    return true;
}

bool CSocket::Initialize_subproc()
{
	//发消息互斥量
	if(pthread_mutex_init(&m_sendMessageQueueMutex, NULL) != 0)
	{
		ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_mutex_init(&m_sendMessageQueueMutex)失败.");
        return false; 
	}
	
	//连接相关互斥量初始化
	if(pthread_mutex_init(&m_connectionMutex, NULL) != 0)
	{
		ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_mutex_init(&m_connectionMutex)失败.");
        return false;    
	}
	
	//回收消息互斥量
	if(pthread_mutex_init(&m_recyconnqueueMutex, NULL)  != 0)
    {
        ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_mutex_init(&m_recyconnqueueMutex)失败.");
        return false;    
    } 
    
    //时间相关互斥量初始化
    if(pthread_mutex_init(&m_timequeueMutex,NULL) != 0)
    {
    	ngx_log_stderr(0,"CSocekt::Initialize_subproc()中pthread_mutex_init(&m_timequeueMutex)失败.");
        return false;  
    }
    
    //初始化发消息相关信号量
    if(sem_init(&m_semEventSendQueue,0,0) == -1)
    {
        ngx_log_stderr(0,"CSocekt::Initialize_subproc()中sem_init(&m_semEventSendQueue,0,0)失败.");
        return false;
    }
    
    //创建线程
    int err;
    ThreadItem *pSendQueue;                      //用来发送数据的线程
    m_threadVector.push_back(pSendQueue = new ThreadItem(this));
    err = pthread_create(&pSendQueue->_Handle, NULL, ServerSendQueueThread,pSendQueue);
    if(err != 0)
    {
    	ngx_log_stderr(0,"CSocekt::Initialize_subproc()pthread_create(&pSendQueue)失败.");
        return false;
    }
    
    ThreadItem *pRecyconn;
    m_threadVector.push_back(pRecyconn = new ThreadItem(this)); 
    err = pthread_create(&pRecyconn->_Handle, NULL, ServerRecyConnectionThread,pRecyconn);
    if(err != 0)
    {
    	ngx_log_stderr(0,"CSocekt::Initialize_subproc()pthread_create(&pRecyconn)失败.");
        return false;
    }
    
    if(m_ifkickTimeCount == 1)
    {
    	ThreadItem *pTimemonitor;
    	m_threadVector.push_back(pTimemonitor = new ThreadItem(this));
    	err = pthread_create(&pTimemonitor->_Handle,NULL,ServerTimerQueueMonitorThread,pTimemonitor);
    	if(err != 0)
    	{
    		ngx_log_stderr(0,"CSocekt::Initialize_subproc()pthread_create(&pTimemonitor)失败.");
    		return false;
    	}
    }
    
	return true;
}

//关闭退出函数[子进程中执行]
void CSocket::Shutdown_subproc()
{
	//用到信号量，还需要调用一下sem_post
	if(sem_post(&m_semEventSendQueue) == -1)
	{
		ngx_log_stderr(0,"CSocekt::Shutdown_subproc()中sem_post(&m_semEventSendQueue)失败.");
	}
	
	//等待所有的线程结束
	std::vector<ThreadItem*>::iterator iter;
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
	{
		pthread_join((*iter)->_Handle, NULL);        //等待一个线程终止
	}
	
	//释放new出来的线程池
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
	{
		if(*iter)
		{
			delete (*iter);
		}
	}	
	m_threadVector.clear();
	
	//队列相关
	clearMsgSendQueue();
	clearconnection();
	clearAllFromTimerQueue();
	
	//多线程
	pthread_mutex_destroy(&m_connectionMutex);         //TCP连接队列互斥量
	pthread_mutex_destroy(&m_sendMessageQueueMutex);   //消息发送队列互斥量
	pthread_mutex_destroy(&m_recyconnqueueMutex);      //待回收连接队列互斥量
	pthread_mutex_destroy(&m_timequeueMutex);
	sem_destroy(&m_semEventSendQueue);                 //信号互斥
	return;
}

//清理TCP发送消息队列
void CSocket::clearMsgSendQueue()
{
	char *sTmpMemPoint;
	CMemory *pMemory = CMemory::GetInstance();
	while(!m_MsgSendQueue.empty())
	{
		sTmpMemPoint = m_MsgSendQueue.front();
		m_MsgSendQueue.pop_front();
		pMemory->FreeMemory(sTmpMemPoint);
	}
}


//实现监听端口的函数，需放到worker子进程之前
bool CSocket::ngx_open_listening_sockets()
{
    int                isock;       //socket
    struct sockaddr_in serv_addr;   //服务器地址结构，系统定义
    int                iport;        //端口号
    char               strinfo[100];//临时字符串

    //初始化相关变量
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;                 //选择协议族为IPV4
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);  //监听本地所有IP地址
    
    CConfig *pConfig = CConfig::GetInstance();
    for(int i=0; i < m_ListenPortCount; i++) //监听端口 
    {
        //参数1：协议族选择 参数2：SOCK_STREAM:使用TCP 参数3：固定用法 0   
        isock = socket(AF_INET, SOCK_STREAM, 0);
        if(isock == -1)
        {
            ngx_log_stderr(errno, "CSocket::Initialize()中socket()失败，i=%d.", i);
            return false;
        }
        //setsockopt():设置一些套接字选项；
        //参数2：表示级别，和参数3配套使用，也就是说，参数3确定参数2就确定了
        //参数3：允许重用本地地址
        //设置 SO_REUSEADDR，解决TIME_WAIT导致bind()失败的问题
        int reuseaddr = 1;  //1：打开对应的设置项
        if(setsockopt(isock, SOL_SOCKET, SO_REUSEADDR,(const void *) &reuseaddr, sizeof(reuseaddr)) == -1)
        {
            ngx_log_stderr(errno, "CSocket::Initialize()中setsockopt(SO_REUSEADDR)失败，i=%d.", i);
            close(isock);
            return false;
        }

		int reuseport = 1;
		if(setsockopt(isock, SOL_SOCKET, SO_REUSEPORT,(const void *) &reuseport, sizeof(int)) == -1)
		{
		 	 ngx_log_stderr(errno,"CSocekt::Initialize()中setsockopt(SO_REUSEPORT)失败",i);
		}

        //设置socket为非阻塞
        if(setnonblocking(isock) == false)
        {
            ngx_log_stderr(errno, "CSocket::Initialize()中setnonblocking()失败，i=%d.", i);
            close(isock);
            return false;
        }

        //设置本服务器要监听的地址和端口号，这样客户端才能够连接到该地址和端口并发送数据
        strinfo[0] = 0;
        sprintf(strinfo,"ListenPort%d",i);
        iport = pConfig -> GetIntDefault(strinfo,10000);
        serv_addr.sin_port = htons((in_port_t)iport);  //in_port_t  类于 uint16_t
        
        //绑定服务器地址结构体
        if(bind(isock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        {
            ngx_log_stderr(0, "CSocket::Initialize()中bind()失败，i=%d.", i);
            close(isock);
            return false;
        }

        //开始监听
        if(listen(isock, NGX_LISTEN_BACKLOG) == -1)
        {
            ngx_log_stderr(0, "CSocket::Initialize()中listen()失败，i=%d.",i);
            close(isock);
            return false;
        }
        lpngx_listening_t p_listensocketitem = new ngx_listening_t;
        memset(p_listensocketitem, 0, sizeof(ngx_listening_t));
        p_listensocketitem->port = iport;
        p_listensocketitem->fd   = isock;
        ngx_log_error_core(NGX_LOG_INFO,0,"监听%d端口成功!",iport);
        m_ListenSocketList.push_back(p_listensocketitem); 
    } //end for(int i=0; i < m_ListenPortCount; i++)
    
    if(m_ListenSocketList.size() <= 0)
    {
    	 ngx_log_error_core(NGX_LOG_EMERG,0,"无监听端口");
    	return false;
    }
    return true;
}

//设置连接为非阻塞模式
bool CSocket::setnonblocking(int sockfd)
{
    int nb=1;                               //0：清楚，1：设置
    if(ioctl(sockfd, FIONBIO, &nb) == -1)   //FIONBBIO 设置/清除非阻塞I/O标记：   0：清除，1：设置
    {
        return false;
    }
    return true;
    /*  下面写法同上面效果一样
    int opts = fcntl(sockfd, F_GETFL);      //用F_GETFL先获取描述符的一些标志信息
    if(opts < 0)
    {
        ngx_log_stderr(errno, "CSocket::setnonblocking()中的fcntl(F_GETFL)失败.");
        return false;
    }
    opts |= O_NONBLOCK;                     //把阻塞标记加到原来的标记上，标记这个是非阻塞套接字
    //如何关闭非阻塞【?opts &= ~O_NONBLOCK】
    if(fcntl(sockfd, F_SETFL, opts) < 0)
    {
        ngx_log_stderr(errno, "CSocket::setnonblocking()中的fcntl(F_SETFL)失败.");
        return false;
    }
    return true;
    */
}

//关闭监听端口
void CSocket::ngx_close_listening_sockets()
{
    for(int i=0; i<m_ListenPortCount; i++)
    {
        close(m_ListenSocketList[i]->fd);
        ngx_log_error_core(NGX_LOG_INFO,0,"关闭监听端口%d!",m_ListenSocketList[i]->port);
    }
    return;
}

//待发送消息入队列
void CSocket::msgSend(char *psendbuf)
{
	CMemory *pMemory = CMemory::GetInstance();
	CLock lock(&m_sendMessageQueueMutex);
	if(m_iSendMsgQueueCount > 50000)
	{
		m_iDiscardSendPkgCount++;
		pMemory->FreeMemory(psendbuf);
		return;
	}
	
	LPSTRUC_MSG_HEADER pMsgHeader = (LPSTRUC_MSG_HEADER)psendbuf;
	lpngx_connection_t pConn = pMsgHeader->pConn;
	if(pConn->iSendCount > 400)
	{
		ngx_log_stderr(0,"CSocekt::msgSend()中发现某用户%d积压了大量待发送数据包，切断与他的连接！",pConn->fd);
		m_iDiscardSendPkgCount++;
		pMemory->FreeMemory(psendbuf);
		zdClosesocketProc(pConn);
		return;
	}
	
	++pConn->iSendCount;
	m_MsgSendQueue.push_back(psendbuf);
	++m_iSendMsgQueueCount;
	if(sem_post(&m_semEventSendQueue) == 1)          //让ServerSendQueueThread()流程走下来干活
	{
		ngx_log_stderr(0,"CSocekt::msgSend()sem_post(&m_semEventSendQueue)失败.");    
	}
	return;
}

//主动关闭连接
void CSocket::zdClosesocketProc(lpngx_connection_t pConn)
{
	if(m_ifkickTimeCount == 1)
	{
		DeleteFromTimerQueue(pConn);
	}
	if(pConn->fd != -1)
	{
		close(pConn->fd);
		pConn->fd = -1;
	}
	
	if(pConn->iThrowsendCount > 0)
	{
		--pConn->iThrowsendCount;
	}
	inRecyConnectQueue(pConn);
	return;
}

//测试是否flood攻击成立，成功返回true，否返回false
bool CSocket::TestFlood(lpngx_connection_t pConn)
{
	struct timeval sCurrTime;        //当前时间结构
	uint64_t       iCurrTime;        //当前时间
	bool           reco = false; 
	
	gettimeofday(&sCurrTime, NULL);  //获取当前时间
	iCurrTime = (sCurrTime.tv_sec *1000 + sCurrTime.tv_usec / 1000);  //毫秒
	if((iCurrTime - pConn->FloodkickLastTime) < m_floodTimeInterval)  //两次收到包的时间 < 100ms
	{
		//发包频繁记录
		pConn->FloodAttackCount++;
		pConn->FloodkickLastTime = iCurrTime;
	}
	else
	{
		//正常频率发包,记录恢复正常
		pConn->FloodAttackCount = 0;
		pConn->FloodkickLastTime = iCurrTime;
	}
	
	//测试使用日志
	//ngx_log_stderr(0,"pConn->FloodAttackCount=%d,m_floodKickCount=%d.",pConn->FloodAttackCount,m_floodKickCount);
	
	if(pConn->FloodAttackCount >= m_floodKickCount)
	{
		reco = true;                    //设置踢人标志
	}
	return reco;
}

void CSocket::printTDInfo()
{
	time_t currtime = time(NULL);
	if((currtime - m_lastprintTime) > 10)
	{
		//每隔十秒打印一次
		int tmprmqc = g_threadpool.getRecvMsgQueueCount();                  //收消息队列
		m_lastprintTime = currtime;
		int tmpoLUC = m_onlineUserCount;
		int tmpsmqc = m_iSendMsgQueueCount;
		ngx_log_stderr(0,"--------------------------------------begin-------------------------------------------");
		ngx_log_stderr(0,"当前在线人数/总人数(%d/%d)。",tmpoLUC,m_worker_connections);
		ngx_log_stderr(0,"连接池中空闲连接/总连接/要释放的连接(%d/%d/%d)。",m_freeconnectionList.size(),m_connectionList.size(),m_recyconnectionList.size());
		ngx_log_stderr(0,"当前时间队列大小(%d)。",m_timerQueuemap.size());
		ngx_log_stderr(0,"当前收消息队列/发消息队列大小(%d/%d),丢弃的待发送数据包的大小(%d)。",tmprmqc,tmpsmqc,m_iDiscardSendPkgCount);
		if(tmprmqc > 100000)
		{
			//接收队列过大，报一下，这个属于应该 引起警觉的，考虑限速等等手段
            ngx_log_stderr(0,"接收队列条目数量过大(%d)，要考虑限速或者增加处理线程数量了！！！！！！",tmprmqc);
		}
		ngx_log_stderr(0,"---------------------------------------end--------------------------------------------");
	}
	return;
}

//初始化epoll功能，在子进程调用
int CSocket::ngx_epoll_init()
{
	//创建一个epoll对象，创建了一个红黑树，还创建了一个双向链表
	m_epollhandle = epoll_create(m_worker_connections);
	if(m_epollhandle == -1)
	{
		ngx_log_stderr(errno,"CSocket::ngx_epoll_init()中epoll_create()失败.");
		exit(2);
	}
	
	//(2)创建连接池【数组】、创建出来，这个东西后续用于处理所有客户端的连接
	initconnection();
	
	
	std::vector<lpngx_listening_t>::iterator pos;
	for(pos=m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); ++pos)
	{
		lpngx_connection_t pConn = ngx_get_connection((*pos)->fd);               //获取空闲连接
		if(pConn == NULL)
		{
			ngx_log_stderr(errno,"CSocket::ngx_epoll_init()中ngx_get_connection失败.");
			exit(2);
		}
		pConn->listening = (*pos);                             //连接对象和监听对象关联，方便寻找监听对象
		(*pos)->connection = pConn;                            //监听对象和连接对象关联，方便寻找连接对象
		//recv->accept = 1;                                  //监听端口必须设置accept标志为1
		pConn->rhandler = &CSocket::ngx_event_accept;        //对监听端口的读事件设置处理方法，监听端口关心的就是读事件
		
		//参数1：socket句柄，参数2：读事件，参数3：写事件，参数4：其他补充标记，参数5：事件类型，参数6：连接池中的连接
		/*if(ngx_epoll_add_event((*pos)->fd,EPOLL_CLT_ADD,EPOLLIN|EPOLLRDHUP,0,pConn) == -1)
		{
			exit(2);
		}*/
		if(ngx_epoll_oper_event((*pos)->fd,EPOLL_CTL_ADD,EPOLLIN|EPOLLRDHUP,0,pConn) == -1)
		{
			exit(2);
		}
	}
	return 1;
}

/*****************************************************************************************************************
   参数说明：
   fd,               //句柄，一个socket
   eventtype,        //事件类型，一般是EPOLL_CTL_ADD，EPOLL_CTL_MOD，EPOLL_CTL_DEL ，用于操作epoll红黑树的节点(增加，修改，删除)
   flag,             //标志，具体含义取决于eventtype
   bcaction,         //补充动作，用于补充flag标记的不足  :  0：增加   1：去掉
   pConn             //pConn：一个指针【其实是一个连接】，EPOLL_CTL_ADD时增加到红黑树中去，将来epoll_wait时能取出来用
*****************************************************************************************************************/
int CSocket::ngx_epoll_oper_event(int fd, uint32_t eventtype,uint32_t flag,int bcaction,lpngx_connection_t pConn)
{
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	if(eventtype == EPOLL_CTL_ADD)                         //往红黑树中增加节点
	{
		//ev.data.ptr   = (void *)pConn;
		ev.events     = flag;                              //设置标记
		pConn->events = flag;                              //这个连接本身也记录这个标记
	}
	else if(eventtype == EPOLL_CTL_MOD)
	{
		//节点已经在红黑树中，修改节点的事件信息
		ev.events = pConn->events;
		if(bcaction == 0)
		{
			ev.events |=flag;                             //增加某标记
		}
		else if(bcaction == 1)
		{
			ev.events &= ~flag;                           //去掉标记
		}
		else
		{
			ev.events = flag;                              //覆盖整个记录
		}
		pConn->events = ev.events;                         //记录该事件
	}
	else
	{
		//删除红黑树中节点，目前没这个需求，所以将来再扩展
        return  1;  //先直接返回1表示成功	
	}
	
	//内核epoll SYSCALL_DEFINE4(epoll_ctl, int, epfd, int, op, int, fd,		struct epoll_event __user *, event)
	//copy_from_user(&epds, event, sizeof(struct epoll_event))),参数全覆盖
	ev.data.ptr   = (void *)pConn;
	
	if(epoll_ctl(m_epollhandle,eventtype,fd,&ev) == -1)
	{
		ngx_log_stderr(errno,"CSocekt::ngx_epoll_oper_event()中epoll_ctl(%d,%ud,%ud,%d)失败.",fd,eventtype,flag,bcaction);    
        return -1;
	}
	return 1;
}

//开始获取发生的事件消息
//参数unsigned int timer：epoll_wait()阻塞的时长，单位是毫秒；
//返回值，1：正常返回  ,0：有问题返回，一般不管是正常还是问题返回，都应该保持进程继续运行
//本函数被ngx_process_events_and_timers()调用，而ngx_process_events_and_timers()是在子进程的死循环中被反复调用
int CSocket::ngx_epoll_process_events(int timer)
{
	int events = epoll_wait(m_epollhandle,m_events,NGX_MAX_EVENTS,timer);
	if(events == -1)
	{
		if(errno == EINTR)
		{
			ngx_log_error_core(NGX_LOG_INFO,errno,"CSocket::ngx_epoll_process_events()中epoll_wait失败!");
			return 1;
		}
		else
		{
			ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocket::ngx_epoll_process_events()中epoll_wait失败!");
			return 0;
		}
	}
	
	if(events == 0)    //超时，但没有事件来
	{
		if(timer != -1)
		{
            //要求epoll_wait阻塞一定的时间而不是一直阻塞，这属于阻塞到时间了，则正常返回
            return 1;
		}
		//无限等待【所以不存在超时】，但却没返回任何事件，这应该不正常有问题        
        ngx_log_error_core(NGX_LOG_ALERT,0,"CSocekt::ngx_epoll_process_events()中epoll_wait()没超时却没返回任何事件!"); 
        return 0; //非正常返回 
	}
	
	lpngx_connection_t pConn;
	//uintptr_t          instance;
	uint32_t           revents;
	for(int i=0;i<events;i++)
	{
		pConn = (lpngx_connection_t)(m_events[i].data.ptr);
		revents = m_events[i].events;                            //取出事件
		
		//EPOLLIN：表示对应的链接上有数据可以读出（TCP链接的远端主动关闭连接，也相当于可读事件，因为本服务器小处理发送来的FIN包）
		//EPOLLOUT：表示对应的连接上可以写入数据发送【写准备好】
		/*if(revents & (EPOLLERR|EPOLLOUT))
		{
			revents |= EPOLLIN|EPOLLOUT;
		}*/
		
		if(revents &EPOLLIN)                                     //读事件
		{
			//c->r_ready = 1;                                      //标记可以读；【从连接池拿出一个连接时这个连接的所有成员都是0】 
			(this->* (pConn->rhandler))(pConn);
		}
		
		if(revents & EPOLLOUT)                                   //写事件
		{
			if(revents & (EPOLLERR|EPOLLHUP|EPOLLRDHUP))         //客户端关闭了
			{
				--pConn->iThrowsendCount;
			}
			else
			{
				(this->*(pConn->whandler))(pConn);                
			}
		}
		
		
	}//end for(int i=0;i<events;i++)
	return 1;
}

//消息发送线程
void *CSocket::ServerSendQueueThread(void * threadData)
{
	ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
	CSocket *pSocketObj = pThread->_pThis;
	int err;
	std::list<char *>::iterator pos,pos2,posend;
		
	char *pMsgBuf;
	LPSTRUC_MSG_HEADER pMsgHeader;
	LPCOMM_PKG_HEADER  pPkgHeader;
	lpngx_connection_t pConn;
	unsigned short     itmp;
	ssize_t            sendsize;
	
	CMemory *pMemory = CMemory::GetInstance();
		
	while(g_stopEvent == 0)                        //程序运行状态
	{
		if(sem_wait(&pSocketObj->m_semEventSendQueue) == -1)
		{
			if(errno != EINTR)
			{
				ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()中sem_wait(&pSocketObj->m_semEventSendQueue)失败."); 
			}
		}
		
	    if(g_stopEvent != 0)                       //程序要求退出
	    {
	    	break;
	    }
	    
	    if(pSocketObj->m_iSendMsgQueueCount > 0)
	    {
	    	err = pthread_mutex_lock(&pSocketObj->m_sendMessageQueueMutex);              //发送队列互斥
	    	if(err != 0)
	    	{
	    		ngx_log_stderr(err,"CSocekt::ServerSendQueueThread()中pthread_mutex_lock()失败，返回的错误码为%d!",err);
	    	}
	    	
	    	pos    = pSocketObj->m_MsgSendQueue.begin();
	    	posend = pSocketObj->m_MsgSendQueue.end();
	    	while(pos != posend)
	    	{
	    		pMsgBuf    = (*pos);                                                     //拿到消息  消息头+包头+包体
	    		pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;                                //取到消息头
	    		pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf+pSocketObj->m_iLenMsgHeader);  //包头
	    		pConn      = pMsgHeader->pConn;
	    		
	    		if(pConn->iCurrsequence != pMsgHeader->iCurrsequence)
	    		{
	    			pos2 = pos;
	    			pos++;
	    			pSocketObj->m_MsgSendQueue.erase(pos2);
	    			--pSocketObj->m_iSendMsgQueueCount;
	    			pMemory->FreeMemory(pMsgBuf);
	    			continue;
	    		}
	    		
	    		if(pConn->iThrowsendCount > 0)                                           //系统驱动发消息，不能再发送
	    		{
	    			pos++;
	    			continue;
	    		}
	    		--pConn->iSendCount;
	    		pConn->psendMemPointer = pMsgBuf;
	    		pos2 = pos;
	    		pos++;
	    		pSocketObj->m_MsgSendQueue.erase(pos2);
	    		--pSocketObj->m_iSendMsgQueueCount;
	    		pConn->psendbuf = (char *)pPkgHeader;                                   //发送消息体=包头+包体
	    		itmp  =ntohs(pPkgHeader->pkgLen);
	    		pConn->isendlen = itmp;
	    		ngx_log_stderr(errno,"即将发送数据%ud。",pConn->isendlen);
	    		sendsize = pSocketObj->sendproc(pConn,pConn->psendbuf,pConn->isendlen); //发送数据
	    		if(sendsize >0)
	    		{
	    			if(sendsize == pConn->isendlen)                                     //成功发送了所有数据
	    			{
	    				pMemory->FreeMemory(pConn->psendMemPointer);                    //释放内存
	    				pConn->psendMemPointer = NULL;
	    				pConn->iThrowsendCount = 0;
	    				
	    				ngx_log_stderr(0,"CSocekt::ServerSendQueueThread()中数据发送完毕，很好。");  //测试使用
	    			}
	    			else
	    			{
	    				pConn->psendbuf = pConn->psendbuf + sendsize;
	    				pConn->isendlen = pConn->isendlen - sendsize;
	    				++pConn->iThrowsendCount;
	    				if(pSocketObj->ngx_epoll_oper_event(pConn->fd, EPOLL_CTL_MOD, EPOLLOUT, 0, pConn) == -1)
	    				{
	    					//有这情况发生？这可比较麻烦，不过先do nothing
                            ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()ngx_epoll_oper_event()失败.");
	    				}
	    				ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()中数据没发送完毕【发送缓冲区满】，整个要发送%d，实际发送了%d。",pConn->isendlen,sendsize);
	    			}
	    			continue;
	    		}//end if(sendsize >0)
	    		else if(sendsize == 0)
	    		{
	    			//不可能出现这种错误，如果出现可能是对方关闭了连接，所以发送数据为0，直接丢弃掉这个包
	    			pMemory->FreeMemory(pConn->psendMemPointer);   //释放内存
	    			pConn->psendMemPointer = NULL;
	    			pConn->iThrowsendCount = 0;
	    			continue;
	    		}
	    		else if(sendsize == -1)
	    		{
	    			//发送缓冲区已满
	    			++pConn->iThrowsendCount;
	    			if(pSocketObj->ngx_epoll_oper_event(pConn->fd,EPOLL_CTL_MOD,EPOLLOUT,0,pConn) == -1)
	    			{
	    				 ngx_log_stderr(errno,"CSocekt::ServerSendQueueThread()中ngx_epoll_add_event()_2失败.");
	    			}
	    			continue;
	    		}
	    		else
	    		{
	    			pMemory->FreeMemory(pConn->psendMemPointer);  //释放内存
                    pConn->psendMemPointer = NULL;
                    pConn->iThrowsendCount = 0;  //这行其实可以没有，因此此时此刻这东西就是=0的    
                    continue;
	    		}
	    		
	    	}//while(pos != posend)
	    	err = pthread_mutex_lock(&pSocketObj->m_sendMessageQueueMutex);
	    	if(err != 0)
	    	{
	    		ngx_log_stderr(err,"CSocekt::ServerSendQueueThread()pthread_mutex_unlock()失败，返回的错误码为%d!",err);
	    	}  
	    }//pSocketObj->m_iSendMsgQueueCount
	    
	}
	return (void*)0;
}