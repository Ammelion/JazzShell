#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>

int parser(char *input, char *args[]){
    int count=0,single=0,idouble=0;
    char *read=input,*write=input,*token=NULL;

    while(*read){
        char c=*read;

        if(single){
            if (c=='\''){
                single=0;
            }
            else{
                *write++=c;
            }
            read++;

        }
        else if(idouble){
            if (c=='"'){
                idouble= 0;
                read++;
            }
            else if (c=='\\' &&(read[1]=='"'|| read[1]=='\\'||read[1]=='$'
                                || read[1]=='`'|| read[1]=='\n')){
                read++;
                if(*read!='\n'){
                    *write++=*read;
                }
                read++;
            }
            else {
                *write++=c;
                read++;
            }

        }
        else{
            if(c=='\''){
                single=1;
                if(!token){
                    token=write;
                    args[count++]=token;
                }
                read++;
            }
            else if(c == '"'){
                idouble=1;
                if (!token){
                    token=write;
                    args[count++]=token;
                }
                read++;
            }
            else if(c=='\\'&&read[1]){
                if(!token){
                    token=write;
                    args[count++]=token;
                }
                read++;
                *write++=*read++;
            }
            else if(isspace((unsigned char)c)){
                if(token){
                    *write++='\0';
                    token=NULL;
                }
                read++;
            }
            else{
                if(!token){
                    token=write;
                    args[count++]=token;
                }
                *write++=c;
                read++;
            }
        }
    }
    if (token){
        *write++= '\0';
    }
    args[count]=NULL;
    return count;
}

int runexec(char **arr, int stream, char *red_op, char *red_file){
    int found=0;
    char command[100];
    strcpy(command,arr[0]);
    char *path=getenv("PATH");
    char pcc[300]; strcpy(pcc,path);
    char *dir=strtok(pcc,":");
    char cp[300];

    while(dir){
        strcpy(cp,dir);
        strcat(cp,"/");
        strcat(cp,command);
        if(access(cp,X_OK)==0){
            strcpy(pcc,cp);
            found=1;
            break;
        }
        dir=strtok(NULL,":");
    }

    if(!found) return 0;
    int pid=fork();
    if (pid==0){
        if (red_file){
            int fd=open(red_file, O_WRONLY|O_CREAT|O_TRUNC,0666);
            if (fd<0){
                perror(red_file);
                exit(1);
            }
            dup2(fd,stream);
            if (strcmp(red_op, "&>")==0)
                dup2(fd,2);
            close(fd);
        }
        execv(pcc, arr);
        perror("execv");
        exit(1);
    }
    else {
        wait(NULL);
        return 1;
    }
}

int main(void){
    setbuf(stdout,NULL);
    char *args[100];
    while(1){
        printf("$ ");
        char input[100];
        if(!fgets(input,100,stdin)) break;
        input[strcspn(input,"\n")]='\0';
        int nargs=parser(input,args);
        if(nargs==0) continue;
        char *cmd=args[0];

        int stream=-1;
        int index=-1;
        char *red_file=NULL;
        char *red_op=NULL;
        for(int i=0; args[i]; i++){
            if(strcmp(args[i],">")==0 || strcmp(args[i],"1>")==0){
                stream=1; index=i;
                red_op  = args[i];
                red_file=args[i+1];
                break;
            }
            if(strcmp(args[i],"2>")==0){
                stream=2; index=i;
                red_op  = args[i];
                red_file=args[i+1];
                break;
            }
            if(strcmp(args[i],"&>")==0){
                stream=1; index=i;
                red_op  = args[i];
                red_file=args[i+1];
                break;
            }
        }

        int builtin_saved=-1;
        if (red_file){
            int fd=open(red_file, O_WRONLY|O_CREAT|O_TRUNC,0666);
            if (fd<0){
                perror(red_file);
            } else {
                builtin_saved=dup(stream);
                dup2(fd,stream);
                if (strcmp(red_op,"&>")==0)
                    dup2(fd,2);
                close(fd);
            }
        }

        if (index!=-1)
            args[index]=NULL;
            nargs = index;

        if(strcmp(cmd,"exit")==0){
            if(nargs>1 && strcmp(args[1],"0")==0) break;
        }
        else if(strcmp(cmd,"cd")==0){
            char *target=nargs>1 ? args[1] : getenv("HOME");
            if(!target) target="/";
            if(target[0]=='~'){
                char hcp[300];
                strcpy(hcp,getenv("HOME"));
                strcat(hcp,target+1);
                target=hcp;
            }
            if(chdir(target)!=0)
                fprintf(stderr,"cd: %s: %s\n",target,strerror(errno));
        }
        else if(strcmp(cmd,"pwd")==0){
            if(nargs>1){
                printf("pwd: Too many arguments\n");
            } else {
                char cwd[300];
                if(getcwd(cwd,sizeof(cwd)))
                    printf("%s\n",cwd);
            }
        }
        else if(strcmp(cmd,"echo")==0){
            for(int i=1;i<nargs;i++){
                printf("%s",args[i]);
                if(i+1<nargs) printf(" ");
            }
            printf("\n");
        }
        else if(strcmp(cmd,"type")==0){
            char *t=nargs>1?args[1]:NULL;
            char *builtins[]={"echo","exit","type","pwd","cd"};
            int fb=0;
            for(int i=0;i<5;i++){
                if(t&&strcmp(t,builtins[i])==0){
                    printf("%s is a shell builtin\n",t);
                    fb=1;
                    break;
                }
            }
            if(!fb){
                char *p2=getenv("PATH");
                char *cp2=strdup(p2);
                char *dir=strtok(cp2,":");
                int fe=0;
                while(dir){
                    char fp[300];
                    strcpy(fp,dir); strcat(fp,"/"); strcat(fp,t?t:"");
                    if(t&&access(fp,X_OK)==0){
                        printf("%s is %s\n",t,fp);
                        fe=1;
                        break;
                    }
                    dir=strtok(NULL,":");
                }
                free(cp2);
                if(!fe) printf("%s: not found\n",t?t:"");
            }
        }
        else {
            if(!runexec(args, stream, red_op, red_file))
                printf("%s: command not found\n",cmd);
        }

        if (builtin_saved!=-1){
            dup2(builtin_saved,stream);
            close(builtin_saved);
        }
    }
    return 0;
}
