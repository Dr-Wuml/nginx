#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>

#include "ngx_global.h"

void ngx_init_setproctitle()
{
    
    //无需判断penvmen
    gp_envmem = new char[g_envneedmem];
    memset(gp_envmem,0,g_envneedmem);

    char *ptmp = gp_envmem;
    for(int i=0;environ[i];i++)
    {
        size_t size = strlen(environ[i]) + 1; //对应环境变量i所在的内存块
        strcpy(ptmp,environ[i]);  //原环境变量的内容拷贝到新的内存
        environ[i] = ptmp;  //新的环境变量指向新的内存
        ptmp += size; //指向下一个老的环境变量起始位置
    }
    return;
}

//设置标题
void ngx_setproctitle(const char *title)
{
    size_t ititlelen = strlen(title); //获取标题长度
    /*size_t e_envirolen = 0 ;
    for(int i=0; g_os_argv[i];i++) //获取参数长度
    {
        e_envirolen += strlen(g_os_argv[i]);
    }*/

    size_t esy = g_argvneedmem + g_envneedmem ; //合并参数长度与环境变量长度

    if(esy < ititlelen) //标题长度不能超出预留有内存大长度
    {
        return;
    }

    g_os_argv[1] = NULL; //设置后续命令行参数为空

    //设置标题
    char *ptmp = g_os_argv[0];
    strcpy(ptmp,title);
    ptmp += ititlelen; //跳过标题

    //清空剩余的内存
    size_t cha = esy - ititlelen; 
    memset(ptmp,0,cha);
    return;
}
