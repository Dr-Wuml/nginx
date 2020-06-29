#ifndef __NGX_C_SOCKET_H__
#define __NGX_C_SOCKET_H__

#include <vector>
#include <list>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <pthread.h>    //多线程
#include <semaphore.h>  //信号量 
#include <atomic>       //c++11里的原子操作

#include "ngx_comm.h"


#define NGX_LISTEN_BACKLOG 511 //已连接队列的最大数量
#define NGX_MAX_EVENTS     512 //epoll_wait一次最多接收的事件个数


typedef unsigned char u_char;
typedef struct ngx_listening_s   ngx_listening_t,*lpngx_listening_t;  //监听端口结构
typedef struct ngx_connection_s  ngx_connection_t,*lpngx_connection_t;//epoll连接结构体
typedef class  CSocket           CSocket;

typedef void (CSocket::*ngx_event_handler_pt)(lpngx_connection_t c);  //定义成员函数指针
 
//监听结构体
struct ngx_listening_s
{
    int                port;       //监听的端口号
    int                fd;         //套接字的句柄socket
    lpngx_connection_t connection; //连接池中的一个连接
};


//(1)该结构表示一个TCP连接【客户端主动发起的、Nginx服务器被动接受的TCP连接】
struct ngx_connection_s
{
	
	ngx_connection_s();                        //构造函数
	virtual ~ngx_connection_s();               //析构函数
	void GetOneToUse();                        //初始化一些参数
	void PutOneToFree();                       //回收参数操作
	
    int                       fd;              //socket套接字句柄
    lpngx_listening_t         listening;       //指向监听套接字对应的lpngx_listening_t的内存首地址
    //unsigned                  instance:1;      //位域，失效标志位：0：有效，1：失效
    uint64_t                  iCurrsequence;   //一个序号，每次分配出去时+1，此法也有可能在一定程度上检测错包废包
    struct sockaddr           s_sockaddr;      //保存对方地址信息用的
    //char                      add_text[100];   //地址的文本信息，255.255.255.255

    //uint8_t                   r_ready;         //读准备好标记
    //uint8_t                   w_ready;         //写准备好标记 
    
    ngx_event_handler_pt      rhandler;        //读事件的相关处理
    ngx_event_handler_pt      whandler;        //写事件的相关处理
    
    //和epoll事件有关
	uint32_t                  events;                         //和epoll事件有关  
    
    //收包相关 
    unsigned char             curStat;                        //当前收包状态
    char                      dataHeadInfo[_DATA_BUFSIZE_];   //用于保存收到的数据的包头信息
    char                      *precvbuf;                      //接收数据的缓冲区头指针
    unsigned int              irecvlen;                       //需要接收数据的大小
    char                      *precvMemPointer;                //new出来用于收包的内存首地址
    
    pthread_mutex_t           logicPorcMutex;                 //逻辑处理的相关互斥量
    
    //发包相关
    std::atomic<int>          iThrowsendCount;                //发送消息，如果发送缓冲区满了，则需要通过epoll事件来驱动消息的继续发送，所以如果发送缓冲区满，则用这个变量标记
    char                      *psendMemPointer;               //释放发送完的内存
    char                      *psendbuf;                      //发送数据的缓冲区头指针 包头+包体
    unsigned int              isendlen;                       //发送数据长度
    
    //入到资源回收站的时间
    time_t                    inRecyTime;
    
    lpngx_connection_t        next;            //指针，指向下一个本类型的对象,空闲连接池的位置
};


//消息头，方便拓展使用
typedef struct _STRUC_MSG_HEADER
{
	lpngx_connection_t          pConn;          //记录对应的链接
	uint64_t                    iCurrsequence;  //记录对应的链接序号，判断链接是否作废
}STRUC_MSG_HEADER,*LPSTRUC_MSG_HEADER;


//socket相关类
class CSocket
{
public:
    CSocket();                                                   //构造函数
    virtual ~CSocket();                                          //析构函数
    virtual bool Initialize();                                   //初始化函数[父进程]
    virtual bool Initialize_subproc();                           //初始化函数[子进程]
    virtual void Shutdown_subproc();                             //关闭退出函数[子进程]
    
public:
	virtual void threadRecvProcFunc(char *pMsgBuf);                    //处理客户端请求，虚函数，因为将来可以考虑自己来写子类继承本类
public:
    int ngx_epoll_init();                                        //epoll功能初始化
    //epoll增加事件
    //int ngx_epoll_add_event(int fd,int readevent,int writeevent,uint32_t otherflag,uint32_t eventtype,lpngx_connection_t c);
    int ngx_epoll_process_events(int timer);                     //epoll等待接收和处理事件
    
    //epoll操作事件
    int ngx_epoll_oper_event(int fd,uint32_t eventtype,uint32_t flag,int bcaction,lpngx_connection_t pConn);
protected:
	void msgSend(char *psendbuf);                                //发送消息送入待发送队列
private:
    void ReadConf();                                             //配置项读取
    bool ngx_open_listening_sockets();                           //监听必须得端口[支持多端口]
    void ngx_close_listening_sockets();                          //关闭监听套接字
    bool setnonblocking(int sockfd);                             //设置非阻塞套接字
    

    //业务处理函数
    void ngx_event_accept(lpngx_connection_t oldc);                   //建立新连接
    void ngx_wait_request_handler(lpngx_connection_t pConn);          //设置数据来时的读处理函数
    void ngx_close_connection(lpngx_connection_t pConn);              //通用链接关闭函数，资源用这个函数释放
    
    ssize_t recvproc(lpngx_connection_t pConn,char *buff,ssize_t buflen);  //接收从客户端传来的数据专用
    void ngx_wait_request_handler_proc_p1(lpngx_connection_t pConn);       //包头接收完整后的处理
    void ngx_wait_request_handler_proc_plast(lpngx_connection_t pConn);    //收到一个完成包后的处理
    void clearMsgSendQueue();                                              //清理发送消息队列  
    
    ssize_t sendproc(lpngx_connection_t c,char *buff,ssize_t size);        //将数据发送到客户端
    
    //获取对端信息
    size_t ngx_sock_ntop(struct sockaddr *sa,int port,u_char *text,size_t len); //根据参数1给定的信息，获取地址端口字符串，返回这个字符串的长度
    //连接池或连接相关
    void initconnection();                                        //初始化连接池
    void clearconnection();                                       //回收连接池
    lpngx_connection_t ngx_get_connection(int isock);             //从连接池中获取一个空闲连接
    void ngx_free_connection(lpngx_connection_t pConn);           //归还参数c所代表的连接到连接池中
    void inRecyConnectQueue(lpngx_connection_t pConn);            //回收连接入队列
    
    //线程相关函数
    //static void* ServerSendQueueThread(void *threadData);         //专门用来发送数据的线程
	static void* ServerRecyConnectionThread(void *threadData);    //专门用来回收连接的线程
protected:
	//和网络通讯相关的变量
    size_t                         m_iLenPkgHeader;              //sizeof(COMM_PKG_HEADER);
    size_t                         m_iLenMsgHeader;              //sizeof(STRUC_MSG_HEADER);
private:
	
	struct ThreadItem
	{
		pthread_t                  _Handle;                      //线程句柄
		CSocket                    *_pThis;                      //线程池地址
		bool                       ifrunning;                    //标记线程池中的线程是否处于运行状态
		
		//构造函数
		ThreadItem(CSocket *pthis):_pThis(pthis),ifrunning(false){}
		//析构函数
		~ThreadItem(){}
	};
    
    int                            m_worker_connections;         //epoll连接的最大数
    int                            m_ListenPortCount;            //所监听的端口数量
    int                            m_epollhandle;                //epoll_create返回的句柄

    //和连接池有关的
    std::list<lpngx_connection_t>  m_connectionList;              //连接列表
    std::list<lpngx_connection_t>  m_freeconnectionList;         //空闲连接池列表
    std::atomic<int>               m_total_connection_n;         //连接池总连接数
    std::atomic<int>               m_free_connection_n;          //连接池空闲连接数
    pthread_mutex_t                m_connectionMutex;            //连接互斥量斥m_freeconnectionList，m_connectionList
    pthread_mutex_t                m_recyconnqueueMutex;         //连接回收互斥量
    std::list<lpngx_connection_t>  m_recyconnectionList;         //连接回收池列表
    std::atomic<int>               m_total_recyconnection_n;     //待释放连接的大小
    int                            m_RecyConnectionWaitTime;       //等待m_RecyConntionWaitTime秒后回收该链接
    
    //lpngx_connection_t             m_pfree_connections;          //控线连接链表头，把空闲的连接专门用该成员记录

    std::vector<lpngx_listening_t> m_ListenSocketList;           //监听套接字队列
    struct epoll_event             m_events[NGX_MAX_EVENTS];     //用于在epoll_wait()承载返回的所发生的事件
    
    //消息队列
    std::list<char *>              m_MsgSendQueue;               //发送数据消息队列
    std::atomic<int>               m_iSendMsgQueueCount;         //发送数据消息队列大小
    	
    //多线程
    std::vector<ThreadItem *>      m_threadVector;              //线程容器
    pthread_mutex_t                m_sendMessageQueueMutex;      //发消息队列互斥量
    sem_t                          m_semEventSendQueue;          //处理消息线程的信号量

};
#endif //!__NGX_C_SOCKET_H__
