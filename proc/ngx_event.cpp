//�Ϳ����ӽ������

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>   //�ź����ͷ�ļ� 
#include <errno.h>    //errno

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"

//���������¼��Ͷ�ʱ���¼�����������nginx�������ͬ������
void ngx_process_events_and_timers()
{
	g_socket.ngx_epoll_process_events(-1); //-1��ʾ���ŵȴ���

    //...������
}