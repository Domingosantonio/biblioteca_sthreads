/* Simplethreads Instructional Thread Package
 * 
 * sthread_user.c - Implements the sthread API using user-level threads.
 *
 *    You need to implement the routines in this file.
 *
 * Change Log:
 * 2002-04-15        rick
 *   - Initial version.
 * 2005-10-12        jccc
 *   - Added semaphores, deleted conditional variables
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include <sthread.h>
#include <sthread_user.h>
#include <sthread_ctx.h>
#include <sthread_time_slice.h>
#include <sthread_user.h>
#include "queue.h"


struct _sthread {
  int prioridade_base;      // fixa na criação
  int prioridade_atual;    // muda se for prioridade dinâmica
  int quantum_base;       // normalmente = 5
  int quantum_restante;  // decrementado a cada tick
  int nice;             // de 0 a 10
  int prioridade_fixa; // 1 se prioridade 0–4

  sthread_ctx_t *saved_ctx;
  sthread_start_func_t start_routine_ptr;
  long wake_time;
  int join_tid;
  void* join_ret;
  void* args;
  int tid;          /* meramente informativo */
};


static queue_t *exe_thr_list;         /* lista de threads executaveis */
static queue_t *dead_thr_list;        /* lista de threads "mortas" */
static queue_t *sleep_thr_list;
static queue_t *join_thr_list;
static queue_t *zombie_thr_list;
static struct _sthread *active_thr;   /* thread activa */
static int tid_gen;                   /* gerador de tid's */

#define NUM_PRIORIDADES 15
#define CLOCK_TICK 100
static long Clock;

typedef struct {
  queue_t* filas[NUM_PRIORIDADES];
} runqueue_t;

static runqueue_t runqueue_ativa;
static runqueue_t runqueue_expirada;
/*********************************************************************/
/* Part 1: Creating and Scheduling Threads                           */
/*********************************************************************/
void trocar_epoca() {
  runqueue_t temp = runqueue_ativa;
  runqueue_ativa = runqueue_expirada;
  runqueue_expirada = temp;
}

struct _sthread* escolher_proxima_tarefa() {
  for (int i = 0; i < NUM_PRIORIDADES; i++) {
    if (!queue_is_empty(runqueue_ativa.filas[i])) {
      return queue_remove(runqueue_ativa.filas[i]);
    }
  }
  return NULL;
}

void update_task_priority(struct _sthread* t) {
  if (!t->prioridade_fixa) {
    int novo_quantum = t->quantum_base + (t->quantum_restante / 2);
    int nova_prioridade = t->prioridade_base - t->quantum_restante + t->nice;

    if (nova_prioridade < 5) nova_prioridade = 5;
    if (nova_prioridade > 14) nova_prioridade = 14;

    t->quantum_restante = novo_quantum;
    t->prioridade_atual = nova_prioridade;
  }
}

void sthread_user_free(struct _sthread *thread);

void sthread_aux_start(void){
  splx(LOW);
  active_thr->start_routine_ptr(active_thr->args);
  sthread_user_exit((void*)0);
}

void sthread_user_dispatcher(void);

void sthread_user_init(void) {
  sleep_thr_list = create_queue();
  join_thr_list = create_queue();
  zombie_thr_list = create_queue();
  tid_gen = 1;

  for (int i = 0; i < NUM_PRIORIDADES; i++) {
      runqueue_ativa.filas[i] = create_queue();
      runqueue_expirada.filas[i] = create_queue();
  }

  struct _sthread *main_thread = malloc(sizeof(struct _sthread));
  main_thread->start_routine_ptr = NULL;
  main_thread->args = NULL;
  main_thread->saved_ctx = sthread_new_blank_ctx();
  main_thread->wake_time = 0;
  main_thread->join_tid = 0;
  main_thread->join_ret = NULL;
  main_thread->tid = tid_gen++;
  
  active_thr = main_thread;

  Clock = 1;
  sthread_time_slices_init(sthread_user_dispatcher, CLOCK_TICK);
}


sthread_t sthread_user_create(sthread_start_func_t start_routine, void *arg,int priority)
{
  struct _sthread *new_thread = (struct _sthread*)malloc(sizeof(struct _sthread));
  sthread_ctx_start_func_t func = sthread_aux_start;
  new_thread->args = arg;
  new_thread->start_routine_ptr = start_routine;
  new_thread->wake_time = 0;
  new_thread->join_tid = 0;
  new_thread->join_ret = NULL;
  new_thread->saved_ctx = sthread_new_ctx(func);

  new_thread->prioridade_base = priority;
  new_thread->prioridade_atual = priority;
  new_thread->quantum_base = 5;
  new_thread->quantum_restante = 5;
  new_thread->nice = 0;
  new_thread->prioridade_fixa = (priority < 5);
  
  splx(HIGH);
  new_thread->tid = tid_gen++;
  queue_insert(runqueue_ativa.filas[priority], new_thread);
  splx(LOW);
  return new_thread;
}

int sthread_nice(int nice){
   active_thr->nice=nice;
   if(!active_thr->prioridade_fixa){
      int nova_prioridade=active_thr->prioridade_base - active_thr->quantum_restante+nice;
      if(nova_prioridade<5) nova_prioridade=5;
      if(nova_prioridade>14) nova_prioridade=14;

      return nova_prioridade;
   }
   return active_thr->prioridade_atual;
}

void sthread_dump() {
  printf("=== dump start ===\n");
  printf("active thread\nid: %d\npriority: %d\nquantum: %d\n",
         active_thr->tid, active_thr->prioridade_atual, active_thr->quantum_restante);

  printf("active runqueue\n");
  for (int i = 0; i < NUM_PRIORIDADES; i++) {
    printf("[%d] ", i);
    queue_element_t* el = runqueue_ativa.filas[i]->first;
    while (el != NULL) {
      printf("%d,%d ", el->thread->tid, el->thread->quantum_restante);
      el = el->next;
    }
    printf("\n");
  }

  printf("expired runqueue\n");
  for (int i = 0; i < NUM_PRIORIDADES; i++) {
    printf("[%d] ", i);
    queue_element_t* el = runqueue_expirada.filas[i]->first;
    while (el != NULL) {
      printf("%d,%d ", el->thread->tid, el->thread->quantum_restante);
      el = el->next;
    }
    printf("\n");
  }
  /*
  printf("blocked list\n");
  queue_element_t* el = sleep_thr_list->first;
  while (el != NULL) {
    printf("%d,%d ", el->thread->tid, el->thread->quantum_restante);
    el = el->next;
  }
  */
  printf("\n=== dump end ===\n");
}

void sthread_user_exit(void *ret) {
    splx(HIGH);
    
    int is_zombie = 1;

    // 1. Tratar threads em espera no join
    queue_t *tmp_queue = create_queue();   
    while (!queue_is_empty(join_thr_list)) {
        struct _sthread *thread = queue_remove(join_thr_list);
        
        if (thread->join_tid == active_thr->tid) {
            thread->join_ret = ret;
            // Reinsere na runqueue ativa com sua prioridade atual
            queue_insert(runqueue_ativa.filas[thread->prioridade_atual], thread);
            is_zombie = 0;
        } else {
            queue_insert(tmp_queue, thread);
        }
    }
    delete_queue(join_thr_list);
    join_thr_list = tmp_queue;

    // 2. Tratar thread terminada
    if (is_zombie) {
        queue_insert(zombie_thr_list, active_thr);
    } else {
        // Se não é zombie, podemos liberar imediatamente
        sthread_user_free(active_thr);
    }

    // 3. Escolher próxima tarefa
    struct _sthread *old_thr = active_thr;
    active_thr = escolher_proxima_tarefa();

    // 4. Se não há mais tarefas, terminar
    if (active_thr == NULL) {
        printf("No more threads to execute!\n");
        exit(0);
    }

    // 5. Trocar de contexto
    sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
    
    splx(LOW);
}

/*
void sthread_user_exit(void *ret) {
  splx(HIGH);
   
   int is_zombie = 1;

   // unblock threads waiting in the join list
   queue_t *tmp_queue = create_queue();   
   while (!queue_is_empty(join_thr_list)) {
      struct _sthread *thread = queue_remove(join_thr_list);
     
      printf("Test join list: join_tid=%d, active->tid=%d\n", thread->join_tid, active_thr->tid);
      if (thread->join_tid == active_thr->tid) {
         thread->join_ret = ret;
         queue_insert(runqueue_ativa.filas[active_thr->prioridade_atual],thread);
         is_zombie = 0;
      } else {
         queue_insert(tmp_queue,thread);
      }
   }
   delete_queue(join_thr_list);
   join_thr_list = tmp_queue;
 
   if (is_zombie) {
      queue_insert(zombie_thr_list, active_thr);
   } else {
      queue_insert(dead_thr_list, active_thr);
   }
   

   if(queue_is_empty(exe_thr_list)){  
    free(exe_thr_list);              /
    delete_queue(dead_thr_list);
    sthread_user_free(active_thr);
    printf("Exec queue is empty!\n");
    exit(0);
  }

  
   // remove from exec list
   struct _sthread *old_thr = active_thr;
   active_thr = queue_remove(exe_thr_list);
   sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);

   splx(LOW);
}

void sthread_user_dispatcher(void)
{
   Clock++;
  
   queue_t *tmp_queue = create_queue();   

   while (!queue_is_empty(sleep_thr_list)) {
      struct _sthread *thread = queue_remove(sleep_thr_list);
      
      if (thread->wake_time == Clock) {
         thread->wake_time = 0;
         queue_insert(exe_thr_list,thread);
      } else {
         queue_insert(tmp_queue,thread);
      }
   }
   delete_queue(sleep_thr_list);
   sleep_thr_list = tmp_queue;
   
   sthread_user_yield();
}

void sthread_user_yield(void)
{
  splx(HIGH);
  struct _sthread *old_thr;
  old_thr = active_thr;
  queue_insert(exe_thr_list, old_thr);
  active_thr = queue_remove(exe_thr_list);
  sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
  splx(LOW);
}
*/

void sthread_user_dispatcher(void) {
    Clock++;
    
    // 1. Verificar se precisa trocar de época (runqueue ativa vazia)
    int runqueue_ativa_vazia = 1;
    for (int i = 0; i < NUM_PRIORIDADES; i++) {
        if (!queue_is_empty(runqueue_ativa.filas[i])) {
            runqueue_ativa_vazia = 0;
            break;
        }
    }
    
    if (runqueue_ativa_vazia) {
        trocar_epoca();
    }
    
    // 2. Tratar tarefas que estavam dormindo (sleep_thr_list)
    queue_t *tmp_queue = create_queue();   
    while (!queue_is_empty(sleep_thr_list)) {
        struct _sthread *thread = queue_remove(sleep_thr_list);
        
        if (thread->wake_time <= Clock) {
            thread->wake_time = 0;
            // Inserir na runqueue ativa com prioridade atual
            queue_insert(runqueue_ativa.filas[thread->prioridade_atual], thread);
            
            // Verificar se precisa preemptar (se a nova tarefa tem maior prioridade)
            if (thread->prioridade_atual < active_thr->prioridade_atual) {
                // Reinserir a tarefa atual na runqueue
                queue_insert(runqueue_ativa.filas[active_thr->prioridade_atual], active_thr);
                // Tornar a nova tarefa ativa
                active_thr = thread;
                sthread_switch(active_thr->saved_ctx, thread->saved_ctx);
            }
        } else {
            queue_insert(tmp_queue, thread);
        }
    }
    delete_queue(sleep_thr_list);
    sleep_thr_list = tmp_queue;
    
    // 3. Chamar yield para permitir troca de tarefas
    sthread_user_yield();
}

void sthread_user_yield(void) {
    splx(HIGH);
    
    struct _sthread *old_thr = active_thr;
    
    // Se a tarefa ainda tem quantum restante
    if (old_thr->quantum_restante > 0) {
        old_thr->quantum_restante--;
        
        // Se ainda tem quantum, reinserir na runqueue ativa
        if (old_thr->quantum_restante > 0) {
            queue_insert(runqueue_ativa.filas[old_thr->prioridade_atual], old_thr);
        } 
        // Quantum esgotado
        else {
            // Para tarefas dinâmicas, calcular nova prioridade e quantum
            if (!old_thr->prioridade_fixa) {
                update_task_priority(old_thr);
            }
            // Mover para runqueue expirada
            queue_insert(runqueue_expirada.filas[old_thr->prioridade_atual], old_thr);
        }
    }
    
    // Escolher próxima tarefa
    active_thr = escolher_proxima_tarefa();
    
    // Se não há tarefas, verificar se pode trocar de época
    if (active_thr == NULL) {
        trocar_epoca();
        active_thr = escolher_proxima_tarefa();
        
        // Se ainda não há tarefas, terminar
        if (active_thr == NULL) {
            printf("No more threads to execute!\n");
            exit(0);
        }
    }
    
    // Realizar a troca de contexto
    sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
    
    splx(LOW);
}

void sthread_user_free(struct _sthread *thread)
{
  sthread_free_ctx(thread->saved_ctx);
  free(thread);
}


/*********************************************************************/
/* Part 2: Join and Sleep Primitives                                 */
/*********************************************************************/

int sthread_user_join(sthread_t thread, void **value_ptr)
{
   /* suspends execution of the calling thread until the target thread
      terminates, unless the target thread has already terminated.
      On return from a successful pthread_join() call with a non-NULL 
      value_ptr argument, the value passed to pthread_exit() by the 
      terminating thread is made available in the location referenced 
      by value_ptr. When a pthread_join() returns successfully, the 
      target thread has been terminated. The results of multiple 
      simultaneous calls to pthread_join() specifying the same target 
      thread are undefined. If the thread calling pthread_join() is 
      canceled, then the target thread will not be detached. 

      If successful, the pthread_join() function returns zero. 
      Otherwise, an error number is returned to indicate the error. */

   
   splx(HIGH);
   // checks if the thread to wait is zombie
   int found = 0;
   queue_t *tmp_queue = create_queue();
   while (!queue_is_empty(zombie_thr_list)) {
      struct _sthread *zthread = queue_remove(zombie_thr_list);
      if (thread->tid == zthread->tid) {
         *value_ptr = thread->join_ret;
         queue_insert(dead_thr_list,thread);
         found = 1;
      } else {
         queue_insert(tmp_queue,zthread);
      }
   }
   delete_queue(zombie_thr_list);
   zombie_thr_list = tmp_queue;
  
   if (found) {
       splx(LOW);
       return 0;
   }

   
   // search active queue
   if (active_thr->tid == thread->tid) {
      found = 1;
   }
   
   queue_element_t *qe = NULL;

   // search exe
   qe = exe_thr_list->first;
   while (!found && qe != NULL) {
      if (qe->thread->tid == thread->tid) {
         printf("Found in exe: tid=%d\n", thread->tid);
         found = 1;
      }
      qe = qe->next;
   }

   // search sleep
   qe = sleep_thr_list->first;
   while (!found && qe != NULL) {
      if (qe->thread->tid == thread->tid) {
         found = 1;
      }
      qe = qe->next;
   }

   // search join
   qe = join_thr_list->first;
   while (!found && qe != NULL) {
      if (qe->thread->tid == thread->tid) {
         found = 1;
      }
      qe = qe->next;
   }

   // if found blocks until thread ends
   if (!found) {
      splx(LOW);
      return -1;
   } else {
      active_thr->join_tid = thread->tid;
      
      struct _sthread *old_thr = active_thr;
      queue_insert(join_thr_list, old_thr);
      active_thr = queue_remove(exe_thr_list);
      printf ("Active is 0:%d\n", (active_thr == NULL));
      printf ("Old is 0:%d\n", (old_thr == NULL));
      sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
  
      *value_ptr = thread->join_ret;
   }
   
   splx(LOW);
   return 0;
}


int sthread_user_sleep(int time)
{
   splx(HIGH);
   
   long num_ticks = 10 * time / CLOCK_TICK;
   if (num_ticks == 0) {
      splx(LOW);
      
      return 0;
   }
   
   active_thr->wake_time = Clock + num_ticks;

   queue_insert(sleep_thr_list,active_thr); 
   sthread_t old_thr = active_thr;
   active_thr = queue_remove(exe_thr_list);
   sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
   
   splx(LOW);
   return 0;
}

/* --------------------------------------------------------------------------*
 * Synchronization Primitives                                                *
 * ------------------------------------------------------------------------- */

/*
 * Mutex implementation
 */

struct _sthread_mutex
{
  lock_t l;
  struct _sthread *thr;
  queue_t* queue;
};

sthread_mutex_t sthread_user_mutex_init()
{
  sthread_mutex_t lock;

  if(!(lock = malloc(sizeof(struct _sthread_mutex)))){
    printf("Error in creating mutex\n");
    return 0;
  }

  /* mutex initialization */
  lock->l=0;
  lock->thr = NULL;
  lock->queue = create_queue();
  
  return lock;
}

void sthread_user_mutex_free(sthread_mutex_t lock)
{
  delete_queue(lock->queue);
  free(lock);
}

void sthread_user_mutex_lock(sthread_mutex_t lock)
{
  while(atomic_test_and_set(&(lock->l))) {}

  if(lock->thr == NULL){
    lock->thr = active_thr;

    atomic_clear(&(lock->l));
  } else {
    queue_insert(lock->queue, active_thr);
    
    atomic_clear(&(lock->l));

    splx(HIGH);
    struct _sthread *old_thr;
    old_thr = active_thr;
    //queue_insert(exe_thr_list, old_thr);
    active_thr = queue_remove(exe_thr_list);
    sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);

    splx(LOW);
  }
}

void sthread_user_mutex_unlock(sthread_mutex_t lock)
{
  if(lock->thr!=active_thr){
    printf("unlock without lock!\n");
    return;
  }

  while(atomic_test_and_set(&(lock->l))) {}

  if(queue_is_empty(lock->queue)){
    lock->thr = NULL;
  } else {
    lock->thr = queue_remove(lock->queue);
    queue_insert(exe_thr_list, lock->thr);
  }

  atomic_clear(&(lock->l));
}

/*
 * Monitor implementation
 */

struct _sthread_mon {
 	sthread_mutex_t mutex;
	queue_t* queue;
};

sthread_mon_t sthread_user_monitor_init()
{
  sthread_mon_t mon;
  if(!(mon = malloc(sizeof(struct _sthread_mon)))){
    printf("Error creating monitor\n");
    return 0;
  }

  mon->mutex = sthread_user_mutex_init();
  mon->queue = create_queue();
  return mon;
}

void sthread_user_monitor_free(sthread_mon_t mon)
{
  sthread_user_mutex_free(mon->mutex);
  delete_queue(mon->queue);
  free(mon);
}

void sthread_user_monitor_enter(sthread_mon_t mon)
{
  sthread_user_mutex_lock(mon->mutex);
}

void sthread_user_monitor_exit(sthread_mon_t mon)
{
  sthread_user_mutex_unlock(mon->mutex);
}

void sthread_user_monitor_wait(sthread_mon_t mon)
{
  struct _sthread *temp;

  if(mon->mutex->thr != active_thr){
    printf("monitor wait called outside monitor\n");
    return;
  }

  /* inserts thread in queue of blocked threads */
  temp = active_thr;
  queue_insert(mon->queue, temp);

  /* exits mutual exclusion region */
  sthread_user_mutex_unlock(mon->mutex);

	splx(HIGH);
	struct _sthread *old_thr;
	old_thr = active_thr;
	//queue_insert(exe_thr_list, old_thr);
	active_thr = queue_remove(exe_thr_list);
	sthread_switch(old_thr->saved_ctx, active_thr->saved_ctx);
	splx(LOW);
}

void sthread_user_monitor_signal(sthread_mon_t mon)
{
  struct _sthread *temp;

  if(mon->mutex->thr != active_thr){
    printf("monitor signal called outside monitor\n");
    return;
  }

  while(atomic_test_and_set(&(mon->mutex->l))) {}
  if(!queue_is_empty(mon->queue)){
    /* changes blocking queue for thread */
    temp = queue_remove(mon->queue);
    queue_insert(mon->mutex->queue, temp);
  }
  atomic_clear(&(mon->mutex->l));
}




/* The following functions are dummies to 
 * highlight the fact that pthreads do not
 * include monitors.
 */

sthread_mon_t sthread_dummy_monitor_init()
{
   printf("WARNING: pthreads do not include monitors!\n");
   return NULL;
}


void sthread_dummy_monitor_free(sthread_mon_t mon)
{
   printf("WARNING: pthreads do not include monitors!\n");
}


void sthread_dummy_monitor_enter(sthread_mon_t mon)
{
   printf("WARNING: pthreads do not include monitors!\n");
}


void sthread_dummy_monitor_exit(sthread_mon_t mon)
{
   printf("WARNING: pthreads do not include monitors!\n");
}


void sthread_dummy_monitor_wait(sthread_mon_t mon)
{
   printf("WARNING: pthreads do not include monitors!\n");
}


void sthread_dummy_monitor_signal(sthread_mon_t mon)
{
   printf("WARNING: pthreads do not include monitors!\n");
}



