//和网络 中 接受连接【accept】 有关的函数放这里

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


//建立新连接专用函数，当新连接进入时，本函数会被ngx_epoll_process_events()所调用
void CSocket::ngx_event_accept(lpngx_connection_t oldc)
{
	struct sockaddr      mysockaddr;                         //远端服务器socket地址
	socklen_t            socklen;                            
	int                  err;
	int                  level;
	int                  s;
	static int           use_accept4 = 1;
	lpngx_connection_t   newc;                                //代表连接池中的一个连接
	
	//ngx_log_stderr(0,"这是几个\n");                         //这里会惊群，epoll技术本身有惊群的问题
	
	socklen = sizeof(mysockaddr);      
	do
	{
		if(use_accept4)
		{
			s = accept4(oldc->fd, &mysockaddr, &socklen, SOCK_NONBLOCK);//从内核获取一个用户端连接，最后一个参数SOCK_NONBLOCK表示返回一个非阻塞的socket，节省一次ioctl【设置为非阻塞】调用
		}
		else
		{
			s = accept(oldc->fd, &mysockaddr, &socklen);
		}
		if(s == -1)
		{
			err = errno;
			//对accept、send和recv而言，事件未发生时errno通常被设置成EAGAIN（意为“再来一次”）或者EWOULDBLOCK（意为“期待阻塞”）
			if(err == EAGAIN)
			{
				return;
			}
			
			level = NGX_LOG_ALERT;
			if(err == ECONNABORTED) //ECONNRESET错误则发生在对方意外关闭套接字后
			{
				level = NGX_LOG_ERR;
			}
			//EMFILE:进程的fd已用尽
            //ulimit -n ,看看文件描述符限制,如果是1024的话，需要改大;  打开的文件句柄数过多 ,把系统的fd软限制和硬限制都抬高.
            //ENFILE这个errno的存在，表明一定存在system-wide的resource limits，而不仅仅有process-specific的resource limits。按照常识，process-specific的resource limits，一定受限于system-wide的resource limits。
			else if(err == EMFILE || err == ENFILE)
			{
				 level = NGX_LOG_CRIT;
			}
			ngx_log_error_core(level, errno, "CSocket::ngx_event_accept()中accept4()失败了.");
			
			if(use_accept4 && err == ENOSYS)
			{
				use_accept4 = 0;           //标记accept4没实现
				continue;                  //继续使用accept
			}
			
			if(err == ECONNABORTED)         //对方关闭套接字
			{
			}
			if (err == EMFILE || err == ENFILE) 
            {
                //do nothing，这个官方做法是先把读事件从listen socket上移除，然后再弄个定时器，定时器到了则继续执行该函数，但是定时器到了有个标记，会把读事件增加到listen socket上去；
                //目前先不处理吧【因为上边已经写这个日志了】；
            } 
            return;
		}//end if(s == -1)
		
		
		if(m_onlineUserCount >= m_worker_connections)
		{
			ngx_log_stderr(0,"超出系统允许的最大连入用户数(最大允许连入数%d)，关闭连入请求(%d)。",m_worker_connections,s);
			close(s);
			return;
		}
		//accept4成功
		newc = ngx_get_connection(s);
		if(newc == NULL)
		{
			//连接池中连接不够用，需把这个socekt直接关闭并返回
			if(close(s) == -1)
			{
				 ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::ngx_event_accept()中close(%d)失败!",s);
			}
			return;
		}
		//...........将来这里会判断是否连接超过最大允许连接数，现在，这里可以不处理
		
		memcpy(&newc->s_sockaddr,&mysockaddr,socklen);  //拷贝客户端地址到连接对象【要转成字符串ip地址参考函数ngx_sock_ntop()】
		
		if(!use_accept4)
		{
			if(setnonblocking(s) == false)
			{
				//设置费阻塞失败
				ngx_close_connection(newc);
				return;
			}
		}
		newc->listening = oldc->listening;                     //连接对象 和监听对象关联，方便通过连接对象找监听对象【关联到监听端口】
		//newc->w_ready = 1;                                     //标记可以写，新连接写事件肯定是ready
		newc->rhandler = &CSocket::ngx_read_request_handler;   //设置数据来时的读处理函数，
		newc->whandler = &CSocket::ngx_write_request_handler;  //设置数据来时的写处理函数
		 //客户端主动发送第一次的数据，将读事件加入epoll监控
		/*if(ngx_epoll_add_event(s,1,0,0,EPOLL_CTL_ADD,newc) == -1)//其他补充标记【EPOLLET(高速模式，边缘触发ET)】
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

/*暂时废弃
void CSocket::ngx_close_accepted_connection(lpngx_connection_t c)
{
	int fd = c->fd;
	ngx_free_connection(c);
	c->fd = -1;
	if(close(fd))
	{
		ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocket::ngx_close_accepted_connection()中close(%d)失败!",fd);
	}
	return;
}*/