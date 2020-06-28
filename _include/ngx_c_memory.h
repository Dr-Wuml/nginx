#ifndef __NGX_C_MEMORY_H__
#define __NGX_C_MEMORY_H__

#include <stddef.h>        
//内存相关类
class CMemory
{
private:
	CMemory() {}
public:
	~CMemory(){}
private:
	static CMemory *m_instance;
public:
	static CMemory* GetInstance()
	{
		if(m_instance == NULL)
		{
			//锁
			if(m_instance == NULL)
			{
				m_instance = new CMemory();//第一次调用不应该放在线程中，应该放在主进程中，以免和其他线程调用冲突从而导致同时执行两次new CMemory()
				static CGarhuishou cl;
			}
			//解锁
		}
		return m_instance;
	}
	
	class CGarhuishou
	{
	public:
		~CGarhuishou()
		{
			if(CMemory::m_instance)
			{
				delete CMemory::m_instance;//这个释放是整个系统退出的时候，系统来调用释放内存
				CMemory::m_instance = NULL;
			}
		}
	};
public:
	void *AllocMemory(int memCount,bool ifmemset);
	void FreeMemory(void *point);
};
#endif
