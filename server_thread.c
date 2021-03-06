#include "request.h"
#include "server_thread.h"
#include "common.h"
#include <pthread.h>

struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
	/* add any other parameters you need */
};

/*************************** thread queue********************************/

typedef struct sorted_points {
	int key; // connection socket
	struct sorted_points *next;
}linked_list; 

int counter;
linked_list *ready_queue;	// request_queue
/********************** thread package***********************************/
/* Mutex to guard condition -- used in getting/returning from thread pool*/ 
pthread_mutex_t conditionMutex = PTHREAD_MUTEX_INITIALIZER;
/* Condition variable -- used to wait for avaiable threads, and signal when available */ 
pthread_cond_t  Notempty;
pthread_cond_t  Notfull;

int buffer_max;

void *thread_pool(void *sv);

/***********************hash table(cache)***************************************/
int  cache_current_size =0;
int  cache_active = 0;
int max_cache =0;
typedef struct sorted_point {
	int size; // connection socket
	char *name;
	struct sorted_point *next;
} lst_list;

lst_list *LST_queue;

struct hash_node {
	//char *word;
	struct file_data *data;
	struct hash_node *next;
	int flag;
};

struct hash_node **cache;
int hash_func(int size, const char *key);
int cache_insert( struct file_data *data);
struct hash_node *cache_lookup(char *file_name);
int cache_evict(int file_size);
lst_list * add_lst (lst_list *head, struct file_data *data);
lst_list* delete_head_lst(lst_list *head);
/************************************************************************/

int hash_func(int size, const char *key) {
	int length = strlen(key);
	int i = 0;
	int hash = 5381;
	for (i = 0; i < length; i++)
		hash = hash * 33 + key[i];
	hash = hash % size; // m is the hash table size
	if(hash < 0)
		hash = 0-hash;
	return hash;
}

struct hash_node *cache_lookup(char *file_name){

	int hash = hash_func(8192,file_name);
	struct hash_node *curr = cache[hash];
	
	if(curr->flag ==0){	
		return NULL;
	}	
	while(curr){
		if(curr->flag == 1){
			if(strcmp(curr->data->file_name,file_name)==0){
				return curr;
			}
			if(curr->next==NULL)	
				return NULL;
			curr=curr->next;
		}
		else if(curr->flag == 2){	// it has been deleted
			if(curr->next == NULL)
				return NULL;
			curr= curr->next;
		}
	}
return NULL;
}

int cache_insert( struct file_data *data)
{
	int hash = hash_func(8192,data->file_name);
	struct hash_node *curr;
	curr = cache[hash];
	if(curr->flag ==0){
		cache_current_size=cache_current_size+data->file_size;
		curr->data->file_name = Malloc(MAXLINE);
		strncpy(curr->data->file_name, data->file_name,MAXLINE);
		curr->data->file_buf = Malloc(data->file_size);
		strncpy(curr->data->file_buf, data->file_buf,data->file_size);
		curr->data->file_size = data->file_size;
		curr->flag =1;
		curr->next=NULL;

	}

	else{
	while(curr){
		if(curr->flag == 1){
			if(strcmp(curr->data->file_name,data->file_name)==0){
				return 0; // do nothing
			}
			if(curr->next==NULL)	
				break;
			curr=curr->next;
		}
		else if(curr->flag == 2){	// it has been deleted
			if(curr->next == NULL)
				break;
			curr= curr->next;
		}

	}	
		cache_current_size=cache_current_size+data->file_size;
		struct hash_node* temp = (struct hash_node *)malloc(sizeof(struct hash_node));
		temp->data = malloc(sizeof(struct file_data));
		temp->data->file_name = Malloc(MAXLINE);
		strncpy(temp->data->file_name,data->file_name,MAXLINE);
		temp->data->file_buf = Malloc(data->file_size);
		strncpy(temp->data->file_buf, data->file_buf,data->file_size);
		temp->data->file_size = data->file_size;
		temp->flag =1;
		temp->next = NULL;
		curr->next =temp;
	}

	LST_queue = add_lst(LST_queue,data);

	return 0;
}

 lst_list * add_lst (lst_list *head, struct file_data *data){

	if(head ==NULL){

		head =Malloc(sizeof(struct sorted_point));
		head->name = Malloc(MAXLINE);
		strncpy(head->name, data->file_name,MAXLINE);
		head->size = data->file_size;
		head->next =NULL;
		return head;
	}

	lst_list *prev= head;
	lst_list *curr= head;
	lst_list *tmp =(malloc(sizeof(struct sorted_point)));
	tmp->name = Malloc(MAXLINE);
	strncpy(tmp->name, data->file_name,MAXLINE);
	tmp->size = data->file_size;
	if(head->size<data->file_size){		// the largest
		tmp->next =head;
		head = tmp;
		return head;
	}
	while(curr->size>=data->file_size){
		prev =curr;
		curr= curr->next;
		if(curr==NULL)
			break;
	}
	prev->next =tmp;
	tmp->next = curr;
	return head;
}

int cache_evict(int file_size){
	while((max_cache-cache_current_size) < file_size){
		cache_current_size =cache_current_size-LST_queue->size;
		int hash = hash_func(8192,LST_queue->name);
		cache[hash]->flag =2;
		LST_queue = delete_head_lst(LST_queue);
	}
	return 0;
}
lst_list* delete_head_lst(lst_list *head){
	if(head ==NULL){
		return NULL;
	}
		
	if(head->next ==NULL){
		free(head->name);
		free(head);
		return NULL;
	}
	lst_list *curr = head;
	head = curr->next;
	free(curr->name);
	free(curr);
	return head;
}

/************************************************************************/
linked_list *delete_head(linked_list*head);
linked_list *put_at_tail(linked_list*head, int key);

/************************************************************************/

/* static functions */

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;


	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data){	

	free(data->file_name);
	free(data->file_buf);
	free(data);
}

static void
do_server_request(struct server *sv, int connfd)
{
	int ret;
	struct request *rq;
	struct file_data *data;
	data = file_data_init();	
	rq = request_init(connfd, data);
	
	if (!rq) {
		file_data_free(data);
		return;
	}
	
	if(cache_active ==1){
		pthread_mutex_lock( &conditionMutex); 
		struct hash_node *cache_stored; 
		cache_stored= cache_lookup(data->file_name);
		
		if(cache_stored!=NULL){  // check if it populated in the cache{
			data->file_buf = Malloc(cache_stored->data->file_size);
			strncpy(data->file_buf, cache_stored->data->file_buf, cache_stored->data->file_size);
			data->file_size =cache_stored->data->file_size;
		}
		else{
		/* reads file, 
	 	* fills data->file_buf with the file contents,
	 	* data->file_size with file size. */
	 	pthread_mutex_unlock( &conditionMutex ); 
		ret = request_readfile(rq);
		pthread_mutex_lock( &conditionMutex ); 
		// need to renew the cache
			if(data->file_size<max_cache){
				if(data->file_size <=(max_cache-cache_current_size) ){
					cache_insert(data);
				}
				else{
					cache_evict(data->file_size);
					cache_insert(data);
				}
			}
		}
		pthread_mutex_unlock( &conditionMutex ); 


	}	
	else{
		ret = request_readfile(rq);
		if (!ret)
		goto out;
	}
	/* sends file to client */
	request_sendfile(rq);
out:
	request_destroy(rq);
	file_data_free(data);
}

/* entry point functions */

struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{
	struct server *sv;

	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests;
	sv->max_cache_size = max_cache_size;
	counter = 0;
	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {

		if(max_requests > 0){	// create queue
			ready_queue = (malloc(sizeof(struct sorted_points)*max_requests));	
			ready_queue =NULL;
		}
		else 
			return sv;
		if(nr_threads > 0){		// create threads here
			buffer_max = max_requests;	// the largest size declared here
			pthread_t * thread;
			thread=malloc(sizeof(pthread_t)*nr_threads);	// declare a nr_threads size thread array

			int i =0;
			for (i= 0; i<nr_threads;i++){
				pthread_create( &thread[i], NULL, thread_pool , (void *)sv); // assume it is correct lol
			}
		}
		if(max_cache_size>0){
			max_cache = max_cache_size;
			cache_active = 1;
			int i = 0;
			//LST_queue = (malloc(sizeof(struct sorted_point)));	
			LST_queue =NULL;
			cache = malloc( sizeof( struct hash_node * ) * 8192 ) ;
			cache_current_size = 0;
			for( i = 0; i < 8192; i++ ) {
				cache[i] = (struct hash_node *)malloc(sizeof(struct hash_node));
				cache[i]->data = Malloc(sizeof(struct file_data));
				cache[i]->data->file_name = NULL;
				cache[i]->data->file_buf = NULL;
				cache[i]->data->file_size = 0;
				cache[i]->flag = 0; 
				cache[i]->next = NULL;
			}

		}

	}

	return sv;
}

void
server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} else {
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */
		pthread_mutex_lock( &conditionMutex ); 
			while(counter>=buffer_max)
				pthread_cond_wait(&Notfull, &conditionMutex);
		if(ready_queue == NULL)
			pthread_cond_signal(&Notempty);
		ready_queue =put_at_tail(ready_queue, connfd);
		pthread_mutex_unlock(&conditionMutex);
	}
}

void *thread_pool(void *sv){
	while(1){
		pthread_mutex_lock( &conditionMutex ); 
		struct server *my_sv = (struct server*)sv;
		while(ready_queue == NULL)
			pthread_cond_wait(&Notempty,&conditionMutex); 
		int connfd  = ready_queue->key;
		
		if(counter>=buffer_max)
			pthread_cond_signal( &Notfull ); 
		ready_queue = delete_head(ready_queue); // delete it from the queue
		pthread_mutex_unlock( &conditionMutex ); 

		do_server_request(my_sv, connfd);
	}
	return NULL;
}

linked_list *delete_head(linked_list*head){
	if(head ==NULL){
		return NULL;
	}
		
	if(head->next ==NULL){
		free(head);
		counter = 0;
		return NULL;
	}

	linked_list *curr = head;
	head = curr->next;
	free(curr);
	counter --;
	return head;
}

linked_list *put_at_tail(linked_list*head, int key){
	linked_list* new_node = (linked_list*)malloc(sizeof(linked_list));
	new_node->key = key;
	new_node->next = NULL;
	if(head ==NULL){
		head = new_node;
		counter =1;
		return head;
	}

	linked_list *curr = head;
	linked_list *prev = head;
	int i=0;
	while(curr!=NULL){
		prev = curr;
		curr= curr->next;
		i++;
	}
	prev->next = new_node;
	new_node->next = NULL;
	counter++;
	return head;
}

