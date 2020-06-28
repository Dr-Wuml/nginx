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

//获取一个空闲连接取用
lpngx_connection_t CSocket::ngx_get_connection(int isock)
{
    //ngx_log_stderr(errno,"CSocket::ngx_get_connection isock = %d.",isock);
	lpngx_connection_t c = m_pfree_connections;           //空闲连接表头
	if(c == NULL)
	{
		ngx_log_stderr(errno,"CSocket::ngx_get_connection()中空闲链表为空，请检查！");
		return NULL;
	}
    //ngx_log_stderr(errno,"CSocket::ngx_get_connection c = %p.",c);
	m_pfree_connections = c->data;
	m_free_connection_n--;
	uintptr_t instance = c->instance;
	uint64_t  iCurrsequence = c->iCurrsequence;
	memset(c, 0, sizeof(ngx_connection_t));
	c->fd = isock;
	c->curStat = _PKG_HD_INIT;                           //收包状态处于 初始状态，准备接收数据包头【状态机】
	
	c->precvbuf = c->dataHeadInfo;                       //收包先收到这里，因为先收包头，所以收数据的buff是dataHeadInfo
	c->irecvlen = sizeof(COMM_PKG_HEADER);               //指定收数据的长度，先要求收包头这么长字节的数据
	
	c->ifnewrecvMem = false;                             //标记没有new内存，所以不用释放	
	c->pnewMemPointer = NULL;                            //无new，内存为空
	
	c->instance = !instance;
	c->iCurrsequence = iCurrsequence;
	++c->iCurrsequence;
	//wev->write = 1;
	return c;
}

void CSocket::ngx_free_connection(lpngx_connection_t c)
{
	if(c->ifnewrecvMem == true)
	{
		//此链接分配过内存，释放该块内存
		CMemory::GetInstance()->FreeMemory(c->pnewMemPointer);
		c->pnewMemPointer = NULL;
		c->ifnewrecvMem = false;
	}
	
	c->data = m_pfree_connections;
	++c->iCurrsequence;                                   //回收后，该值就加1,用于判断某些网络事件是否过期
	m_pfree_connections = c;
	++m_free_connection_n;
	return;
}

void CSocket::ngx_close_connection(lpngx_connection_t c)
{
	if(close(c->fd) == -1)
	{
		ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocket::ngx_close_connection()中close(%d)失败！",c->fd);
	}
	c->fd -1 ;
	ngx_free_connection(c);
	return;
}
