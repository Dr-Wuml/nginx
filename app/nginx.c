#include<stdio.h>
#include<unistd.h>

#include "ngx_func.h"
#include "ngx_signal.h"


int main(int argc ,char *argv)
{
	printf("ͨѶ���nginx��ʼλ��");
	myconf();
	mysignal();




	printf("�����˳��ټ�!");
	return 0;
}