#ifndef __NGX_FUNC_H__
#define __NGX_FUNC_H__

#include <stdarg.h>

typedef unsigned char   u_char;

//字符串截取函数
void Rtrim(char *str);
void Ltrim(char *str);

//设置标题的可执行函数
void   ngx_init_setproctitle();
void   ngx_setproctitle(const char *title);

//和日志打印有关的函数
void ngx_log_init();
void ngx_log_stderr(int err,const char *fmt,...);
void ngx_log_error_core(int level,int err,const char *fmt,...);

u_char *ngx_log_errno(u_char *buf,u_char *last,int err);
u_char *ngx_snprintf(u_char *buf,size_t max,const char *fmt,...);
u_char *ngx_slprintf(u_char *buf,u_char *last,const char *fmt,...);
u_char *ngx_vslprintf(u_char *buf,u_char *last,const char *fmt,va_list args);


//信号、进程有关的函数
int    ngx_init_signals();
void   ngx_master_process_cycle();
int    ngx_daemon();
void   ngx_process_events_and_timers();
#endif
