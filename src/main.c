#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int runexec(char **arr){
  int found=0;
  char command[100];
  strcpy(command,arr[0]);
  char *path=getenv("PATH");
  char pcc[300]; strcpy(pcc,path);
  char *dir=strtok(pcc,":");

  while(dir){
    char cp[300];
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
  if(!found){
    return 0;
  }

  int pid=fork();
  if(pid==0){
    execv(pcc,arr);
    perror("execv");
    exit(1);
  }
  else{
    wait(NULL);
    return 1;
  }

}

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);
  while(1){
    printf("$ ");

    char input[100];
    fgets(input, 100, stdin);
    input[strlen(input) - 1] = '\0';

    char *command=strtok(input," ");
    if(command==NULL){continue;}

    if(strcmp(command,"exit")==0){
      command=strtok(NULL," ");
      if (!command) {
        continue;
      }
      if (command && strcmp(command, "0") == 0) {
        return 0;
      }
    }

    if(strcmp(command,"echo")==0){
      while(command!=NULL){
        command=strtok(NULL," ");
        if (!command){break;}

        printf("%s ",command);
      }
      printf("\n");
      continue;
    }

    char *commands[]={"echo","exit","type","pwd"};
    if(strcmp(command, "type")==0){
      command=strtok(NULL," ");
      int found=0;
      for(int i=0;i<sizeof(commands)/sizeof(commands[0]);i++){
        if (!command) {
          command=" ";
        }
        if (strcmp(commands[i], command)==0){
          printf("%s is a shell builtin\n",command);
          found=1;
          break;
        }
      }
      if(found){continue;}

      char *path = getenv("PATH");
      if(!path){printf("%s: not found\n",command); continue;}

      char *cp = strdup(path);
      char *dir = strtok(cp, ":");
      found=0;

      while(dir){
        char fp[300];
        strcpy(fp,dir);
        strcat(fp,"/");
        strcat(fp,command);
        if (access(fp,X_OK)==0){
          printf("%s is %s\n",command,fp);
          found=1;
          break;
        }
        dir=strtok(NULL,":");
      }
    free(cp);
    if(!found){
      printf("%s: not found\n", command);
    }
    continue;

    }
    
    char *args[100];
    int num=0;

    while(command!=NULL){
      args[num]=command;
      command=strtok(NULL," ");
      num++;
    }
    args[num] = NULL; 

    int i=runexec(args);
    if(!i){
      printf("%s: command not found\n", input);
    }
  }
}

