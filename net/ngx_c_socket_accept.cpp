//������ �� �������ӡ�accept�� �йصĺ���������

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


//����������ר�ú������������ӽ���ʱ���������ᱻngx_epoll_process_events()������
void CSocket::ngx_event_accept(lpngx_connection_t oldc)
{
	struct sockaddr      mysockaddr;                         //Զ�˷�����socket��ַ
	socklen_t            socklen;                            
	int                  err;
	int                  level;
	int                  s;
	static int           use_accept4 = 1;
	lpngx_connection_t   newc;                                //�������ӳ��е�һ������
	
	//ngx_log_stderr(0,"���Ǽ���\n");                         //����ᾪȺ��epoll���������о�Ⱥ������
	
	socklen = sizeof(mysockaddr);      
	do
	{
		if(use_accept4)
		{
			s = accept4(oldc->fd, &mysockaddr, &socklen, SOCK_NONBLOCK);//���ں˻�ȡһ���û������ӣ����һ������SOCK_NONBLOCK��ʾ����һ����������socket����ʡһ��ioctl������Ϊ������������
		}
		else
		{
			s = accept(oldc->fd, &mysockaddr, &socklen);
		}
		if(s == -1)
		{
			err = errno;
			//��accept��send��recv���ԣ��¼�δ����ʱerrnoͨ�������ó�EAGAIN����Ϊ������һ�Ρ�������EWOULDBLOCK����Ϊ���ڴ���������
			if(err == EAGAIN)
			{
				return;
			}
			
			level = NGX_LOG_ALERT;
			if(err == ECONNABORTED) //ECONNRESET���������ڶԷ�����ر��׽��ֺ�
			{
				level = NGX_LOG_ERR;
			}
			//EMFILE:���̵�fd���þ�
            //ulimit -n ,�����ļ�����������,�����1024�Ļ�����Ҫ�Ĵ�;  �򿪵��ļ���������� ,��ϵͳ��fd�����ƺ�Ӳ���ƶ�̧��.
            //ENFILE���errno�Ĵ��ڣ�����һ������system-wide��resource limits������������process-specific��resource limits�����ճ�ʶ��process-specific��resource limits��һ��������system-wide��resource limits��
			else if(err == EMFILE || err == ENFILE)
			{
				 level = NGX_LOG_CRIT;
			}
			ngx_log_error_core(level, errno, "CSocket::ngx_event_accept()��accept4()ʧ����.");
			
			if(use_accept4 && err == ENOSYS)
			{
				use_accept4 = 0;           //���accept4ûʵ��
				continue;                  //����ʹ��accept
			}
			
			if(err == ECONNABORTED)         //�Է��ر��׽���
			{
			}
			if (err == EMFILE || err == ENFILE) 
            {
                //do nothing������ٷ��������ȰѶ��¼���listen socket���Ƴ���Ȼ����Ū����ʱ������ʱ�����������ִ�иú��������Ƕ�ʱ�������и���ǣ���Ѷ��¼����ӵ�listen socket��ȥ��
                //Ŀǰ�Ȳ�����ɡ���Ϊ�ϱ��Ѿ�д�����־�ˡ���
            } 
            return;
		}//end if(s == -1)
		
		
		if(m_onlineUserCount >= m_worker_connections)
		{
			ngx_log_stderr(0,"����ϵͳ�������������û���(�������������%d)���ر���������(%d)��",m_worker_connections,s);
			close(s);
			return;
		}
		//accept4�ɹ�
		newc = ngx_get_connection(s);
		if(newc == NULL)
		{
			//���ӳ������Ӳ����ã�������socektֱ�ӹرղ�����
			if(close(s) == -1)
			{
				 ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::ngx_event_accept()��close(%d)ʧ��!",s);
			}
			return;
		}
		//...........����������ж��Ƿ����ӳ���������������������ڣ�������Բ�����
		
		memcpy(&newc->s_sockaddr,&mysockaddr,socklen);  //�����ͻ��˵�ַ�����Ӷ���Ҫת���ַ���ip��ַ�ο�����ngx_sock_ntop()��
		
		if(!use_accept4)
		{
			if(setnonblocking(s) == false)
			{
				//���÷�����ʧ��
				ngx_close_connection(newc);
				return;
			}
		}
		newc->listening = oldc->listening;                     //���Ӷ��� �ͼ����������������ͨ�����Ӷ����Ҽ������󡾹����������˿ڡ�
		//newc->w_ready = 1;                                     //��ǿ���д��������д�¼��϶���ready
		newc->rhandler = &CSocket::ngx_read_request_handler;   //����������ʱ�Ķ���������
		newc->whandler = &CSocket::ngx_write_request_handler;  //����������ʱ��д������
		 //�ͻ����������͵�һ�ε����ݣ������¼�����epoll���
		/*if(ngx_epoll_add_event(s,1,0,0,EPOLL_CTL_ADD,newc) == -1)//���������ǡ�EPOLLET(����ģʽ����Ե����ET)��
		{
			ngx_close_connection(newc);
			return;
		}*/
		if(ngx_epoll_oper_event(s,EPOLL_CTL_ADD,EPOLLIN|EPOLLRDHUP,0,newc) == -1)
		{
			ngx_close_connection(newc);
			return;
		}
		
		if(m_ifkickTimeCount == 1)
		{
			AddToTimerQueue(newc);
		}
		++m_onlineUserCount;
		break;		
	}while(1);                  
	return;
}

/*��ʱ����
void CSocket::ngx_close_accepted_connection(lpngx_connection_t c)
{
	int fd = c->fd;
	ngx_free_connection(c);
	c->fd = -1;
	if(close(fd))
	{
		ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocket::ngx_close_accepted_connection()��close(%d)ʧ��!",fd);
	}
	return;
}*/