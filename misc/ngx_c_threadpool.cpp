//线程池实现模块

#include <stdarg.h>
#include <unistd.h>

#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_memory.h"
#include "ngx_macro.h"

//静态成员初始化
pthread_mutex_t CThreadPool::m_pthreadMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t CThreadPool::m_pthreadCond   = PTHREAD_COND_INITIALIZER;
bool            CThreadPool::m_shutdown     = false;

//构造函数
CThreadPool::CThreadPool()
{
	m_iRunningThreadNum  = 0;                //正在运行的线程数量为0
	m_iLastEmgTime       = 0;                //上次报告线程不够用的时间
	//m_iPrintInfoTime    = 0;                //上次打印参考信息的时间
	m_iRecvMsgQueueCount = 0;                //收消息队列
} 

CThreadPool::~CThreadPool()
{
	//资源释放在StopAll()中
	clearMsgRecvQueue();
}
void CThreadPool::clearMsgRecvQueue()
{
	char * sTmpMempoint;
	CMemory *p_memory = CMemory::GetInstance();

	//尾声阶段，需要互斥？该退的都退出了，该停止的都停止了，应该不需要退出了
	while(!m_MsgRecvQueue.empty())
	{
		sTmpMempoint = m_MsgRecvQueue.front();		
		m_MsgRecvQueue.pop_front(); 
		p_memory->FreeMemory(sTmpMempoint);
	}	
}

//创建线程池，所有线程创建成功返回true，否则返回false
bool CThreadPool::Create(int threadNum)
{
	ThreadItem *pNew;
	int err;
	
	m_iThreadNum = threadNum;
	for(int i = 0; i < m_iThreadNum; i++)
	{
		m_threadVector.push_back(pNew = new ThreadItem(this));            //创建一个新线程对象并放在线程容器里面
		err = pthread_create(&pNew->_Handle, NULL, ThreadFunc, pNew);     //创建线程
		if(err != 0)
		{
			//创建线程有错
			ngx_log_stderr(err,"CThreadPool::Create()创建线程%d失败，返回的错误码为%d!",i,err);
			return false;
		}
		//ngx_log_stderr(0,"CThreadPool::Create()创建线程%d成功,线程id=%d",pNew->_Handle);
	}
	std::vector<ThreadItem*>::iterator iter;
lblfor:
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
	{
		if((*iter)->ifrunning == false)
		{
			usleep(100 * 1000);
			goto lblfor;
		}
	}
	return true;
}

//线程入口函数,pthread_create()创建线程后会立刻调用该函数
void *CThreadPool::ThreadFunc(void *threadData)
{
	//静态成员函数，不存在this指针
	ThreadItem  *pThread        = static_cast<ThreadItem*>(threadData);
	CThreadPool *pThreadPoolObj = pThread->_pThis;
	
	CMemory *pMemory = CMemory::GetInstance();
	int err;
	
	pthread_t tid    = pthread_self();                 //获取自身进程号
	while(true)
	{
		err = pthread_mutex_lock(&m_pthreadMutex);
		if(err != 0)
		{
			ngx_log_stderr(err,"CThreadPool::ThreadFunc()pthread_mutex_lock()失败，返回的错误码为%d!",err);
		}
		while( (pThreadPoolObj->m_MsgRecvQueue.size() == 0) && m_shutdown == false)
		{
			if(pThread->ifrunning == false)
			{
				pThread->ifrunning = true;
			}
			pthread_cond_wait(&m_pthreadCond, &m_pthreadMutex);
		}
		//拿到了真正的 消息队列中的数据   或者 m_shutdown == true

        /*
        jobbuf = g_socket.outMsgRecvQueue(); //从消息队列中取消息
        if( jobbuf == NULL && m_shutdown == false)
        {
            //消息队列为空，并且不要求退出，则
            //pthread_cond_wait()阻塞调用线程直到指定的条件有信号（signaled）。
                //该函数应该在互斥量锁定时调用，当在等待时会自动解锁互斥量【这是重点】。在信号被发送，线程被激活后，互斥量会自动被锁定，当线程结束时，由程序员负责解锁互斥量。  
                  //说白了，某个地方调用了pthread_cond_signal(&m_pthreadCond);，这个pthread_cond_wait就会走下来；

            ngx_log_stderr(0,"--------------即将调用pthread_cond_wait,tid=%d--------------",tid);


            if(pThread->ifrunning == false)
                pThread->ifrunning = true; //标记为true了才允许调用StopAll()：测试中发现如果Create()和StopAll()紧挨着调用，就会导致线程混乱，所以每个线程必须执行到这里，才认为是启动成功了；

            err = pthread_cond_wait(&m_pthreadCond, &m_pthreadMutex);
            if(err != 0) ngx_log_stderr(err,"CThreadPool::ThreadFunc()pthread_cond_wait()失败，返回的错误码为%d!",err);//有问题，要及时报告



            ngx_log_stderr(0,"--------------调用pthread_cond_wait完毕,tid=%d--------------",tid);
        }
        */
        //if(!m_shutdown)  //如果这个条件成立，表示肯定是拿到了真正消息队列中的数据，要去干活了，干活，则表示正在运行的线程数量要增加1；
        //    ++m_iRunningThreadNum; //因为这里是互斥的，所以这个+是OK的；
        
        if(m_shutdown)
        {
        	pthread_mutex_unlock(&m_pthreadMutex);
        	break;
        }
        char    *jobbuf  = pThreadPoolObj->m_MsgRecvQueue.front();
        pThreadPoolObj->m_MsgRecvQueue.pop_front();
        --pThreadPoolObj->m_iRecvMsgQueueCount;                         //收消息队列数字-1
        
        err = pthread_mutex_unlock(&m_pthreadMutex); 
        if(err != 0)
        {
        	ngx_log_stderr(err,"CThreadPool::ThreadFunc()pthread_cond_wait()失败，返回的错误码为%d!",err);//有问题，要及时报告
        }
        ++pThreadPoolObj->m_iRunningThreadNum;               //原子量+1,速度比互斥量快
        g_socket.threadRecvProcFunc(jobbuf);         //处理消息队列中来的消息
        
        //ngx_log_stderr(0,"执行开始---begin,tid=%ui!",tid);
        //sleep(5);      //临时测试代码
        //ngx_log_stderr(0,"执行结束---end,tid=%ui!",tid);
        
        pMemory->FreeMemory(jobbuf);                         //释放消息内存
        --pThreadPoolObj->m_iRunningThreadNum;               //原子-1
	}
	return (void*)0;
}

//停止所有线程
void CThreadPool::StopAll()
{
	//(1)判断是否已经释放过了
	if(m_shutdown == true)
	{
		return;
	}
	m_shutdown = true;
	
	//(2)唤醒等待条件
	int err = pthread_cond_broadcast(&m_pthreadCond);
	if(err != 0)
	{
		ngx_log_stderr(err,"CThreadPool::StopAll()中pthread_cond_broadcast()失败，返回的错误码为%d!",err);
		return;
	}
	
	//(3)等等线程，让线程真返回
	std::vector<ThreadItem*>::iterator iter;
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter ++)
	{
		pthread_join((*iter)->_Handle,NULL);           //等待一个线程终止
	}
	
	pthread_mutex_destroy(&m_pthreadMutex);
	pthread_cond_destroy(&m_pthreadCond);
	
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter ++)
	{
		if(*iter)
		{
			delete *iter;
		}
	}
	m_threadVector.clear();
	
	ngx_log_stderr(0,"CThreadPool::StopAll()成功返回，线程池中线程全部正常结束!");	
	return;
}

//收到完整消息入队列并触发线程处理
void CThreadPool::inMsgRecvQueueAndSignal(char *buf)
{
	//互斥
	int err = pthread_mutex_lock(&m_pthreadMutex);
	if(err != 0)
	{
		ngx_log_stderr(err,"CThreadPool::inMsgRecvQueueAndSignal()lock 失败！err = %d!",err);
	}
	m_MsgRecvQueue.push_back(buf);                     //入消息队列
	++m_iRecvMsgQueueCount;
	
	err = pthread_mutex_unlock(&m_pthreadMutex);
	if(err != 0)
	{
		ngx_log_stderr(err,"CThreadPool::inMsgRecvQueueAndSignal()UNlock 失败！err = %d!",err);
	}
	Call();
	return;
}

//任务到来，调用一个空闲线程处理
void CThreadPool::Call()
{
	//ngx_log_stderr(0,"m_pthreadCondbegin--------------=%ui!",m_pthreadCond);  //数字5，此数字不靠谱
    //for(int i = 0; i <= 100; i++)
    //{
    int err = pthread_cond_signal(&m_pthreadCond); //唤醒一个等待该条件的线程，也就是可以唤醒卡在pthread_cond_wait()的线程
    if(err != 0 )
    {
        //这是有问题啊，要打印日志啊
        ngx_log_stderr(err,"CThreadPool::Call()中pthread_cond_signal()失败，返回的错误码为%d!",err);
    }
    //}
    //唤醒完100次，试试打印下m_pthreadCond值;
    //ngx_log_stderr(0,"m_pthreadCondend--------------=%ui!",m_pthreadCond);  //数字1
    
    //(1)如果当前的工作线程全部都忙，则要报警
    //bool ifallthreadbusy = false;
    if(m_iThreadNum == m_iRunningThreadNum) //线程池中线程总量，跟当前正在干活的线程数量一样，说明所有线程都忙碌起来，线程不够用了
    {        
        //线程不够用了
        //ifallthreadbusy = true;
        time_t currtime = time(NULL);
        if(currtime - m_iLastEmgTime > 10) //最少间隔10秒钟才报一次线程池中线程不够用的问题；
        {
            //两次报告之间的间隔必须超过10秒，不然如果一直出现当前工作线程全忙，但频繁报告日志也够烦的
            m_iLastEmgTime = currtime;  //更新时间
            //写日志，通知这种紧急情况给用户，用户要考虑增加线程池中线程数量了
            ngx_log_stderr(0,"CThreadPool::Call()中发现线程池中当前空闲线程数量为0，要考虑扩容线程池了!");
        }
    } //end if 
    
    /*
    //-------------------------------------------------------如下内容都是一些测试代码；
    //唤醒丢失？--------------------------------------------------------------------------
    //(2)整个工程中，只在一个线程（主线程）中调用了Call，所以不存在多个线程调用Call的情形。
    if(ifallthreadbusy == false)
    {
        //有空闲线程  ，有没有可能我这里调用   pthread_cond_signal()，但因为某个时刻线程曾经全忙过，导致本次调用 pthread_cond_signal()并没有激发某个线程的pthread_cond_wait()执行呢？
           //我认为这种可能性不排除，这叫 唤醒丢失。如果真出现这种问题，我们如何弥补？
        if(irmqc > 5) //我随便来个数字比如给个5吧
        {
            //如果有空闲线程，并且 接收消息队列中超过5条信息没有被处理，则我总感觉可能真的是 唤醒丢失
            //唤醒如果真丢失，我是否考虑这里多唤醒一次？以尝试逐渐补偿回丢失的唤醒？此法是否可行，我尚不可知，我打印一条日志【其实后来仔细相同：唤醒如果真丢失，也无所谓，因为ThreadFunc()会一直处理直到整个消息队列为空】
            ngx_log_stderr(0,"CThreadPool::Call()中感觉有唤醒丢失发生，irmqc = %d!",irmqc);

            int err = pthread_cond_signal(&m_pthreadCond); //唤醒一个等待该条件的线程，也就是可以唤醒卡在pthread_cond_wait()的线程
            if(err != 0 )
            {
                //这是有问题啊，要打印日志啊
                ngx_log_stderr(err,"CThreadPool::Call()中pthread_cond_signal 2()失败，返回的错误码为%d!",err);
            }
        }
    }  //end if

    //(3)准备打印一些参考信息【10秒打印一次】,当然是有触发本函数的情况下才行
    m_iCurrTime = time(NULL);
    if(m_iCurrTime - m_iPrintInfoTime > 10)
    {
        m_iPrintInfoTime = m_iCurrTime;
        int irunn = m_iRunningThreadNum;
        ngx_log_stderr(0,"信息：当前消息队列中的消息数为%d,整个线程池中线程数量为%d,正在运行的线程数量为 = %d!",irmqc,m_iThreadNum,irunn); //正常消息，三个数字为 1，X，0
    }
    */
	return;
}
