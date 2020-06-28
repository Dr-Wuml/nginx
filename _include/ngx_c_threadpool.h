#ifndef __NGX_C_THREADPOOL_H__
#define __NGX_C_THREADPOOL_H__

#include <vector>
#include <pthread.h>
#include <atomic>       //ԭ�Ӳ���ͷ�ļ�

//�̳߳���
class CThreadPool
{
public:
	CThreadPool();
	
	~CThreadPool();

public:
	bool Create(int threadnum);                    //�����̳߳��е������߳�
	void StopAll();                                //�ͷ��̳߳��е������߳�
	
	void inMsgRecvQueueAndSignal(char *buf);       //����Ϣ���У����Ҵ����̴߳���
	void Call();                          //������ʱ������һ�������߳�ִ������
	
	
private:
	static void *ThreadFunc(void *ThreadData);     //�̻߳ص�����
	
	void clearMsgRecvQueue();                       //���������Ϣ����


private:
	//�̳߳����̵߳Ľṹ
	struct ThreadItem
	{
		pthread_t             _Handle;             //�߳̾��
		CThreadPool           *_pThis;             //��¼�̳߳ص�ָ��
		bool                  ifrunning;           //��Ǹ��߳��Ƿ��Ѿ�����
		
		//���캯��
		ThreadItem(CThreadPool *pthis):_pThis(pthis),ifrunning(false){}
		//��������
		~ThreadItem(){}
	};

private:
	static pthread_mutex_t    m_pthreadMutex;      //�߳�ͬ����/������
	static pthread_cond_t     m_pthreadCond;       //�߳�ͬ����������
	static bool               m_shutdown;          //�߳��˳���־��false ���˳���true �˳�
	
	int                       m_iThreadNum;        //Ҫ�������߳�����
	//int                       m_iRunningThreadNum; //�����е��߳���	
	std::atomic<int>          m_iRunningThreadNum; //�����е��߳���������ԭ�Ӳ���
	time_t                    m_iLastEmgTime;      //ǰһ�η����̲߳����õ�ʱ��
	//time_t                    m_iPrintInfoTime;    //��ӡ��Ϣ��һ��ʱ����
	//time_t                    m_CurrTime;          //��ǰʱ��
	
	std::vector<ThreadItem *> m_threadVector;                    //�߳�����
	std::list<char *>              m_MsgRecvQueue;               //������Ϣ����	
	int                            m_iRecvMsgQueueCount;         //����Ϣ���еĴ�С
}; 



#endif