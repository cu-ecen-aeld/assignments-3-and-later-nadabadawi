#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>

int main(int argc, char *argv[]) { 
    
    openlog(NULL, 0, LOG_USER);
    
    // check if there are two arguments passed
    if (argc != 3) {
        syslog(LOG_ERR, "Usage: %s <writefile> <writestr>", argv[0]);
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);

    // open file
    FILE *file = fopen(argv[1], "w");
    if (file == NULL) {
        syslog(LOG_ERR, "Error opening file %s. Error: %s", argv[1], strerror(errno));
        return 1;
    }

    // write into file
    fwrite(argv[2], sizeof(char), strlen(argv[2]), file);

    fclose(file);
    return 0;
}