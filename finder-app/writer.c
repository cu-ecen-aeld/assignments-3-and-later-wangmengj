#include<stdio.h>
#include<syslog.h>
#include<errno.h>
#include<string.h>
#include<stdbool.h>

//#define LOGCONSOLE 

#if defined (LOGCONSOLE)
#define DBGLOG(...) printf (__VA_ARGS__)
#define ERRLOG(...) printf (__VA_ARGS__)
#else
#define DBGLOG(...) syslog(LOG_DEBUG,__VA_ARGS__)
#define ERRLOG(...) syslog(LOG_ERR,__VA_ARGS__) 
#endif 

int main(int argc, char * argv[]) 
{
#if !defined (LOGCONSOLE) 
    openlog(NULL, LOG_PID, LOG_USER);
#endif 
    
    //Step 1: check arguments.
    if(argc != 3)
    {
        ERRLOG("writer needs 2 arguments: filesdir and searchstr\n");
        return 1;
    }

    DBGLOG("Arguments are ok.\n");

    // Step 2: open/create file.
    // For "w+" mode:
    // w+     Open  for  reading and writing.  The file is created if it does not exist, otherwise
    //          it is truncated.  The stream is positioned at the beginning of the file. 
    FILE * fp = fopen(argv[1],"w+");
    if(NULL == fp)
    {
        ERRLOG("File:%s can not be created/open:%s \n", argv[1], strerror(errno));
        return 1;
    }

    DBGLOG("File:%s opened.\n", argv[1]);


    // Step 3: write to the file.
    int szString = strlen(argv[2]);
    int itemsWriten = fwrite(argv[2], sizeof(char), szString, fp);
    bool quitInError = false;
    if(itemsWriten != szString)
    {
        ERRLOG("File:%s can not be created/open:%s \n", argv[1], strerror(errno));
        quitInError = true;
    }

    // Step 4: log debug message when everything goes find.
    else DBGLOG("Writing %s to %s", argv[2], argv[1]);

    // Step 4: close everything and return result.
    fclose(fp);
#if !defined (LOGCONSOLE) 
    closelog();
#endif 
    return quitInError ? 1:0;
}
