#ifndef __pseudo_mutex_h__
#define __pseudo_mutex_h__


//------------------�򵥻�����ʵ��--------------------
typedef struct{
	volatile struct protothread * lock;
}ros_mutex_t; 


// ��ʼ��һ�� mutex 
#define task_mutex_init(mx)       (mx)->lock = NULL

// ���� mutex ��ʧ�������ֱ�������ɹ� 
#define task_mutex_lock(mx)       task_cond_while((mx)->lock);(mx)->lock = task

// ���� mutex ,���ܽ�����һ�������ϵ���
#define task_mutex_unlock(mx)     if ((mx)->lock == task)  (mx)->lock = NULL

#define task_mutex_is_locked(mx)  ((mx)->lock)
	

#endif
