


#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>


int main(int argc, char**argv)
{

    // setlogmask (LOG_UPTO (LOG_NOTICE));
    openlog (NULL, 0, LOG_USER);

    // syslog (LOG_DEBUG, "test");

    if (argc != 3) {
        syslog (LOG_ERR, "Cmd line args incorrect");
        exit(1);
    }

    char* writefile = argv[1];
    char* writestr = argv[2];

    syslog (LOG_DEBUG, "Writing %s to %s", writestr, writefile);
    // printf("Writing %s to %s", writefile, writestr );

    FILE* fp = fopen(writefile, "w");
    if (!fp) {
        syslog (LOG_ERR, "Could not open output file");
        exit(1);
    }
    int ret = fputs(writestr, fp);
    if (ret == EOF) {        
        syslog (LOG_ERR, "Could not write output file");
        exit(1);
    }
    fclose(fp);

    closelog ();

    exit(0);
}



