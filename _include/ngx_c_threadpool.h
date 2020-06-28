#ifndef __NGX_C_THREADPOOL_H__
#define __NGX_C_THREADPOOL_H__

#include <vector>
#include <pthread.h>
#include <atomic>       //原子操作头文件

//线程池类
class CThreadPool
{
public:
	CThreadPool();
	
	~CThreadPool();

public:
	bool Create(int threadnum);                    //创建线程池中的所有线程
	void StopAll();                                //释放线程池中的所有线程
	
	void inMsgRecvQueueAndSignal(char *buf);       //入消息队列，并且触发线程处理
	void Call();                          //任务来时，调用一个空闲线程执行任务
	
	
private:
	static void *ThreadFunc(void *ThreadData);     //线程回调函数
	
	void clearMsgRecvQueue();                       //清理接收消息队列


private:
	//线程池中线程的结构
	struct ThreadItem
	{
		pthread_t             _Handle;             //线程句柄
		CThreadPool           *_pThis;             //记录线程池的指针
		bool                  ifrunning;           //标记该线程是否已经工作
		
		//构造函数
		ThreadItem(CThreadPool *pthis):_pThis(pthis),ifrunning(false){}
		//析构函数
		~ThreadItem(){}
	};

private:
	static pthread_mutex_t    m_pthreadMutex;      //线程同步锁/互斥量
	static pthread_cond_t     m_pthreadCond;       //线程同步条件变量
	static bool               m_shutdown;          //线程退出标志，false 不退出，true 退出
	
	int                       m_iThreadNum;        //要创建的线程数量
	//int                       m_iRunningThreadNum; //运行中的线程数	
	std::atomic<int>          m_iRunningThreadNum; //运行中的线程数，改用原子操作
	time_t                    m_iLastEmgTime;      //前一次发生线程不够用的时间
	//time_t                    m_iPrintInfoTime;    //打印信息的一个时间间隔
	//time_t                    m_CurrTime;          //当前时间
	
	std::vector<ThreadItem *> m_threadVector;                    //线程容器
	std::list<char *>              m_MsgRecvQueue;               //接收消息队列	
	int                            m_iRecvMsgQueueCount;         //收消息队列的大小
}; 



#endif