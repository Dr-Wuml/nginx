//�̳߳�ʵ��ģ��

#include <stdarg.h>
#include <unistd.h>

#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_memory.h"
#include "ngx_macro.h"

//��̬��Ա��ʼ��
pthread_mutex_t CThreadPool::m_pthreadMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t CThreadPool::m_pthreadCond   = PTHREAD_COND_INITIALIZER;
bool            CThreadPool::m_shutdown     = false;

//���캯��
CThreadPool::CThreadPool()
{
	m_iRunningThreadNum  = 0;                //�������е��߳�����Ϊ0
	m_iLastEmgTime       = 0;                //�ϴα����̲߳����õ�ʱ��
	//m_iPrintInfoTime    = 0;                //�ϴδ�ӡ�ο���Ϣ��ʱ��
	m_iRecvMsgQueueCount = 0;                //����Ϣ����
} 

CThreadPool::~CThreadPool()
{
	//��Դ�ͷ���StopAll()��
	clearMsgRecvQueue();
}
void CThreadPool::clearMsgRecvQueue()
{
	char * sTmpMempoint;
	CMemory *p_memory = CMemory::GetInstance();

	//β���׶Σ���Ҫ���⣿���˵Ķ��˳��ˣ���ֹͣ�Ķ�ֹͣ�ˣ�Ӧ�ò���Ҫ�˳���
	while(!m_MsgRecvQueue.empty())
	{
		sTmpMempoint = m_MsgRecvQueue.front();		
		m_MsgRecvQueue.pop_front(); 
		p_memory->FreeMemory(sTmpMempoint);
	}	
}

//�����̳߳أ������̴߳����ɹ�����true�����򷵻�false
bool CThreadPool::Create(int threadNum)
{
	ThreadItem *pNew;
	int err;
	
	m_iThreadNum = threadNum;
	for(int i = 0; i < m_iThreadNum; i++)
	{
		m_threadVector.push_back(pNew = new ThreadItem(this));            //����һ�����̶߳��󲢷����߳���������
		err = pthread_create(&pNew->_Handle, NULL, ThreadFunc, pNew);     //�����߳�
		if(err != 0)
		{
			//�����߳��д�
			ngx_log_stderr(err,"CThreadPool::Create()�����߳�%dʧ�ܣ����صĴ�����Ϊ%d!",i,err);
			return false;
		}
		//ngx_log_stderr(0,"CThreadPool::Create()�����߳�%d�ɹ�,�߳�id=%d",pNew->_Handle);
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

//�߳���ں���,pthread_create()�����̺߳�����̵��øú���
void *CThreadPool::ThreadFunc(void *threadData)
{
	//��̬��Ա������������thisָ��
	ThreadItem  *pThread        = static_cast<ThreadItem*>(threadData);
	CThreadPool *pThreadPoolObj = pThread->_pThis;
	
	CMemory *pMemory = CMemory::GetInstance();
	int err;
	
	pthread_t tid    = pthread_self();                 //��ȡ������̺�
	while(true)
	{
		err = pthread_mutex_lock(&m_pthreadMutex);
		if(err != 0)
		{
			ngx_log_stderr(err,"CThreadPool::ThreadFunc()pthread_mutex_lock()ʧ�ܣ����صĴ�����Ϊ%d!",err);
		}
		while( (pThreadPoolObj->m_MsgRecvQueue.size() == 0) && m_shutdown == false)
		{
			if(pThread->ifrunning == false)
			{
				pThread->ifrunning = true;
			}
			pthread_cond_wait(&m_pthreadCond, &m_pthreadMutex);
		}
		//�õ��������� ��Ϣ�����е�����   ���� m_shutdown == true

        /*
        jobbuf = g_socket.outMsgRecvQueue(); //����Ϣ������ȡ��Ϣ
        if( jobbuf == NULL && m_shutdown == false)
        {
            //��Ϣ����Ϊ�գ����Ҳ�Ҫ���˳�����
            //pthread_cond_wait()���������߳�ֱ��ָ�����������źţ�signaled����
                //�ú���Ӧ���ڻ���������ʱ���ã����ڵȴ�ʱ���Զ������������������ص㡿�����źű����ͣ��̱߳�����󣬻��������Զ������������߳̽���ʱ���ɳ���Ա���������������  
                  //˵���ˣ�ĳ���ط�������pthread_cond_signal(&m_pthreadCond);�����pthread_cond_wait�ͻ���������

            ngx_log_stderr(0,"--------------��������pthread_cond_wait,tid=%d--------------",tid);


            if(pThread->ifrunning == false)
                pThread->ifrunning = true; //���Ϊtrue�˲��������StopAll()�������з������Create()��StopAll()�����ŵ��ã��ͻᵼ���̻߳��ң�����ÿ���̱߳���ִ�е��������Ϊ�������ɹ��ˣ�

            err = pthread_cond_wait(&m_pthreadCond, &m_pthreadMutex);
            if(err != 0) ngx_log_stderr(err,"CThreadPool::ThreadFunc()pthread_cond_wait()ʧ�ܣ����صĴ�����Ϊ%d!",err);//�����⣬Ҫ��ʱ����



            ngx_log_stderr(0,"--------------����pthread_cond_wait���,tid=%d--------------",tid);
        }
        */
        //if(!m_shutdown)  //������������������ʾ�϶����õ���������Ϣ�����е����ݣ�Ҫȥ�ɻ��ˣ��ɻ���ʾ�������е��߳�����Ҫ����1��
        //    ++m_iRunningThreadNum; //��Ϊ�����ǻ���ģ��������+��OK�ģ�
        
        if(m_shutdown)
        {
        	pthread_mutex_unlock(&m_pthreadMutex);
        	break;
        }
        char    *jobbuf  = pThreadPoolObj->m_MsgRecvQueue.front();
        pThreadPoolObj->m_MsgRecvQueue.pop_front();
        --pThreadPoolObj->m_iRecvMsgQueueCount;                         //����Ϣ��������-1
        
        err = pthread_mutex_unlock(&m_pthreadMutex); 
        if(err != 0)
        {
        	ngx_log_stderr(err,"CThreadPool::ThreadFunc()pthread_cond_wait()ʧ�ܣ����صĴ�����Ϊ%d!",err);//�����⣬Ҫ��ʱ����
        }
        ++pThreadPoolObj->m_iRunningThreadNum;               //ԭ����+1,�ٶȱȻ�������
        g_socket.threadRecvProcFunc(jobbuf);         //������Ϣ������������Ϣ
        
        //ngx_log_stderr(0,"ִ�п�ʼ---begin,tid=%ui!",tid);
        //sleep(5);      //��ʱ���Դ���
        //ngx_log_stderr(0,"ִ�н���---end,tid=%ui!",tid);
        
        pMemory->FreeMemory(jobbuf);                         //�ͷ���Ϣ�ڴ�
        --pThreadPoolObj->m_iRunningThreadNum;               //ԭ��-1
	}
	return (void*)0;
}

//ֹͣ�����߳�
void CThreadPool::StopAll()
{
	//(1)�ж��Ƿ��Ѿ��ͷŹ���
	if(m_shutdown == true)
	{
		return;
	}
	m_shutdown = true;
	
	//(2)���ѵȴ�����
	int err = pthread_cond_broadcast(&m_pthreadCond);
	if(err != 0)
	{
		ngx_log_stderr(err,"CThreadPool::StopAll()��pthread_cond_broadcast()ʧ�ܣ����صĴ�����Ϊ%d!",err);
		return;
	}
	
	//(3)�ȵ��̣߳����߳��淵��
	std::vector<ThreadItem*>::iterator iter;
	for(iter = m_threadVector.begin(); iter != m_threadVector.end(); iter ++)
	{
		pthread_join((*iter)->_Handle,NULL);           //�ȴ�һ���߳���ֹ
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
	
	ngx_log_stderr(0,"CThreadPool::StopAll()�ɹ����أ��̳߳����߳�ȫ����������!");	
	return;
}

//�յ�������Ϣ����в������̴߳���
void CThreadPool::inMsgRecvQueueAndSignal(char *buf)
{
	//����
	int err = pthread_mutex_lock(&m_pthreadMutex);
	if(err != 0)
	{
		ngx_log_stderr(err,"CThreadPool::inMsgRecvQueueAndSignal()lock ʧ�ܣ�err = %d!",err);
	}
	m_MsgRecvQueue.push_back(buf);                     //����Ϣ����
	++m_iRecvMsgQueueCount;
	
	err = pthread_mutex_unlock(&m_pthreadMutex);
	if(err != 0)
	{
		ngx_log_stderr(err,"CThreadPool::inMsgRecvQueueAndSignal()UNlock ʧ�ܣ�err = %d!",err);
	}
	Call();
	return;
}

//������������һ�������̴߳���
void CThreadPool::Call()
{
	//ngx_log_stderr(0,"m_pthreadCondbegin--------------=%ui!",m_pthreadCond);  //����5�������ֲ�����
    //for(int i = 0; i <= 100; i++)
    //{
    int err = pthread_cond_signal(&m_pthreadCond); //����һ���ȴ����������̣߳�Ҳ���ǿ��Ի��ѿ���pthread_cond_wait()���߳�
    if(err != 0 )
    {
        //���������Ⱑ��Ҫ��ӡ��־��
        ngx_log_stderr(err,"CThreadPool::Call()��pthread_cond_signal()ʧ�ܣ����صĴ�����Ϊ%d!",err);
    }
    //}
    //������100�Σ����Դ�ӡ��m_pthreadCondֵ;
    //ngx_log_stderr(0,"m_pthreadCondend--------------=%ui!",m_pthreadCond);  //����1
    
    //(1)�����ǰ�Ĺ����߳�ȫ����æ����Ҫ����
    //bool ifallthreadbusy = false;
    if(m_iThreadNum == m_iRunningThreadNum) //�̳߳����߳�����������ǰ���ڸɻ���߳�����һ����˵�������̶߳�æµ�������̲߳�������
    {        
        //�̲߳�������
        //ifallthreadbusy = true;
        time_t currtime = time(NULL);
        if(currtime - m_iLastEmgTime > 10) //���ټ��10���Ӳű�һ���̳߳����̲߳����õ����⣻
        {
            //���α���֮��ļ�����볬��10�룬��Ȼ���һֱ���ֵ�ǰ�����߳�ȫæ����Ƶ��������־Ҳ������
            m_iLastEmgTime = currtime;  //����ʱ��
            //д��־��֪ͨ���ֽ���������û����û�Ҫ���������̳߳����߳�������
            ngx_log_stderr(0,"CThreadPool::Call()�з����̳߳��е�ǰ�����߳�����Ϊ0��Ҫ���������̳߳���!");
        }
    } //end if 
    
    /*
    //-------------------------------------------------------�������ݶ���һЩ���Դ��룻
    //���Ѷ�ʧ��--------------------------------------------------------------------------
    //(2)���������У�ֻ��һ���̣߳����̣߳��е�����Call�����Բ����ڶ���̵߳���Call�����Ρ�
    if(ifallthreadbusy == false)
    {
        //�п����߳�  ����û�п������������   pthread_cond_signal()������Ϊĳ��ʱ���߳�����ȫæ�������±��ε��� pthread_cond_signal()��û�м���ĳ���̵߳�pthread_cond_wait()ִ���أ�
           //����Ϊ���ֿ����Բ��ų������ ���Ѷ�ʧ�����������������⣬��������ֲ���
        if(irmqc > 5) //������������ֱ������5��
        {
            //����п����̣߳����� ������Ϣ�����г���5����Ϣû�б����������ܸо���������� ���Ѷ�ʧ
            //��������涪ʧ�����Ƿ�������໽��һ�Σ��Գ����𽥲����ض�ʧ�Ļ��ѣ��˷��Ƿ���У����в���֪���Ҵ�ӡһ����־����ʵ������ϸ��ͬ����������涪ʧ��Ҳ����ν����ΪThreadFunc()��һֱ����ֱ��������Ϣ����Ϊ�ա�
            ngx_log_stderr(0,"CThreadPool::Call()�ио��л��Ѷ�ʧ������irmqc = %d!",irmqc);

            int err = pthread_cond_signal(&m_pthreadCond); //����һ���ȴ����������̣߳�Ҳ���ǿ��Ի��ѿ���pthread_cond_wait()���߳�
            if(err != 0 )
            {
                //���������Ⱑ��Ҫ��ӡ��־��
                ngx_log_stderr(err,"CThreadPool::Call()��pthread_cond_signal 2()ʧ�ܣ����صĴ�����Ϊ%d!",err);
            }
        }
    }  //end if

    //(3)׼����ӡһЩ�ο���Ϣ��10���ӡһ�Ρ�,��Ȼ���д���������������²���
    m_iCurrTime = time(NULL);
    if(m_iCurrTime - m_iPrintInfoTime > 10)
    {
        m_iPrintInfoTime = m_iCurrTime;
        int irunn = m_iRunningThreadNum;
        ngx_log_stderr(0,"��Ϣ����ǰ��Ϣ�����е���Ϣ��Ϊ%d,�����̳߳����߳�����Ϊ%d,�������е��߳�����Ϊ = %d!",irmqc,m_iThreadNum,irunn); //������Ϣ����������Ϊ 1��X��0
    }
    */
	return;
}
