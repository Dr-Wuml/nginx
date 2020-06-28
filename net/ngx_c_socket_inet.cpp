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

typedef unsigned char u_char;

/**************************************************************************************************
  ��socket�󶨵ĵ�ַת��Ϊ�ı���ʽ�����ݲ���1��������Ϣ����ȡ��ַ�˿��ַ�������������ַ����ĳ��ȡ�
  ����sa���ͻ��˵�ip��ַ��Ϣһ�������
  ����port��Ϊ1�����ʾҪ�Ѷ˿���ϢҲ�ŵ���ϳɵ��ַ����Ϊ0���򲻰����˿���Ϣ
  ����text���ı�д������
  ����len���ı��Ŀ���������¼
***************************************************************************************************/
size_t CSocket::ngx_sock_ntop(struct sockaddr *sa,int port,u_char *text,size_t len)
{
	struct sockaddr_in            *sin;
	u_char                        *p;
	
	switch(sa->sa_family)
	{
	case AF_INET:
		sin = (struct sockaddr_in *) sa;
		p = (u_char *)&sin->sin_addr;
		if(port)
		{
			p = ngx_snprintf(text,len, "%ud.%ud.%ud.%ud:%d", p[0], p[1], p[2], p[3], ntohs(sin->sin_port));//���ص����µĿ�д��ַ
		}
		else
		{
			p = ngx_snprintf(text,len, "%ud.%ud.%ud.%ud", p[0], p[1], p[2], p[3]);
		}
		return (p - text);
		break;
	default:
		return 0;
		break;
	}
	return 0;
}