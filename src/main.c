#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

    char *commands[]={"echo","exit","type"};
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
        if (access(fp, X_OK) == 0) {
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
    printf("%s: command not found\n", input);

  }
}

