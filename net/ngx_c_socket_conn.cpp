#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>     //errno
//#include <sys/socket.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

ngx_connection_s::ngx_connection_s()         //构造函数
{
	iCurrsequence = 0;
	pthread_mutex_init(&logicPorcMutex,NULL);//互斥量初始化
}
ngx_connection_s::~ngx_connection_s()        //析构函数
{
	pthread_mutex_destroy(&logicPorcMutex) ;  //互斥量释放
}

//连接分配初始化
void ngx_connection_s::GetOneToUse()
{
	++iCurrsequence;
	
	curStat  = _PKG_HD_INIT;                  //收包初始状态
	precvbuf = dataHeadInfo;                 //包信息存放位置
	irecvlen = sizeof(COMM_PKG_HEADER);      //指定接收数据长度
	
	precvMemPointer = NULL;                  //暂未分配内存
	iThrowsendCount = 0;                     //原子的
	psendMemPointer = NULL;                  //发送数据头指针记录
	events          = 0;                     //开始无事件
	lastPingTime    = time(NULL);            //上次Ping的时间
	
	FloodkickLastTime = 0;                   //Flood攻击上次收到包的时间
	FloodAttackCount  = 0;                   //Flood在该时间内收到包的次数统计
}

//回收的连接用来清理一些参数
void ngx_connection_s::PutOneToFree()
{
	++iCurrsequence;
	if(precvMemPointer != NULL)              //接收缓冲区
	{
		CMemory::GetInstance()->FreeMemory(precvMemPointer);
		precvMemPointer = NULL;
	}
	if(psendMemPointer != NULL)              //发送缓冲区
	{
		CMemory::GetInstance()->FreeMemory(psendMemPointer);
		psendMemPointer = NULL;
	}
	iThrowsendCount = 0;
}

void CSocket::initconnection()
{
	lpngx_connection_t pConn;
	CMemory *pMemory = CMemory::GetInstance();
	
	int ilenconnpool = sizeof(ngx_connection_t);
	for(int i = 0;i < m_worker_connections; i++)                                 //初始化建立m_worker_connections个连接
	{
		pConn = (lpngx_connection_t)pMemory->AllocMemory(ilenconnpool,true);
		pConn = new(pConn) ngx_connection_t();
		pConn->GetOneToUse();
		m_connectionList.push_back(pConn);                                       //全部连接  占用+空闲
		m_freeconnectionList.push_back(pConn);                                   //空闲连接
	}
	m_free_connection_n = m_total_connection_n = m_connectionList.size();        //初始阶段两个列表一样大
}

//最终回收连接池，释放内存
void CSocket::clearconnection()
{
	lpngx_connection_t pConn;
	CMemory *pMemory = CMemory::GetInstance();
	while(!m_connectionList.empty())
	{
		pConn = m_connectionList.front();
		m_connectionList.pop_front();
		pConn->~ngx_connection_s();
		pMemory->FreeMemory(pConn);
	}
}

//获取一个空闲连接取用
lpngx_connection_t CSocket::ngx_get_connection(int isock)
{
	CLock lock(&m_connectionMutex);

	if(!m_freeconnectionList.empty())
	{
		lpngx_connection_t pConn = m_freeconnectionList.front();                       //返回第一个空闲连接
		m_freeconnectionList.pop_front();
		pConn->GetOneToUse();
		--m_free_connection_n;
		pConn->fd = isock;
		return pConn;
	}
    
    //没有连接，创建一个连接
    CMemory *pMemory = CMemory::GetInstance();
    lpngx_connection_t pConn = (lpngx_connection_t)pMemory->AllocMemory(sizeof(ngx_connection_t),true);
    pConn = new(pConn)ngx_connection_t();
    pConn->GetOneToUse();
    m_connectionList.push_back(pConn);
    ++m_total_connection_n;
    pConn->fd = isock;
    return pConn;
}

//立即回收
void CSocket::ngx_free_connection(lpngx_connection_t pConn)
{
	CLock lock(&m_connectionMutex);
	
	pConn->PutOneToFree();
	m_freeconnectionList.push_back(pConn); // 加入到空闲连接
	++m_free_connection_n;
	return;
}

//待回收连接入队列
void CSocket::inRecyConnectQueue(lpngx_connection_t pConn)
{
	//ngx_log_stderr(0,"CSocket::inRecyConnectQueue()中执行，连接放入待释放列表.");
	std::list<lpngx_connection_t>::iterator pos;
	bool iffind = false;
	CLock lock(&m_recyconnqueueMutex);              //连接回收列表的互斥量，线程ServerRecyConnectionThread()也要用到这个回收列表
	
	for(pos = m_recyconnectionList.begin(); pos != m_recyconnectionList.begin(); ++pos)
	{
		if((*pos) == pConn)
		{
			iffind = true;
			break;
		}
	}
	
	if(iffind == true)
	{
		return;
	}
	
	pConn->inRecyTime = time(NULL);                 //记录回收时间
	++pConn->iCurrsequence;
	m_recyconnectionList.push_back(pConn);          //放入待回收容器，等待ServerRecyConnectionThread线程处理
	++m_total_recyconnection_n;                     //待释放队列大小+1
	--m_onlineUserCount;                            //用户在线数量
	return;
}


//连接延时回收线程
void* CSocket::ServerRecyConnectionThread(void *threadData)
{
	ThreadItem *pThread = static_cast<ThreadItem*>(threadData);
	CSocket *pSocketObj = pThread->_pThis;
	
	time_t currtime;
	int err;
	std::list<lpngx_connection_t>::iterator pos,posend;
    lpngx_connection_t pConn;
    
    while(1)
    {
    	usleep(200 *1000);
    	if(pSocketObj->m_total_recyconnection_n > 0)
    	{
    		currtime = time(NULL);
    		err = pthread_mutex_lock(&pSocketObj->m_recyconnqueueMutex);
    		if(err != 0)
    		{
    			ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()中pthread_mutex_lock()失败，返回的错误码为%d!",err);
    		}
lblRRTD:
	        pos    = pSocketObj->m_recyconnectionList.begin();
	        posend = pSocketObj->m_recyconnectionList.end();
	        for(; pos != posend; ++pos)
	        {
	        	pConn = (*pos);
	        	if( ((pConn->inRecyTime + pSocketObj->m_RecyConnectionWaitTime) > currtime) && (g_stopEvent == 0) )
	        	{
	        		continue;
	        	}
	        	
	        	if(pConn->iThrowsendCount > 0)
                {
                    //这确实不应该，打印个日志吧；
                    ngx_log_stderr(0,"CSocekt::ServerRecyConnectionThread()中到释放时间却发现p_Conn.iThrowsendCount!=0，这个不该发生");
                    //其他先暂时啥也不敢，路程继续往下走，继续去释放吧。
                }
	        	//预留
	        	--pSocketObj->m_total_recyconnection_n;                             //待释放队列减一
	        	pSocketObj->m_recyconnectionList.erase(pos);                        //删除该节点
	        	pSocketObj->ngx_free_connection(pConn);                             //归还该链接
	        	goto lblRRTD;
	        }//end for
	        err = pthread_mutex_unlock(&pSocketObj->m_recyconnqueueMutex);
	        if(err != 0)
	        {
	        	ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()pthread_mutex_unlock()失败，返回的错误码为%d!",err);
	        }  
    	}//end if
    	if(g_stopEvent == 1)                                                        //退出整个程序
    	{
    		if(pSocketObj->m_total_recyconnection_n > 0)
    		{
    			err = pthread_mutex_lock(&pSocketObj->m_recyconnqueueMutex);
    			if(err != 0)
    			{
    				ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()中pthread_mutex_lock2()失败，返回的错误码为%d!",err);
    			}
    		lblRRTD2:
    			pos    = pSocketObj->m_recyconnectionList.begin();
	            posend = pSocketObj->m_recyconnectionList.end();
	            for(; pos != posend; pos++)
	            {
	            	pConn = (*pos);
	            	--pSocketObj->m_total_recyconnection_n;                             //待释放队列减一
	        	    pSocketObj->m_recyconnectionList.erase(pos);                        //删除该节点
	        	    pSocketObj->ngx_free_connection(pConn);                             //归还该链接
	        	    goto lblRRTD2;
	            }//end for
	            err = pthread_mutex_unlock(&pSocketObj->m_recyconnqueueMutex);
	            if(err != 0)
    			{
    				ngx_log_stderr(err,"CSocekt::ServerRecyConnectionThread()中pthread_mutex_lock2()失败，返回的错误码为%d!",err);
    			}
    		}//end if
    		break;
    	}//end if
    }//end while
    return (void*)0;
}

//把ngx_close_accepted_connection()函数改名，并从文件ngx_socket_accept.cxx迁移到本文件中，并改造其中代码
void CSocket::ngx_close_connection(lpngx_connection_t pConn)
{
	ngx_free_connection(pConn);
	if(close(pConn->fd) == -1)
	{
		ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocket::ngx_close_connection()中close(%d)失败！",pConn->fd);
	}
	pConn->fd = -1 ;
	return;
}