#include<stdio.h>
#include<unistd.h>

#include "ngx_func.h"
#include "ngx_signal.h"


int main(int argc ,char *argv)
{
	printf("通讯框架nginx开始位置");
	myconf();
	mysignal();




	printf("程序退出再见!");
	return 0;
}