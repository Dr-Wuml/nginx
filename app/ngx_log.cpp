#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<stdint.h>
#include<stdarg.h>
#include<sys/time.h>
#include<time.h>
#include<fcntl.h>
#include<errno.h>

#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h"
#include "ngx_c_conf.h"

/*
    全局变量定义
    错误等级同macro.h中定义的一样
*/
static u_char err_levels[][20] =
{
    {"stderr"}, //0: 控制台错误
    {"emerg"},  //1: 紧急
    {"alert"},  //2: 警戒
    {"crit"},   //3: 严重
    {"error"},  //4: 错误
    {"warn"},   //5: 警告
    {"notice"}, //6: 注意
    {"info"},   //7: 信息
    {"debug"}   //8: 调试
};
ngx_log_t ngx_log;

/*--------------------------------------------------------------------------------------------------
    描述：通过可变参数组合出字符串【支持...省略号形参】，
    自动往字符串最末尾增加换行符【所以调用者不用加\n】，
     往标准错误上输出这个字符串；
     如果err不为0，表示有错误，会将该错误编号以及对应的错误信息一并放到组合出的字符串中一起显示；
--------------------------------------------------------------------------------------------------*/
void ngx_log_stderr(int err,const char *fmt,...)
{
    va_list args;
    u_char errstr[NGX_MAX_ERROR_STR+1];
    u_char *p,*last;
    memset(errstr,0,sizeof(errstr));

    last = errstr + NGX_MAX_ERROR_STR;//指向errstr内存的末尾

    p = ngx_cpymem(errstr,"nginx: ",7);
    va_start(args,fmt);
    p = ngx_vslprintf(p,last,fmt,args);
    va_end(args);

    if(err)
    {
        p = ngx_log_errno(p,last,err);
    }

    if(p >= (last - 1))
    {
        p= (last -1) -1;
    }

    *p++ = '\n'; //增加一个换行符

    write(STDERR_FILENO,errstr,p - errstr); //往标准错误输出信息，一般指向屏幕
    if(ngx_log.fd > STDERR_FILENO)
    {
        err = 0;
        p--;*p = 0;
        ngx_log_error_core(NGX_LOG_STDERR,err,(const char *)errstr);
    }
    return;
}

/*-------------------------------------------------------------------------------------------------
        描述：给一段内存，一个错误编号，组合出一个字符串，形如：   (错误编号: 错误原因)，放到给的这段内存中去
        这个函数与原始的nginx代码多有不同
        buf：是个内存，要往这里保存数据
        last：放的数据不要超过这里
        err：错误编号，这个错误编号对应的错误字符串，保存到buffer中
---------------------------------------------------------------------------------------------------*/
u_char *ngx_log_errno(u_char *buf,u_char *last,int err)
{
    char *perrorinfo = strerror(err);
    size_t len =strlen(perrorinfo);

    char leftstr[10] = {0};
    sprintf(leftstr," (%d: ",err);
    size_t leftlen = strlen(leftstr);

    char rightstr[] = ") ";
    size_t rightlen = strlen(rightstr);

    size_t extralen = leftlen + rightlen;

    if((buf + len + extralen) < last) //超出范围则放弃加入
    {
        buf = ngx_cpymem(buf,leftstr,leftlen);
        buf = ngx_cpymem(buf,perrorinfo,len);
        buf = ngx_cpymem(buf,rightstr,rightlen);
    }
    return buf;
}

/*--------------------------------------------------------------------------------------------------
    往日志文件中写日志，代码中有自动加换行符，所以调用时字符串不用刻意加\n；
    日过定向为标准错误，则直接往屏幕上写日志
    【比如日志文件打不开，则会直接定位到标准错误，此时日志就打印到屏幕上，参考ngx_log_init()】
    level:一个等级数字，我们把日志分成一些等级，以方便管理、显示、过滤等等，
    如果这个等级数字比配置文件中的等级数字"LogLevel"大，那么该条信息不被写到日志文件中
    err：是个错误代码，如果不是0，就应该转换成显示对应的错误信息,一起写到日志文件中，
    ngx_log_error_core(5,8,"这个XXX工作的有问题,显示的结果是=%s","YYYY")
-----------------------------------------------------------------------------------------------------*/
void ngx_log_error_core(int level,int err,const char *fmt,...)
{
    /* 创建保存错误信息内存并确定最大范围*/
    u_char *last;
    u_char errstr[NGX_MAX_ERROR_STR+1];
    memset(errstr, 0, sizeof(errstr));
    last = errstr + NGX_MAX_ERROR_STR;
    
    /*定义需要用的变量*/
    struct timeval tv;
    struct tm      tm;
    time_t         sec; //秒
    u_char         *p;  //指向当前数据拷贝的位置
    va_list        args;//可变参数的存储位置
    memset(&tv, 0, sizeof(struct timeval));
    memset(&tm, 0, sizeof(struct tm));

    gettimeofday(&tv, NULL); //获取当前时间，返回自1970-01-01 00:00:00到现在经历的秒数【第二个参数是时区,一般不用】
    sec = tv.tv_sec; //秒
    localtime_r(&sec, &tm);//把参数1的time_t转换为本地时间，保存到参数2中去，带_r的是线程安全的版本，尽量使用
    tm.tm_mon++;
    tm.tm_year += 1900;
    u_char strcurrtime[40] = {0}; //保存组合出的当前时间字符串，格式形如：2019/01/08 19:57:11
    ngx_slprintf(strcurrtime,
                 (u_char *) -1 ,  //若用一个u_char *接一个 (u_char *)-1,则 得到的结果是 0xffffffff....，值够大
                 "%4d/%02d/%02d %02d:%02d:%02d",
                 tm.tm_year, tm.tm_mon, tm.tm_mday, //年月日
                 tm.tm_hour, tm.tm_min, tm.tm_sec); //时分秒
    p = ngx_cpymem(errstr,strcurrtime,strlen((const char *)strcurrtime));//日期增加进来，得到形如：     2019/01/08 20:26:07
    p = ngx_slprintf(p, last, " [%s] ", err_levels[level]);                 //日志级别增加进来，得到形如：  2019/01/08 20:26:07 [crit] 
    p = ngx_slprintf(p, last, "%P: ", ngx_pid);                             //支持%P格式，进程id增加进来，得到形如：   2019/01/08 20:50:15 [crit] 2037:
    
    va_start(args, fmt);                   //使args指向起始的参数
    p = ngx_vslprintf(p, last, fmt, args); //把fmt和args参数弄进去，组合出来这个字符串
    va_end(args);                          //释放args

    if(err)
    {
        p = ngx_log_errno(p, last, err);//错误代码和错误信息也要显示出来
    }

    if(p > (last -1))
    {
        p =( last - 1 ) -1 ;
    }
    *p++ = '\n'; //增加个换行符 

    ssize_t n;
    while(1)
    {
        if(level > ngx_log.log_level) //大于打印日志的登记
        {
            break;
        }

        n = write(ngx_log.fd,errstr,p - errstr);

        if(n == -1)
        {
            if(errno == ENOSPC) //写失败，原因磁盘满了
            {
                //暂不做处理
            }
            else
            {
                if(ngx_log.fd != STDERR_FILENO)
                {
                    n = write(STDERR_FILENO,errstr,p - errstr);
                }
            }   
        }
        break;
    }//end while
    return ;
}



/*
       初始化日志
*/
void ngx_log_init()
{
    u_char *plogname = NULL;
    size_t nlen;
    CConfig *pConfig = CConfig::GetInstance();
    plogname = (u_char *) pConfig->Getstring("Log");
    if(plogname == NULL)
    {
        plogname = (u_char *) NGX_LOG_PATH ; //使用默认日志路径
    }
    ngx_log.log_level = pConfig->GetIntDefault("LogLevel",NGX_LOG_NOTICE);//缺省情况下日志级别为6

    //只写打开|追加到末尾|文件不存在则创建【这个需要跟第三参数指定文件访问权限】
    //mode = 0644：文件访问权限， 6: 110    , 4: 100：     【用户：读写， 用户所在组：读，其他：读】 
    ngx_log.fd = open((const char *)plogname,O_WRONLY|O_APPEND|O_CREAT,0644);
    if(ngx_log.fd == -1)//如果有错误，则直接定位到 标准错误上去 
    {
        ngx_log_stderr(errno,"[alert] could not open error log file: open() \"%s\" failed",plogname); 
        ngx_log.fd = STDERR_FILENO; //直接定位到标准位置上去
    }

    return;
}
