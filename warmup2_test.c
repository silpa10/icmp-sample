#include <stdio.h>
#include "pthread.h"
#include "my402list.h"
#include "unistd.h"
#include <sys/time.h>
#include "stdlib.h"
#include <string.h>
#include "signal.h"
#include <math.h>

#define SECTOMSEC 1000000 // second to microsecond
#define THOUSAND 1000
#define N 3 // # of threads


/*File Pointer */
FILE *fp = NULL;
/* Thread ID */
pthread_t thrd[N];
double emulation_ends = 0.0;
/* Default parameters */
/* Interarrival time */
float lamda= 0.5;
/* Service rate */
float mu = 0.35;
/* Token filling rate */
float r = 1.5;
/* Maximum token bucket size */
int B = 10;
/* Packet required token size */
int P = 3;
/* # of packers to serve */
int num = 20;
/* file address */
const char *filename= NULL;

double total_ms = 0;


int tokens_drop = 0;
int  total_tokens_prod =0;
int packets_drop = 0;
int total_packets_prod =0;
double total_i_a_time = 0;
double total_ser_time = 0;
double total_sys_time = 0;
double time_in_Q1 =0, time_in_Q2 =0;
int packet_arrived = 0;

/* Thread ID array */
extern pthread_t thrd[N];

/* Initialization */
extern My402List Q1_node;
extern My402List Q2_node;
extern pthread_mutex_t lock ;
extern pthread_cond_t serverQ ;

/* structure for pointer */
typedef struct{
    float int_time ; /* arrival time */
    int tokens;     /* tokens required */
    float ser_time; /* service time */
    int name_ID;    /* Id */
    double Q1_time_enters;
    double total_Q1_time;
    double Q2_time_enters;
    double total_Q2_time;
    double S_time_enters;
    double total_S_time;
}Packet_desc;

/* Printing functions */
void PrintParameters();
void PrintStatistics();

/*Time Utility functions*/
void SaveCurTimeOfDay();
double GetMTimeOfDay();

/* Read from file */
void ReadFile(Packet_desc *p);

/* Processing fuctions */
void EnquePacket(Packet_desc *p);
void ProcessPacket();
void StartThreads();
void DequePacket(Packet_desc *p,My402ListElem *first_elem);

/*Thread functions */
void *Arrival(void*);
void *Token(void*);
void *Service(void*);
void CreateThreads();

/*Remove packerts from Q1 and Q1 after interrupt is called */
void RemoveAllPackets(My402List *queue , int queue_no);

/* Signal handler */
void handler(int signo);

/*Check validity of command line arguments */
int checkValidity(const char *str, int *ifnegative);

/* Read input from command line arguments */
void ReadCommandArguments(int argc, const char *argv[]);


/* Initialize the mutex lock and condition wait */
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t serverQ = PTHREAD_COND_INITIALIZER;

/* To calculate Variance */
double sys_variance = 0;

double GetMTimeOfDay(){
    int errno = -1;
    double cur_time =0.0;
    double s=0.0;
    double micro_s =0;
    
    struct timeval mytime;
    
    if((errno = gettimeofday(&mytime, NULL))){
        fprintf(stderr, "PrintTimeOfDay %s",strerror(errno));
    }
    
    micro_s = (double)mytime.tv_usec/THOUSAND;
    s=(double)mytime.tv_sec*THOUSAND;
    
    cur_time = s + micro_s - total_ms;
    
    return cur_time;
    
}
/* Enque the packet in Q1 when arrived */
void EnquePacket(Packet_desc *p){
    //   int j = i+1;
    p->Q1_time_enters = GetMTimeOfDay(); //lavish
    
    //Q1_time_enters[p->name_ID] = GetMTimeOfDay();
    fprintf(stdout, "%012.3lfms: p%d enters Q1\n",GetMTimeOfDay(), p->name_ID);
    
    My402ListAppend(&Q1_node, (void*)p);
    
}



/* Process the pakcets in Q1 and transfer to Q2 when enough tokens */
void ProcessPacket(){
    My402ListElem *find = NULL;
    Packet_desc *p = NULL;
    
    find=My402ListFirst(&Q1_node);
    
    if(find->obj == NULL){
        fprintf(stderr,"Error: Obj is NULL");
        pthread_exit(0);
    }
    
    p = (Packet_desc*)find->obj;
    
    
    if(p->tokens <= token_limit){
        // move this to Q2
        
        token_limit -= p->tokens;
        
        // total time in Q1
        
        p->total_Q1_time = GetMTimeOfDay()- p->Q1_time_enters;
        
        
        fprintf(stdout, "%012.3lfms: p%d leaves Q1, time in Q1 = %.3lfms, tokens bucket now has %d token\n",GetMTimeOfDay(),p->name_ID,p->total_Q1_time,token_limit);
        
        
        if(My402ListEmpty(&Q2_node)){
            // signal the condition about the queue not empty
            p->Q2_time_enters = GetMTimeOfDay();
            // Q2_time_enters[p->name_ID] = GetMTimeOfDay();
            fprintf(stdout, "%012.3lfms: p%d enters Q2\n",GetMTimeOfDay(),p->name_ID);
            
            My402ListAppend(&Q2_node, (void*)p);
            curr_q2_size = 1;
            /* signal the condition variable queue*/
            pthread_cond_signal(&serverQ);
            find = My402ListFind(&Q1_node, (void*)p);
            My402ListUnlink(&Q1_node, find);
            
        }else{
            p->Q2_time_enters = GetMTimeOfDay();
            
            //   Q2_time_enters[p->name_ID] = GetMTimeOfDay();
            
            fprintf(stdout, "%012.3lfms: p%d enters Q2\n",GetMTimeOfDay(),p->name_ID);
            
            My402ListAppend(&Q2_node, (void*)p);
            /* gaurd set true */
            curr_q2_size = 1;
            find = My402ListFind(&Q1_node, (void*)p);
            My402ListUnlink(&Q1_node, find);
            
        }
        
    }
}

/* Arrival thread */

void *Arrival(void* arg){
    
    
    int i =0;
    double prev_itime = 0 ;
    double inter_a_time = 0.0;
    Packet_desc *p = NULL;
    
    //Init the list
    My402ListInit(&Q1_node);
    
    
    //create packet
    while(i != num) {
        
        p = (Packet_desc*)malloc(sizeof(Packet_desc));
        
        if(fp != NULL){
            ReadFile(p);
        }
        else{
            p->int_time = 1/lamda*THOUSAND ; // interarrival time
            p->tokens = P;
            p->ser_time = 1/mu*THOUSAND; // service time
        }
        p->name_ID = ++i;
        
        
        usleep(p->int_time*THOUSAND);
        
        inter_a_time = GetMTimeOfDay()- prev_itime;
        
        prev_itime = GetMTimeOfDay() ;
        
        total_i_a_time += inter_a_time;
        
        pthread_mutex_lock(&lock);
        
        if(p->tokens > B){
            
            fprintf(stdout, "%012.3lfms: p%d arrives, needs %d tokens, inter-arrival time = %.3fms, dropped\n",GetMTimeOfDay(),p->name_ID,p->tokens, inter_a_time);
            packets_drop++;
            total_packets_prod++;
            free(p);
            
            pthread_mutex_unlock(&lock);
            
            continue;
        }
        
        fprintf(stdout, "%012.3lfms: p%d arrives, needs %d tokens, inter-arrival time = %.3fms\n",GetMTimeOfDay() ,p->name_ID,p->tokens,inter_a_time);
        
        total_packets_prod++;
        
        pthread_mutex_unlock(&lock);
        
        pthread_mutex_lock(&lock);
        
        packet_arrived++;
        
        //critical section
        
        EnquePacket(p);
        
        ProcessPacket();
        
        pthread_mutex_unlock(&lock);
    }
    
    if(filename){
        fclose(fp);
    }
    
    return 0;
}

/* Token bucket thread */

void *Token(void* arg){
    int j=0;
    useconds_t wait_time = 0;
    
    if(r < 0.1)
        r = 0.1;
    
    wait_time = (1/r)*SECTOMSEC;
    
    
    while( 1 ) {
        
        pthread_mutex_lock(&lock);
        
        if( total_packets_prod == num && My402ListEmpty(&Q1_node)){
            curr_q2_size = 1;
            pthread_cond_signal(&serverQ);
            pthread_mutex_unlock(&lock);
            break;
        }
        
        pthread_mutex_unlock(&lock);
        
        usleep(wait_time);
        
        pthread_mutex_lock(&lock);
        
        if( total_packets_prod == num && My402ListEmpty(&Q1_node)){
            pthread_mutex_unlock(&lock);
            continue;
        }
        pthread_mutex_unlock(&lock);
        
        pthread_mutex_lock(&lock);
        if(token_limit < B){
            j++;
            total_tokens_prod = j;
            token_limit++;
            fprintf(stdout, "%012.3lfms: token t%d arrives, token bucket now has %d token\n",GetMTimeOfDay(),j,token_limit);
        }else{
            j++;
            total_tokens_prod = j;
            tokens_drop++;
            fprintf(stdout, "%012.3lfms: token t%d arrives, dropped\n",GetMTimeOfDay(), j );
        }
        //check if it can move first packet from Q1 to Q2
        if(!My402ListEmpty(&Q1_node)){
            ProcessPacket();
            
        }
        pthread_mutex_unlock(&lock);
        
    }
    
    return 0;
}



void DequePacket(Packet_desc *p,My402ListElem *first_elem){
    
    p->total_Q2_time = GetMTimeOfDay()- p->Q2_time_enters;
    
    fprintf(stdout, "%012.3lfms: p%d leaves Q2, time in Q2 = %.3lfms, begin service at S\n",GetMTimeOfDay(), p->name_ID,p->total_Q2_time);
    
    My402ListUnlink(&Q2_node, first_elem);
    
    if(My402ListEmpty(&Q2_node))
        curr_q2_size = 0;
    
}

void *Service(void* arg){
    My402ListInit(&Q2_node);
    double time_in_system = 0.0;
    
    while(1){
        My402ListElem *first_elem = NULL ;
        Packet_desc *p = NULL;
        
        
        if(total_packets_prod == num && My402ListEmpty(&Q2_node) && My402ListEmpty(&Q1_node)){
        	pthread_cond_broadcast(&serverQ);
        	 break;
    
		}
           
        
        pthread_mutex_lock(&lock);
        
        while(curr_q2_size == 0 && !interrupt_called){
            pthread_cond_wait(&serverQ, &lock);
        }
        if((total_packets_prod == num && My402ListEmpty(&Q2_node) && My402ListEmpty(&Q1_node)) || interrupt_called){
            pthread_exit(0);
        }
        
        first_elem = My402ListFirst(&Q2_node);
        
        if(first_elem->obj == NULL){
            fprintf(stderr,"Error: Obj is NULL");
            pthread_exit(0);
        }
        
        p = (Packet_desc*)first_elem->obj;
        
        
        DequePacket(p,first_elem);
        p->S_time_enters = GetMTimeOfDay();
        pthread_mutex_unlock(&lock);
        
        
        usleep(p->ser_time*THOUSAND);
        
        pthread_mutex_lock(&lock);
        
        packet_served++;
        
        p->total_S_time = GetMTimeOfDay()- p->S_time_enters;
        time_in_system = GetMTimeOfDay()- p->Q1_time_enters;
        sys_variance += time_in_system * time_in_system ;
        
        /* Calculate total time for the packets served */
        total_ser_time += p->total_S_time;
        total_sys_time += time_in_system;
        time_in_Q1 += p->total_Q1_time;
        time_in_Q2 += p->total_Q2_time;
        
        
        fprintf(stdout, "%012.3lfms: p%d departs from S, service time = %.3fms, time in system = %.3fms\n",GetMTimeOfDay(), p->name_ID,p->total_S_time,time_in_system);
        
        free(p);
        
        pthread_mutex_unlock(&lock);
        
    }
    
    return 0;
}

void ReadFile(Packet_desc *p){
    
    fscanf(fp,"%f %d %f",&p->int_time,&p->tokens,&p->ser_time);
    
}


void PrintParameters(){
    
    
    fprintf(stdout, "\nEmulation Parameters:\n");
    
    if(fp == NULL){
        fprintf(stdout, "\tnumber to arrive = %d\n",num);
        
        fprintf(stdout, "\tlambda = %.2f\n",lamda);
        
        fprintf(stdout, "\tmu = %.2f\n",mu);
        fprintf(stdout, "\tr = %.2f\n",r);
        fprintf(stdout, "\tB = %d\n",B);
        fprintf(stdout, "\tP = %d\n",P);
        fprintf(stdout, "\n");
    }else{
        fprintf(stdout, "\tnumber to arrive = %d\n",num);
        fprintf(stdout, "\tr = %.2f\n",r);
        fprintf(stdout, "\tB = %d\n",B);
        
        fprintf(stdout, "\ttsfile = %s\n\n",filename);
    }
    
}

void SaveCurTimeOfDay(){
    int errno = -1;
    double s=0.0;
    float micro_s =0;
    struct timeval mytime;
    if((errno = gettimeofday(&mytime, NULL))){
        fprintf(stderr, "PrintTimeOfDay %s",strerror(errno));
    }
    
    micro_s = (float)mytime.tv_usec/THOUSAND;
    s=(double)mytime.tv_sec*THOUSAND;
    
    total_ms = s + micro_s;
    
}

/* check whether command line is valid or not */

int checkValidity(const char *str, int *ifnegative){
    
    if(str == NULL){
        return 0;
    }
    
    if(*str++ == '-'){
        if(!*ifnegative)
            *ifnegative = 1;
    }
    
    while (*str != '\0') {
        if((*str >= '0' && *str <= '9') || *str == '.')
            str++;
        else
            return 0;
    }
    
    return 1;
}


/*          Read Command Arguments          */

void ReadCommandArguments(int argc, const char *argv[]){
    
    int i=0;
    int ifnegative = 0;
    struct stat statbuf;
    
    
    for (i=1; i<argc; i++) {
        
        if(strcmp(argv[i],"-lambda")==0){
            if(checkValidity(argv[i+1],&ifnegative)){
                sscanf(argv[i+1], "%f", &lamda);
                if(lamda < 0.1)
                    lamda = 0.1;
            }else{
                fprintf(stderr, "Invalid Commandline\n");
                exit(1);
            }
            continue;
        }
        if(strcmp(argv[i],"-mu")==0){
            if(checkValidity(argv[i+1],&ifnegative)){
                sscanf(argv[i+1], "%f", &mu);
                if(mu < 0.1)
                    mu = 0.1;
            }else{
                fprintf(stderr, "Invalid Commandline\n");
                exit(1);
            }
            
            continue;
            
        }
        if(strcmp(argv[i],"-r")==0){
            if(checkValidity(argv[i+1],&ifnegative)){
                sscanf(argv[i+1], "%f", &r);
                if(r < 0.1)
                    r = 0.1;
            }else{
                fprintf(stderr, "Invalid Commandline\n");
                exit(1);
            }
            continue;
        }
        
        if(strcmp(argv[i],"-B")==0){
            if(checkValidity(argv[i+1],&ifnegative)){
                B = atoi(argv[i+1]);
            }else{
                fprintf(stderr, "Invalid Commandline\n");
                exit(1);
            }
            continue;
        }
        
        if(strcmp(argv[i],"-P")==0){
            if(checkValidity(argv[i+1],&ifnegative)){
                P = atoi(argv[i+1]);
            }else{
                fprintf(stderr, "Invalid Commandline\n");
                exit(1);
            }
            continue;
        }
        if(strcmp(argv[i],"-n")==0){
            if(checkValidity(argv[i+1],&ifnegative)){
                    num = atoi(argv[i+1]);
                /* Check whether # is greater than the integer limit */
                    if( strlen(argv[i+1])>10 || (num < 0 && strlen(argv[i+1]) == 10))
                        ifnegative = 1;
            }else{
                fprintf(stderr, "Invalid Commandline\n");
                exit(1);
            }
            continue;
        }
        if(strcmp(argv[i],"-t")==0){
            filename = argv[i+1];
            continue;
        }
        
    }
    if(filename){
        fp = fopen(filename, "r");
        stat(filename, &statbuf);
        
        if(S_ISDIR(statbuf.st_mode)){
            fprintf(stderr, "\"%s\" is a directory\n",argv[2]);
            exit(1);
        }
        
        if(fp == NULL){
            fprintf(stderr, "Cannot open the file!\n");
            exit(1);
        }else{
            fscanf(fp,"%d",&num);
        }
    }else if(ifnegative == 1){
        fprintf(stderr, "Invalid value\n");
        exit(1);
    }
    
}

/* MAIN */

int main(int argc, const char * argv[])
{
    int errno = 0,i=0;
    //sigset(SIGINT, handler);
    
    
    if(argc<1 && argc>15){
        fprintf(stderr, "Error in argumentss/n");
        exit(1);
    }
    
    
    ReadCommandArguments(argc,argv);
    
    PrintParameters();
    SaveCurTimeOfDay();
    
    fprintf(stdout, "00000000.000ms: emulation begins\n");
    
    
    if((errno = pthread_create(&thrd[0], 0,Arrival, (void*)0 ))){
        fprintf(stderr, "pthread_create[0] %s\n",strerror(errno));
        pthread_exit(0);
    }
    if((errno = pthread_create(&thrd[1], 0,Token, (void*)1 ))){
        fprintf(stderr, "pthread_create[1] %s\n",strerror(errno));
        pthread_exit(0);
    }
    if((errno = pthread_create(&thrd[2], 0,Service, (void*)2 ))){
        fprintf(stderr, "pthread_create[2] %s\n",strerror(errno));
        pthread_exit(0);
    }
    
    for (i=0; i<N; i++) {
        if((errno = pthread_join(thrd[i], 0))){
            fprintf(stderr, "pthread_join[i] %s\n",strerror(errno));
            pthread_exit(0);
            
        }
        
    }
    
    emulation_ends = GetMTimeOfDay();
    fprintf(stdout, "%012.3lfms: emulation ends\n ",emulation_ends);
    
    //PrintStatistics();
    return 0;
}
