#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>

#define __SHORT_FORM_OF_FILE__ \
                    (strrchr(__FILE__,'/') \
                    ? strrchr(__FILE__,'/')+1 \
                    : __FILE__ \
                    )

#define MS_TRACE(label, fmt, ...)  \
                                         { \
                                             char* strMSDEBUG = getenv("MS_DEBUG"); \
                                             if(strMSDEBUG != NULL) \
                                             { \
                                                 if((strcmp(strMSDEBUG, label)==0)||(strcmp(strMSDEBUG, "ALL")==0)) \
                                                 { \
                                                     if(strcmp(label, "audiosink")==0) \
                                                     { \
                                                        printf(("\033[0;35m[%s][%s][TID:%d][%d] " fmt "\033[0m\n"), __SHORT_FORM_OF_FILE__, __FUNCTION__, (pid_t) syscall (SYS_gettid), __LINE__, ##__VA_ARGS__); \
                                                     } \
                                                     else \
                                                     { \
                                                         printf(("\033[0;32m[%s][%s][TID:%d][%d] " fmt "\033[0m\n"), __SHORT_FORM_OF_FILE__, __FUNCTION__, (pid_t) syscall (SYS_gettid), __LINE__, ##__VA_ARGS__); \
                                                     } \
                                                 } \
                                             } \
                                         }
#define MS_ERROR(fmt, ...)   printf(("\033[0;31m[ERROR][%s][%s][TID:%d][%d] " fmt "\033[0m\n"), __SHORT_FORM_OF_FILE__, __FUNCTION__, (pid_t) syscall (SYS_gettid), __LINE__, ##__VA_ARGS__);

#define MS_TAG(label, fmt, ...)  \
                                         { \
                                             char* strMSDEBUG = getenv("MS_TAG"); \
                                             if(strMSDEBUG != NULL) \
                                             { \
                                                 if((strcmp(strMSDEBUG, label)==0)||(strcmp(strMSDEBUG, "ALL")==0)) \
                                                 { \
                                                     if(strcmp(label, "audiosink")==0) \
                                                     { \
                                                        printf(("\033[0;35m[%s][%s][TID:%d][%d] " fmt "\033[0m\n"), __SHORT_FORM_OF_FILE__, __FUNCTION__, (pid_t) syscall (SYS_gettid), __LINE__, ##__VA_ARGS__); \
                                                     } \
                                                     else \
                                                     { \
                                                         printf(("\033[0;32m[%s][%s][TID:%d][%d] " fmt "\033[0m\n"), __SHORT_FORM_OF_FILE__, __FUNCTION__, (pid_t) syscall (SYS_gettid), __LINE__, ##__VA_ARGS__); \
                                                     } \
                                                 } \
                                             } \
                                         }
