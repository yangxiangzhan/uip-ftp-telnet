
#ifndef _ros_Semaphore_h_
#define _ros_Semaphore_h_


//------------------���ź���ʵ��--------------------
typedef struct{
	volatile int signal;
	struct protothread * wait;
}ros_semaphore_t;

// ��ʼ��һ���ź���
#define task_semaphore_init(sph)  do{(sph)->signal = 0;(sph)->wait = NULL;}while(0)

// �ͷ�һ���ź���������һ���ж�.�� post һ���¼���������Ӧ
#define task_semaphore_release(sph)\
do{\
	(sph)->signal = 1;  \
	if((sph)->wait)     \
		OS_task_post((sph)->wait);\
}while(0)

// �ȴ�һ����ֵ�ź���
#define task_semaphore_wait(sph)\
do{\
	(sph)->wait = task ;          \
	task_cond_wait((sph)->signal);\
	task_semaphore_init(sph);     \
}while(0)
	

#endif


