#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);
  while(1){
    printf("$ ");
    // Wait for user input
    char input[100];
    fgets(input, 100, stdin);
    input[strlen(input) - 1] = '\0';

    char *command=strtok(input," ");

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

    char *commands[]={"echo","exit"};
    if(strcmp(command, "type")==0){
      command=strtok(NULL," ");
      int found=0;
      for(int i=0;i<sizeof(commands)/sizeof(commands[0]);i++){
        if (!command) {
          command=" ";
        }
        if (strcmp(commands[i], command)==0){
          printf("%s is a shell bulletin\n",command);
          found=1;
          break;
        }
      }
      if(!found){printf("%s: not found\n",command);}
      continue;
    }
    printf("%s: command not found\n", input);

  }
}

