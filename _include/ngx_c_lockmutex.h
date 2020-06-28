#ifndef __NGX_C_LOCALMUTEX__
#define __NGX_C_LOCALMUTEX__

#include<pthread.h>


//»¥³âËøÀà
class CLock
{
public:
	CLock(pthread_mutex_t *pMutex)
	{
		m_pMutex = pMutex;
		pthread_mutex_lock(m_pMutex);    //»¥³âÁ¿¼ÓËø
	}
	~CLock()
	{
		pthread_mutex_unlock(m_pMutex);    //½âËø»¥³âÁ¿
	}
private:
	pthread_mutex_t *m_pMutex;            //»¥³âÁ¿
};



#endif