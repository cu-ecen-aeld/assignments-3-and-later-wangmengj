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

#define SIZE_BUFFER 1000
#define STR_PORT "9000"
// ipv6 takes 39 characters
#define IP_LENTH 39
#define OUTPUT_FILENAME "/var/tmp/aesdsocketdata"

//#define LOGCONSOLE 

#if defined (LOGCONSOLE)
#define DBGLOG(...) printf (__VA_ARGS__)
#define ERRLOG(...) printf (__VA_ARGS__)
#else
#define DBGLOG(...) syslog(LOG_DEBUG,__VA_ARGS__)
#define ERRLOG(...) syslog(LOG_ERR,__VA_ARGS__) 
#endif

int strGetIP(const struct sockaddr *sa, char *s, size_t maxlen);
void signalHandler(int signal);

bool quitServices = false;

int main(int argc, char * argv[])
{

#if !defined (LOGCONSOLE)
    openlog(NULL, LOG_PID, LOG_USER);
#endif

    // ignore SIGPIPE
    if(SIG_ERR == signal(SIGPIPE, SIG_IGN))
    {
        ERRLOG("Ignore SIGPIPE failed: %s \n", strerror(errno));
        return -1;
    }

    if(SIG_ERR == signal(SIGINT, signalHandler))
    {
        ERRLOG("Register SIGINT failed: %s \n", strerror(errno));
        return -1;
    }

    if(SIG_ERR == signal(SIGTERM, signalHandler))
    {
        ERRLOG("Register SIGTERM failed: %s \n", strerror(errno));
        return -1;
    }
    
    int sdListen = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if(-1 == sdListen)
    {
        ERRLOG("Get socket failed: %s \n", strerror(errno));
        return -1;
    }

    int iVar = 1;
    if (setsockopt(sdListen, SOL_SOCKET, SO_REUSEADDR, (char *)&iVar, sizeof(iVar)) < 0)
        DBGLOG("setsockopt:SO_REUSEADDR failed.");

#ifdef SO_REUSEPORT
    if (setsockopt(sdListen, SOL_SOCKET, SO_REUSEPORT, (char *)&iVar, sizeof(iVar)) < 0)
        DBGLOG("setsockopt:SO_REUSEPORT failed.");
#endif

/*
    struct linger lin;
    lin.l_onoff = 0;
    lin.l_linger = 0;
    setsockopt(sdListen, SOL_SOCKET, SO_LINGER, (const char *)&lin, sizeof(int));
*/

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
        close(sdListen);
        return -1;
    }

    // test a 
    bool bBind = false;
    for(struct addrinfo * cur = addInfo ; cur != NULL ; cur = cur->ai_next)
    {
        struct sockaddr_in *addr = (struct sockaddr_in *)cur->ai_addr;
        char ipbuf[16];
        int port;
        printf("ip: %s\n", inet_ntop(AF_INET, &addr->sin_addr, ipbuf, 16));
        printf("port: %s\n", inet_ntop(AF_INET, &addr->sin_port, (void *)&port, 2));
        printf("- family: %d\n", addr->sin_family);

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
        freeaddrinfo(addInfo);
        close(sdListen);
        return -1;
    }

    if(argc > 1 && NULL != argv[1] && 0 == strcmp("-d", argv[1]))
    {
        DBGLOG("Entering daemon mode.");
        // 
        pid_t pRetFork = fork();
        if(-1 == pRetFork)
        {
            // if something wrong happened,
            freeaddrinfo(addInfo);
            close(sdListen);
            return -1;
        }

        if(0 == pRetFork)
        {
            // We are in child process and return only failed.
            // child process continues ... 
            //if(-1 == setsid())
        }
        else
        {
            // we are in parent process
            freeaddrinfo(addInfo);
            close(sdListen);
            return 0;
        }
    }

    if(0 != listen(sdListen, 10))
    {
        ERRLOG("listen failed: %s \n", strerror(errno));
        freeaddrinfo(addInfo);
        close(sdListen);
        return -1;
    }

    FILE * fOutput = fopen(OUTPUT_FILENAME, "a+");
    if(NULL == fOutput)
    {
        ERRLOG("fopen failed: %s \n", strerror(errno));
        freeaddrinfo(addInfo);
        close(sdListen);
        return -1;
    }

    // start services:
    while(!quitServices)
    {
        struct sockaddr clientAddr; 
        socklen_t clientAddrLen = sizeof(clientAddr) ; 
        memset(&clientAddr, 0, sizeof(clientAddr));

        int sdClient = accept(sdListen, &clientAddr, &clientAddrLen);
        if(-1 == sdClient)
        {
            if(EWOULDBLOCK == errno || EAGAIN == errno)
                continue;

            ERRLOG("accept failed: %s \n", strerror(errno));
            break;
        }

        // get the ip address of the client:
        char strIP[IP_LENTH]; 
        memset(strIP, 0, IP_LENTH);
        strGetIP(&clientAddr, strIP, IP_LENTH);
        time_t t = time(NULL);
        DBGLOG("Accepted connection from %s , %d @ %s.\n", strIP, sdClient, ctime(&t));

        /// prepare a buffer for the transfer of the data
        char buffer[SIZE_BUFFER];
        memset(buffer, 0, SIZE_BUFFER);

        // point to the end of the file to start:
        fseek(fOutput, 0L, SEEK_END); 

        // receive all packages/data
        int iReceived = 0;
        do
        {
            iReceived = recv(sdClient, buffer, SIZE_BUFFER, MSG_DONTWAIT);
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
            int iRet = send(sdClient, buffer, iReceived, 0);
            DBGLOG("Data send: %d vs read: %d, %s \n", iRet, iReceived, buffer);
        }
        
        DBGLOG("Closed connection from %s\n", strIP);
        close(sdClient);
    }

    if(quitServices) 
        DBGLOG("Caught signal, exiting\n");

    fclose(fOutput);
    if (0 !=  remove(OUTPUT_FILENAME))
        DBGLOG("Remove file %s failed.\n", OUTPUT_FILENAME);

    close(sdListen);
    freeaddrinfo(addInfo);
#if !defined (LOGCONSOLE)
    closelog();
#endif

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
    quitServices = true; 
}

