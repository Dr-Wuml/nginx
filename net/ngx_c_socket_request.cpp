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
#include <pthread.h>   //多线程

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"  //自动释放互斥量的一个类
//来数据时候的处理，当连接上有数据来的时候，本函数会被ngx_epoll_process_events()所调用  ,官方的类似函数为ngx_http_wait_request_handler();
void CSocket::ngx_read_request_handler(lpngx_connection_t pConn)
{ 
	bool isflood = false; 
    ssize_t reco = recvproc(pConn,pConn->precvbuf,pConn->irecvlen);
    if(reco <= 0 )
    {
    	return;
    }
    if(pConn->curStat == _PKG_HD_INIT)     
    {
    	if(reco == m_iLenPkgHeader)      //接收到完整包头
    	{
    		ngx_wait_request_handler_proc_p1(pConn,isflood);
    	}
    	else
    	{
    		pConn->curStat  = _PKG_HD_RECVING;
    		pConn->precvbuf = pConn->precvbuf + reco;
    		pConn->irecvlen = pConn->irecvlen - reco;
    	}
    }
    else if(pConn->curStat == _PKG_HD_RECVING)
    {
    	if(pConn->irecvlen == reco)           //收到完整包头
    	{
    		ngx_wait_request_handler_proc_p1(pConn,isflood);
    	}
    	else
    	{
    		pConn->precvbuf = pConn->precvbuf + reco;
    		pConn->irecvlen = pConn->irecvlen - reco;
    	}
    }
    else if(pConn->curStat == _PKG_BD_INIT)
    {
    	if(reco = pConn->irecvlen)           //接收到完整包体
    	{
    		if(m_floodAkEnable == 1)
    		{
    			isflood = TestFlood(pConn);
    		}
    		ngx_wait_request_handler_proc_plast(pConn,isflood);
    	}
    	else
    	{
    		pConn->curStat  = _PKG_BD_RECVING;
    		pConn->precvbuf = pConn->precvbuf + reco;
    		pConn->irecvlen = pConn->irecvlen - reco;
    	}
    }
    else if(pConn->curStat == _PKG_BD_RECVING)
    {
    	if(pConn->irecvlen == reco)          //收到剩余所有包体
    	{
    		if(m_floodAkEnable == 1)
    		{
    			isflood = TestFlood(pConn);
    		}
    		ngx_wait_request_handler_proc_plast(pConn,isflood);
    	}
    	else
    	{
    		pConn->precvbuf = pConn->precvbuf + reco;
    		pConn->irecvlen = pConn->irecvlen - reco;
    	}
    }
    
    if(isflood == true)
    {
    	 zdClosesocketProc(pConn);
    } 
    return;
}

//设置数据来时的写处理函数
void CSocket::ngx_write_request_handler(lpngx_connection_t pConn)         
{
	CMemory *pMemory = CMemory::GetInstance();
	
	ssize_t sendsize = sendproc(pConn, pConn->psendbuf, pConn->isendlen);
	if(sendsize >0 && sendsize != pConn->isendlen)                       //数据未发送完毕
	{
		pConn->psendbuf = pConn->psendbuf + sendsize;
		pConn->isendlen = pConn->isendlen - sendsize;
		return;
	}
	else if(sendsize == -1)
	{
		//这不太可能，可以发送数据时通知write发送数据，发送时系统却通知我发送缓冲区满？
        ngx_log_stderr(errno,"CSocekt::ngx_write_request_handler()时if(sendsize == -1)成立，这很怪异。"); //打印个日志
        return;
	}
	
	if(sendsize > 0 && sendsize == pConn->isendlen)                                        //数据发送完毕，干掉红黑树中的写事件
	{
		if(ngx_epoll_oper_event(pConn->fd, EPOLL_CTL_MOD, EPOLLOUT, 1, pConn) == -1)
		{
			ngx_log_stderr(errno,"CSocekt::ngx_write_request_handler()中ngx_epoll_oper_event()失败。");
		}
		 ngx_log_stderr(0,"CSocekt::ngx_write_request_handler()中数据发送完毕，很好。");
	}
	
	if(sem_post(&m_semEventSendQueue) == -1)
	{
		 ngx_log_stderr(0,"CSocekt::ngx_write_request_handler()中sem_post(&m_semEventSendQueue)失败.");
	}
	pMemory->FreeMemory(pConn->psendMemPointer);
	pConn->psendMemPointer = NULL;
	--pConn->iThrowsendCount;
	
	return ;
}
ssize_t CSocket::recvproc(lpngx_connection_t c,char *buff,ssize_t buflen)     //接收消息
{
	ssize_t n;
	n = recv(c->fd, buff, buflen, 0);
	if(n == 0)                                      //客户端正常关闭 
	{
        //ngx_log_stderr(0,"连接被客户端正常关闭[4路挥手关闭]！");
        //ngx_close_connection(c);
        zdClosesocketProc(c);
		return -1;
	}
	//客户端连接还在
	if(n < 0)
	{
		if(errno == EAGAIN || errno == EWOULDBLOCK)
		{
			ngx_log_stderr(errno,"CSocket::recvproc()中errno == EAGAIN || errno == EWOULDBLOCK成立！");
			return -1;
		}
		if(errno == EINTR)
		{
			ngx_log_stderr(errno,"CSocket::recvproc()中errno == EINTR成立！");
			return -1;
		}
		if(errno == ECONNRESET)
		{
			//ngx_log_stderr(errno,"CSocket::recvproc()中errno == ECONNRESET成立，客户端非正常关闭！");
		}
		else
		{
			ngx_log_stderr(errno,"CSocket::recvproc()中发生错误！");
		}
		//ngx_log_stderr(0,"连接被客户端 非 正常关闭！");
		//ngx_close_connection(c);
		zdClosesocketProc(c);
		return -1;
	}
	return n;
}

//包处理阶段1添加消息头，存包头
void CSocket::ngx_wait_request_handler_proc_p1(lpngx_connection_t pConn,bool &isflood)
{
	CMemory *pMemory = CMemory::GetInstance();
	
	LPCOMM_PKG_HEADER pPkgHeader;
	pPkgHeader = (LPCOMM_PKG_HEADER)pConn->dataHeadInfo;
	unsigned short e_pkgLen;
	e_pkgLen = ntohs(pPkgHeader->pkgLen);           //ntohs网络序转本机序,htons本机序转网络序
	if(e_pkgLen < m_iLenPkgHeader)                  //报文长度小于包头长度
	{
		pConn->curStat  = _PKG_HD_INIT;
		pConn->precvbuf = pConn->dataHeadInfo;
		pConn->irecvlen = m_iLenPkgHeader;
	}
	else if(e_pkgLen > (_PKG_MAX_LENGTH-1000))      //报文长度大于指定长度
	{
		pConn->curStat  = _PKG_HD_INIT;
		pConn->precvbuf = pConn->dataHeadInfo;
		pConn->irecvlen = m_iLenPkgHeader;
	}
	else
	{
		//合法包头
		char *pTmpBuffer  = (char *)pMemory->AllocMemory(m_iLenMsgHeader + e_pkgLen,false);  //消息头+消息体
		pConn->precvMemPointer = pTmpBuffer;
		
		//先填写消息头
		LPSTRUC_MSG_HEADER ptmpMsgHeader = (LPSTRUC_MSG_HEADER)pTmpBuffer;
		ptmpMsgHeader->pConn = pConn;
		ptmpMsgHeader->iCurrsequence = pConn->iCurrsequence;                                     //收到包时的连接池中连接序号记录到消息头里来，以备将来用；
		
		//再填写包头内容
		pTmpBuffer += m_iLenMsgHeader;
		memcpy(pTmpBuffer,pPkgHeader,m_iLenPkgHeader);
		if(e_pkgLen == m_iLenPkgHeader)
		{
			if(m_floodAkEnable == 1)
			{
				isflood = TestFlood(pConn);
			}
			ngx_wait_request_handler_proc_plast(pConn,isflood);
		}
		else
		{
			pConn->curStat  = _PKG_BD_INIT;
			pConn->precvbuf = pTmpBuffer + m_iLenPkgHeader;
			pConn->irecvlen = e_pkgLen - m_iLenPkgHeader;
		}	
	}
	return;
}

//包处理阶段二 ，完整包之后的处理
void CSocket::ngx_wait_request_handler_proc_plast(lpngx_connection_t pConn,bool &isflood)
{
	//将消息放入接收消息处理队列
	//int irmqc = 0;
	//inMsgRecvQueue(c->precvMemPointer,irmqc);
	//g_threadpool.Call(irmqc);
	if(isflood == false)
	{
		g_threadpool.inMsgRecvQueueAndSignal(pConn->precvMemPointer);
	}
    else
    {
    	CMemory *pMemory = CMemory::GetInstance();
    	pMemory->FreeMemory(pConn->precvMemPointer);                     //释放内存
    }
	//参数处理
	//pConn->ifnewrecvMem   = false;
	pConn->precvMemPointer = NULL;
	pConn->curStat        = _PKG_HD_INIT;    //还原收包状态，收下一个包
	pConn->precvbuf       = pConn->dataHeadInfo;  //设置收包位置
	pConn->irecvlen       = m_iLenPkgHeader; //设置收包大小
	return;
}

//发送数据专用函数，返回本次发送的字节数
//返回 > 0，成功发送了一些字节
//=0，估计对方断了
//-1，errno == EAGAIN ，本方发送缓冲区满了
//-2，errno != EAGAIN != EWOULDBLOCK != EINTR ，一般我认为都是对端断开的错误
ssize_t CSocket::sendproc(lpngx_connection_t c,char *buff,ssize_t size)
{
	ssize_t n;
	for(;;)
	{
		n = send(c->fd, buff, size, 0);
		if(n > 0)
		{
			return n;
		}
		
		if(n == 0)         //连接超时断开导致，放入recv中处理该情况
		{
			return 0;
		}
		
		if(errno == EAGAIN) //内核缓冲区满了
		{
			return -1;
		}
		
		if(errno == EINTR)
		{
			ngx_log_stderr(errno,"CSocekt::sendproc()中send()失败."); 
		}
		else                //同样放入recv模快处理
		{
			return -2;
		}
	}
}
//消息入队列
/*void CSocket::inMsgRecvQueue(char *buf,int irmqc)
{
	CLock lock(&m_recvMessageQueueMutex);
	m_MsgRecvQueue.push_back(buf);
	 ++m_iRecvMsgQueueCount;                  //收消息队列数字+1，个人认为用变量更方便一点，比 m_MsgRecvQueue.size()高效
    irmqc = m_iRecvMsgQueueCount;            //接收消息队列当前信息数量保存到irmqc
	//tmpoutMsgRecvQueue();
	ngx_log_stderr(0,"success收到了一个完整的数据包！");
	return;
}

char *CSocket::outMsgRecvQueue() 
{
    CLock lock(&m_recvMessageQueueMutex);	//互斥
    if(m_MsgRecvQueue.empty())
    {
        return NULL; //也许会存在这种情形： 消息本该有，但被干掉了，这里可能为NULL的？        
    }
    char *sTmpMsgBuf = m_MsgRecvQueue.front(); //返回第一个元素但不检查元素存在与否
    m_MsgRecvQueue.pop_front();                //移除第一个元素但不返回	
    --m_iRecvMsgQueueCount;                    //收消息队列数字-1
    return sTmpMsgBuf;                         
}*/

//消息处理线程主函数，专门处理各种接收到的TCP消息
//pMsgBuf：发送过来的消息缓冲区，消息本身是自解释的，通过包头可以计算整个包长
//         消息本身格式【消息头+包头+包体】 
void CSocket::threadRecvProcFunc(char *pMsgBuf)
{
    return;
}
/*void CSocket::tmpoutMsgRecvQueue()
{
	if(m_MsgRecvQueue.empty())                        //消息队列为空，退出
	{
		return ;
	}
	int size = m_MsgRecvQueue.size();                 //一千条以内可接受
	if(size < 1000)
	{
		return;
	}
	CMemory *pMemory = CMemory::GetInstance();
	int cha = size - 500;                             //一次性去掉500条
	for(int i = 0;i < cha; i++)
	{
		char *sTmpMsgBuf = m_MsgRecvQueue.front();    //取出一条
		m_MsgRecvQueue.pop_front();                   //移除第一条
		pMemory->FreeMemory(sTmpMsgBuf);              //先释放掉这条
	}
	return ;
}*/

