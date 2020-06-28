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
#include "ngx_c_threadpool.h"

//构造函数
CSocket::CSocket()
{
    m_ListenPortCount = 1;      //默认监听一个端口
    m_worker_connections = 1;   //epoll最大连接数
    
    //epoll相关
    m_epollhandle = -1 ;        //epoll返回的句柄
    m_pconnections = NULL;      //连接池
    m_pfree_connections = NULL;  //连接池中的空闲连接
    //m_pread_events = NULL;      //读事件数组
    //m_pwrite_events = NULL;     //写事件数组
    
    //一些和网络通讯有关的常用变量值
    m_iLenPkgHeader = sizeof(COMM_PKG_HEADER);
    m_iLenMsgHeader = sizeof(STRUC_MSG_HEADER);
    
    //m_iRecvMsgQueueCount = 0;    //收消息队列
    //pthread_mutex_init(&m_recvMessageQueueMutex, NULL); //互斥量初始化
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
    
    //连接池相关内存释放
    /*
    if(m_pwrite_events !=NULL) //写事件释放
    {
    	delete []m_pwrite_events;
    }
    
    if(m_pread_events !=NULL)  //读事件释放
    {
    	delete []m_pread_events;
    }
    */
    if(m_pconnections != NULL) //连接池释放S
    {
    	delete [] m_pconnections;
    }
    //释放接收消息队列的缓存
   // clearMsgRecvQueue();
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
    CConfig *pConfig = CConfig::GetInstance();
    /*取得需要监听的端口个数*/
    m_ListenPortCount = pConfig -> GetIntDefault("ListenPortCount", m_ListenPortCount);
    m_worker_connections = pConfig->GetIntDefault("worker_connections",m_worker_connections);
}


//初始化函数，成功返回true，失败返回false
bool CSocket::Initialize()
{
	ReadConf();
    bool reco = ngx_open_listening_sockets();
    return reco;
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
	
	m_connection_n = m_worker_connections;                //连接池大小
	m_pconnections = new ngx_connection_t[m_connection_n];//申请连接池内存
	
	
	//m_pread_events = new ngx_event_t[m_connection_n];
	//m_pwrite_events = new ngx_event_t[m_connection_n];
	/*for(int i=0; i<m_connection_n; i++)
	{
		m_pconnections[i].instance = 1;                   //失效标志位设成1：失效
	}*/
	
	int i = m_connection_n ;                              //连接池中连接数
	lpngx_connection_t next = NULL;                       //后继节点初始化为NULL
	lpngx_connection_t c = m_pconnections;                //连接池数组首地址
	
	//初始化连接池
	do
	{
		i--;
		c[i].data = next;                                 //前插法
		c[i].fd =-1;                                      //初始化无连接
		c[i].instance = 1;                                //失效先置为1
		c[i].iCurrsequence = 0;                           //序号从0开始
		next = &c[i];                                     //指针前移
	}while(i);//采用前插法
	m_pfree_connections = next;                           //空闲链表头指针
	m_free_connection_n = m_connection_n;                 //空闲连接池大小
	
	std::vector<lpngx_listening_t>::iterator pos;
	for(pos=m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); ++pos)
	{
		c = ngx_get_connection((*pos)->fd);               //获取空闲连接
		if(c == NULL)
		{
			ngx_log_stderr(errno,"CSocket::ngx_epoll_init()中ngx_get_connection失败.");
			exit(2);
		}
		c->listening = (*pos);                             //连接对象和监听对象关联，方便寻找监听对象
		(*pos)->connection = c;                            //监听对象和连接对象关联，方便寻找连接对象
		//recv->accept = 1;                                  //监听端口必须设置accept标志为1
		c->rhandler = &CSocket::ngx_event_accept;        //对监听端口的读事件设置处理方法，监听端口关心的就是读事件
		
		//参数1：socket句柄，参数2：读事件，参数3：写事件，参数4：其他补充标记，参数5：事件类型，参数6：连接池中的连接
		if(ngx_epoll_add_event((*pos)->fd,1,0,0,EPOLL_CTL_ADD,c) == -1)
		{
			exit(2);
		}
	}
	return 1;
}

/*
//(2)监听端口开始工作，监听端口要开始工作，必须为其增加读事件，因为监听端口只关心读事件
void CSocekt::ngx_epoll_listenportstart()
{
    std::vector<lpngx_listening_t>::iterator pos;	
	for(pos = m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); ++pos) //vector
	{	
        //本函数如果失败，直接退出
        ngx_epoll_add_event((*pos)->fd,1,0); //只关心读事件
    } //end for
    return;
}
*/
/*********************************************************************************************************************
                epoll增加事件，可能被ngx_epoll_init()等函数调用
                fd:句柄，一个socket
                readevent：表示是否是个读事件，0是，1不是
                writeevent：表示是否是个写事件，0是，1不是
                otherflag：其他需要额外补充的标记，弄到这里
                eventtype：事件类型  ，一般就是用系统的枚举值，增加，删除，修改等;
                c：对应的连接池中的连接的指针
                返回值：成功返回1，失败返回-1；
**********************************************************************************************************************/
int CSocket::ngx_epoll_add_event(int fd, int readevent, int writeevent, uint32_t otherflag, uint32_t eventtype, lpngx_connection_t c)
{
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	
	if(readevent == 1)
	{
		//读事件,官方nginx没有使用EPOLLERR，【有些范例中是使用EPOLLERR的】
		ev.events = EPOLLIN|EPOLLRDHUP;//EPOLLIN读事件，也就是read ready,EPOLLRDHUP 客户端关闭连接，断连
		
		//只支持非阻塞socket的高速模式
		//ET：边缘触发】，对于accetp来说，如果加这个EPOLLET，则客户端连入时，epoll_wait()只会返回一次该事件，
		//EPOLLLT【水平触发：低速模式】，则客户端连入时，epoll_wait()会被触发多次，一直到用accept()来处理；
		//ev.events |= (ev.events | EPOLLET);
		
	}
	else
	{
		//其他事件待处理
	}
	
	if(otherflag != 0)
	{
		ev.events |= otherflag; 
	}
	
	ev.data.ptr = (void*)((uintptr_t)c | c->instance);//将对象放进去，后续来事件时，用epoll_wait()后，可将该对象取出来用
	
	if(epoll_ctl(m_epollhandle,eventtype,fd,&ev) == -1)
	{
		ngx_log_stderr(errno,"CSocket::ngx_epoll_add_event()中epoll_clt(%d,%d,%d,%u,%u)失败.",fd,readevent,writeevent,otherflag,eventtype);
		return -1;
	}
	return 1;
}

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
	
	lpngx_connection_t c;
	uintptr_t          instance;
	uint32_t           revents;
	for(int i=0;i<events;i++)
	{
		c = (lpngx_connection_t)(m_events[i].data.ptr);
		instance = (uintptr_t)c & 1;                             //取得是epoll_add_events()里面的值
		c = (lpngx_connection_t) ((uintptr_t)c & (uintptr_t) ~1);
		if(c->fd == -1)                                          //过滤过期事件
		{
			ngx_log_error_core(NGX_LOG_DEBUG,0,"CSocket::ngx_epoll_process_events()中遇到了fd =-1的过期事件:%p.",c);
			continue;
		}
		
		if(c->instance != instance)
		{
			 ngx_log_error_core(NGX_LOG_DEBUG,0,"CSocekt::ngx_epoll_process_events()中遇到了instance值改变的过期事件:%p.",c); 
			 continue;
		}
		
		revents = m_events[i].events;                            //取出事件
		
		//EPOLLIN：表示对应的链接上有数据可以读出（TCP链接的远端主动关闭连接，也相当于可读事件，因为本服务器小处理发送来的FIN包）
		//EPOLLOUT：表示对应的连接上可以写入数据发送【写准备好】
		if(revents & (EPOLLERR|EPOLLOUT))
		{
			revents |= EPOLLIN|EPOLLOUT;
		}
		
		if(revents &EPOLLIN)                                     //读事件
		{
			//c->r_ready = 1;                                      //标记可以读；【从连接池拿出一个连接时这个连接的所有成员都是0】 
			(this->* (c->rhandler))(c);
		}
		
		if(revents & EPOLLOUT)        //暂不处理
		{
			 ngx_log_stderr(errno,"do nothing now.");
		}
		
		
	}//end for(int i=0;i<events;i++)
	return 1;
}