#include <sys/socket.h>
#include <stdio.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include <arpa/inet.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>

#include <pthread.h>
#include <sys/time.h>

#define SIZE_BUFFER 1000
#define STR_PORT "9000"
// ipv6 takes 39 characters
#define IP_LENTH 39

#define USE_AESD_CHAR_DEVICE 1
#ifdef USE_AESD_CHAR_DEVICE
#define OUTPUT_FILENAME "/dev/aesdchar"
#else
#define OUTPUT_FILENAME "/var/tmp/aesdsocketdata"
#endif

// mask off this to remove Alarm related function.
//#define USE_ALARM

#define PID_FILE "/var/run/aesdsocket.pid"

//#define LOGCONSOLE 

#if defined (LOGCONSOLE)
#define DBGLOG(...) printf (__VA_ARGS__)
#define ERRLOG(...) printf (__VA_ARGS__)
#else
#define DBGLOG(...) syslog(LOG_DEBUG,__VA_ARGS__)
#define ERRLOG(...) syslog(LOG_ERR,__VA_ARGS__) 
#endif


// thread realted items;
struct listOfThread 
{
    pthread_t thread; 
    int sdClient;
    char strIP[IP_LENTH];

    struct listOfThread * pNext;
    struct listOfThread * pPrevious;

    bool isComplete; 
};

// static items 
static struct listOfThread * pListHead = NULL;
static struct listOfThread * pListTail = NULL;
static pthread_mutex_t mutexFOutput;

void * threadHandler(void *);


int strGetIP(const struct sockaddr *sa, char *s, size_t maxlen);
void signalHandler(int signal);

bool quitServices = false;

#ifdef USE_ALARM
bool isAlarmed = false;
#endif // USE_ALARM

int main(int argc, char * argv[])
{
    int iReturnValue = -1; // default is failure

#if !defined (LOGCONSOLE)
    openlog(NULL, LOG_PID, LOG_USER);
#endif

    // ignore SIGPIPE
    if(SIG_ERR == signal(SIGPIPE, SIG_IGN))
    {
        ERRLOG("Ignore SIGPIPE failed: %s \n", strerror(errno));
        goto closelog;
    }

    if(SIG_ERR == signal(SIGINT, signalHandler))
    {
        ERRLOG("Register SIGINT failed: %s \n", strerror(errno));
        goto closelog;
    }

    if(SIG_ERR == signal(SIGTERM, signalHandler))
    {
        ERRLOG("Register SIGTERM failed: %s \n", strerror(errno));
        goto closelog;
    }

#ifdef USE_ALARM
    if(SIG_ERR == signal(SIGALRM, signalHandler))
    {
        ERRLOG("Register SIGALRM failed: %s \n", strerror(errno));
        goto closelog;
    }
#endif // USE_ALARM
    
    int sdListen = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if(-1 == sdListen)
    {
        ERRLOG("Get socket failed: %s \n", strerror(errno));
        goto closelog;
    }

    int iVar = 1;
    if (setsockopt(sdListen, SOL_SOCKET, SO_REUSEADDR, (char *)&iVar, sizeof(iVar)) < 0)
        DBGLOG("setsockopt:SO_REUSEADDR failed.");

#ifdef SO_REUSEPORT
    if (setsockopt(sdListen, SOL_SOCKET, SO_REUSEPORT, (char *)&iVar, sizeof(iVar)) < 0)
        DBGLOG("setsockopt:SO_REUSEPORT failed.");
#endif

    struct addrinfo serverAdd;  
    struct addrinfo *addInfo = NULL;  
    memset(&serverAdd, 0, sizeof(serverAdd));

    // If the AI_PASSIVE flag is specified in hints.ai_flags, and node  is  NULL,
    // then  the  returned  socket  addresses  will  be suitable for bind(2)ing a
    // socket that will accept(2) connections.
    serverAdd.ai_flags = AI_PASSIVE;
    if(0 != getaddrinfo(NULL, STR_PORT, &serverAdd, &addInfo))
    {
        ERRLOG("getaddrinfo failed: %s \n", strerror(errno));
        goto closeSDListen;
    }

    // test a 
    bool bBind = false;
    for(struct addrinfo * cur = addInfo ; cur != NULL ; cur = cur->ai_next)
    {
        struct sockaddr_in *addr = (struct sockaddr_in *)cur->ai_addr;
        char ipbuf[16];
        int port;
        DBGLOG("ip: %s\n", inet_ntop(AF_INET, &addr->sin_addr, ipbuf, 16));
        DBGLOG("port: %s\n", inet_ntop(AF_INET, &addr->sin_port, (void *)&port, 2));
        DBGLOG("- family: %d\n", addr->sin_family);

        // try other addresses ....
        if(0!= bind(sdListen, cur->ai_addr, cur->ai_addrlen))
        {
            ERRLOG("bind failed: %s, try next one. \n", strerror(errno));
            continue;
        }
        else
        {
            // success in binding ... 
            bBind = true;
            break;
        }
    }

    if(!bBind)
    {
        ERRLOG("all bind failed: %s \n", strerror(errno));
        goto freeAddrinfo;
    }

    if(argc > 1 && NULL != argv[1] && 0 == strcmp("-d", argv[1]))
    {
        DBGLOG("Entering daemon mode.");
        // 
        pid_t pRetFork = fork();
        if(-1 == pRetFork)
        {
            // if something wrong happened,
            goto freeAddrinfo;
        }

        if(0 == pRetFork)
        {
            // We are in child process and return only failed.
            // child process continues ... 
            //if(-1 == setsid())
        }
        else
        {
            // record child process id
            FILE * pidfile = fopen(PID_FILE, "w");
            if(NULL != pidfile)
            {
                DBGLOG("creating pid file.");
                fprintf(pidfile, "%d", pRetFork);
                fclose(pidfile);
            }

            // we are in parent process
            goto successExit;
        }
    }

    if(0 != listen(sdListen, 10))
    {
        ERRLOG("listen failed: %s \n", strerror(errno));
        goto freeAddrinfo;
    }

    DBGLOG("fopening file: %s \n", OUTPUT_FILENAME);
    FILE * fOutput = fopen(OUTPUT_FILENAME, "a+");
    if(NULL == fOutput)
    {
        ERRLOG("fopen failed: %s \n", strerror(errno));
        goto freeAddrinfo;
    }

#ifdef USE_ALARM
    // setup a timer for every 10s 
    struct itimerval newValue;
    newValue.it_interval.tv_sec = 10; // every 10 seconds
    newValue.it_interval.tv_usec = 0;
    newValue.it_value = newValue.it_interval;
    if(-1 == setitimer(ITIMER_REAL, &newValue, NULL))
    {
        DBGLOG("setitimer failed: %s \n", strerror(errno));
    }
#endif // USE_ALARM
    
    time_t t = time(NULL);
    // start services:
    while(!quitServices)
    {
        struct sockaddr clientAddr; 
        socklen_t clientAddrLen = sizeof(clientAddr) ; 
        memset(&clientAddr, 0, sizeof(clientAddr));

#ifdef USE_ALARM
        if(isAlarmed)
        {
            isAlarmed = false;
            //RFC 2822-compliant date format with a newline
            //  (with an English locale for %a and %b)
            char strRFC2822[] = "timestamp:%a, %d %b %Y %T %z\n";

            t = time(NULL);
            struct tm * tmp = localtime(&t);
            if(NULL != tmp)
            {
                char tmString[200];
                int iRet = strftime(tmString, sizeof(tmString), strRFC2822, tmp);
                if(0 != iRet)
                {
                    tmString[iRet]= '\0';
                    DBGLOG("alarm triggerd: %s \n", tmString);
                    pthread_mutex_lock(&(mutexFOutput));
                    fseek(fOutput, 0L, SEEK_END); 
                    fwrite(tmString, 1 , iRet, fOutput);
                    fflush(fOutput);
                    pthread_mutex_unlock(&(mutexFOutput));
                }
                else 
                   DBGLOG("strftime returned 0: %s \n", strerror(errno));

            }
            else 
                DBGLOG("Incorrect NULL locatime: %s \n", strerror(errno));
        }
#endif 
 
        // Join all complete child threads.
        struct listOfThread * pList = pListHead;
        while(NULL != pList)
        {
            struct listOfThread * pTmp = pList; 
            pList = pList->pNext;

            if(pTmp -> isComplete)
            {
                if(NULL != pTmp->pPrevious)
                    pTmp->pPrevious->pNext = pTmp->pNext; 
                if(NULL != pTmp->pNext)
                    pTmp->pNext->pPrevious = pTmp->pPrevious;
                if(pListHead == pTmp)
                    pListHead = pTmp->pNext;
                if(pListTail == pTmp)
                    pListTail = pTmp->pPrevious;
                
                void * p; 
                pthread_join(pTmp->thread, &p);
                free(pTmp);
            }
        }
        

        //  
        int sdClient = accept(sdListen, &clientAddr, &clientAddrLen);
        if(-1 == sdClient)
        {
            if(EWOULDBLOCK == errno || EAGAIN == errno)
                continue;

            ERRLOG("accept failed: %s \n", strerror(errno));
            break;
        }
        
        DBGLOG("accepted 1 connection \n");

        pList = malloc(sizeof(struct listOfThread));
        if(NULL == pList)
        {
            // running out of memory?
            ERRLOG(" failed malloc pList: %s \n", strerror(errno));
            break;
        }

        // get the ip address of the client:
        memset(pList->strIP, 0, IP_LENTH);
        strGetIP(&clientAddr, pList->strIP, IP_LENTH);
        DBGLOG("Accepted connection from %s , %d @ %s.\n", 
                pList->strIP, sdClient, ctime(&t));


        pList->pNext = NULL;
        pList->pPrevious = NULL;
        pList->isComplete = false;
        pList->sdClient = sdClient;

        if(NULL == pListHead)
            pListHead = pList;
        if(NULL == pListTail)
            pListTail = pList;
        else
        {
            pList->pPrevious = pListTail;
            pListTail->pNext = pList;
            pListTail=pList;
        }

        int iRet = pthread_create(&(pList->thread),NULL, &threadHandler, pList); 
        if(0 != iRet)
        {
            // when thread created wrong, will remove it from list:
            pListTail = pList->pPrevious;
            if(NULL != pListTail)
                pListTail->pNext = NULL;

            free(pList);
            ERRLOG(" failed malloc pList: %s \n", strerror(errno));
            break;
        }
    }

    if(quitServices) 
        DBGLOG("Caught signal, exiting\n");

#ifndef USE_AESD_CHAR_DEVICE
    if (0 !=  remove(OUTPUT_FILENAME))
        DBGLOG("Remove file %s failed.\n", OUTPUT_FILENAME);
#endif
    
    // Join all child threads.
    struct listOfThread * pList = pListHead;
    while(NULL != pList)
    {
        void * p; 
        pthread_join(pList->thread, &p);

        struct listOfThread * pTmp = pList; 
        pList = pList->pNext;
        free(pTmp);
    }
   

    fclose(fOutput);

successExit:
    iReturnValue = 0;

freeAddrinfo:
    freeaddrinfo(addInfo);

closeSDListen:
    close(sdListen);

closelog:
#if !defined (LOGCONSOLE)
    closelog();
#endif
    return iReturnValue;
}


int strGetIP(const struct sockaddr *sa, char *s, size_t maxlen)
{
    switch(sa->sa_family) 
    {
        case AF_INET:
            inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),
                    s, maxlen);
            break;

        case AF_INET6:
            inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),
                    s, maxlen);
            break;

        default:
            DBGLOG("unknow ip address.\n");
            return -1;
    }
    return 0;
}


void signalHandler(int signal)
{
    if(SIGINT == signal || SIGTERM == signal )
        quitServices = true; 

#ifdef USE_ALARM
    if(SIGALRM == signal)
        isAlarmed = true;
#endif // USE_ALARM
}


void * threadHandler(void * alist)
{
    struct listOfThread * list = (struct listOfThread *) alist;
    if(NULL == list)
    {
        ERRLOG("threadHandler: empty arguments\n");
        return NULL;
    }

    // each thread open its own output file. 
    FILE * fOutput = fopen(OUTPUT_FILENAME, "a+");
    if(NULL == fOutput)
    {
        ERRLOG("fopen failed: %s \n", strerror(errno));
        list->isComplete = true;
        return NULL;
    }


    /// prepare a buffer for the transfer of the data
    char buffer[SIZE_BUFFER];
    memset(buffer, 0, SIZE_BUFFER);

    // point to the end of the file to start:
    fseek(fOutput, 0L, SEEK_END); 

    time_t t = time(NULL);
    pthread_mutex_lock(&(mutexFOutput));
    // receive all packages/data
    int iReceived = 0;
    do
    {
        iReceived = recv(list->sdClient, buffer, SIZE_BUFFER, MSG_DONTWAIT);
        if(0 < iReceived)
        {
            int iRet = fwrite(buffer, 1 , iReceived, fOutput);
            DBGLOG("Data saved: %d vs received: %d, %d. \n", 
                    iRet, iReceived, (int) buffer[iReceived - 1]);

            // quit receving when a full packet is received.
            if('\n' == buffer[iReceived - 1]) 
            {
                t = time(NULL);
                DBGLOG("Full package: %d vs received: %d, %s. \n", 
                    iRet, iReceived, ctime(&t));
                break;
            }
        }

    } while(((errno == EAGAIN || errno == EWOULDBLOCK) && iReceived <0) || (iReceived > 0));
    pthread_mutex_unlock(&(mutexFOutput));


    // really finsihed recving? 
    t = time(NULL);
    DBGLOG("Data saved other: %d , %s at %s  \n", iReceived, strerror(errno), ctime(&t));

    // make sure all data saved to file.
    fflush(fOutput);
    rewind(fOutput);

    iReceived = 0;
    // send everything in the file to the client.
    while(0 < (iReceived = fread(buffer, 1, SIZE_BUFFER, fOutput)))
    {
        buffer[iReceived] = '\0'; 
        int iRet = send(list->sdClient, buffer, iReceived, 0);
        DBGLOG("Data send: %d vs read: %d, %s \n", iRet, iReceived, buffer);
    }
    
    DBGLOG("Closed connection from %s\n", list->strIP);
    close(list->sdClient);
    fclose(fOutput);
    list->isComplete = true;

    return NULL;
}
