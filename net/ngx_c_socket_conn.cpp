#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO��
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

//��ȡһ����������ȡ��
lpngx_connection_t CSocket::ngx_get_connection(int isock)
{
    //ngx_log_stderr(errno,"CSocket::ngx_get_connection isock = %d.",isock);
	lpngx_connection_t c = m_pfree_connections;           //�������ӱ�ͷ
	if(c == NULL)
	{
		ngx_log_stderr(errno,"CSocket::ngx_get_connection()�п�������Ϊ�գ����飡");
		return NULL;
	}
    //ngx_log_stderr(errno,"CSocket::ngx_get_connection c = %p.",c);
	m_pfree_connections = c->data;
	m_free_connection_n--;
	uintptr_t instance = c->instance;
	uint64_t  iCurrsequence = c->iCurrsequence;
	memset(c, 0, sizeof(ngx_connection_t));
	c->fd = isock;
	c->curStat = _PKG_HD_INIT;                           //�հ�״̬���� ��ʼ״̬��׼���������ݰ�ͷ��״̬����
	
	c->precvbuf = c->dataHeadInfo;                       //�հ����յ������Ϊ���հ�ͷ�����������ݵ�buff��dataHeadInfo
	c->irecvlen = sizeof(COMM_PKG_HEADER);               //ָ�������ݵĳ��ȣ���Ҫ���հ�ͷ��ô���ֽڵ�����
	
	c->ifnewrecvMem = false;                             //���û��new�ڴ棬���Բ����ͷ�	
	c->pnewMemPointer = NULL;                            //��new���ڴ�Ϊ��
	
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
		//�����ӷ�����ڴ棬�ͷŸÿ��ڴ�
		CMemory::GetInstance()->FreeMemory(c->pnewMemPointer);
		c->pnewMemPointer = NULL;
		c->ifnewrecvMem = false;
	}
	
	c->data = m_pfree_connections;
	++c->iCurrsequence;                                   //���պ󣬸�ֵ�ͼ�1,�����ж�ĳЩ�����¼��Ƿ����
	m_pfree_connections = c;
	++m_free_connection_n;
	return;
}

void CSocket::ngx_close_connection(lpngx_connection_t c)
{
	if(close(c->fd) == -1)
	{
		ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocket::ngx_close_connection()��close(%d)ʧ�ܣ�",c->fd);
	}
	c->fd -1 ;
	ngx_free_connection(c);
	return;
}
