#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>

int parser(char *input, char *args[]) {
  static char buf[1000]; // Safe static buffer
  int i=0, arg=0, pos=0;

  while(input[i]) {
    while(isspace((unsigned char)input[i])) i++;
    if(!input[i]) break;

    args[arg++] = &buf[pos];

    if(input[i] == '\'' || input[i] == '"') {
      char quote = input[i++];
      while(input[i] && input[i] != quote) {
        if(input[i] == '\\' && input[i+1]) {
          buf[pos++] = input[i+1];
          i += 2;
        } else {
          buf[pos++] = input[i++];
        }
      }
      if(input[i] == quote) i++;
    } else {
      while(input[i] && !isspace((unsigned char)input[i])) {
        if(input[i] == '\\' && input[i+1]) {
          buf[pos++] = input[i+1];
          i += 2;
        } else {
          buf[pos++] = input[i++];
        }
      }
    }

    buf[pos++] = '\0';
  }

  args[arg] = NULL;
  return arg;
}


int runexec(char **arr){
  int found=0;
  char command[100];
  strcpy(command,arr[0]);
  char *path=getenv("PATH");
  char pcc[300]; strcpy(pcc,path);
  char *dir=strtok(pcc,":");
  char cp[300];
  while(dir){
    strcpy(cp,dir); strcat(cp,"/"); strcat(cp,command);
    if(access(cp,X_OK)==0){
      strcpy(pcc,cp);
      found=1;
      break;
    }
    dir=strtok(NULL,":");
  }
  if(!found) return 0;
  int pid=fork();
  if(pid==0){
    execv(pcc,arr);
    perror("execv");
    exit(1);
  } else {
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

    if(strcmp(cmd,"exit")==0){
      if(nargs>1 && strcmp(args[1],"0")==0) break;
      continue;
    }

    if(strcmp(cmd,"cd")==0){

      char *target=nargs>1 ? args[1] : getenv("HOME");

      if(!target) target="/";
      if(target[0]=='~'){
        char hcp[300]; strcpy(hcp,getenv("HOME")); strcat(hcp,target+1);
        target=hcp;
      }
      if(chdir(target)!=0) fprintf(stderr,"cd: %s: %s\n",target,strerror(errno));
      continue;
    }

    if(strcmp(cmd,"pwd")==0){
      if(nargs>1){ printf("pwd: Too many arguments\n"); continue; }
      char cwd[300];
      if(getcwd(cwd,sizeof(cwd))) printf("%s\n",cwd);
      continue;
    }

    if(strcmp(cmd,"echo")==0){
      for(int i=1;i<nargs;i++){
        printf("%s",args[i]);
        if(i+1<nargs) printf(" ");
      }
      printf("\n");
      continue;
    }

    if(strcmp(cmd,"type")==0){
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
      if(fb) continue;
      char *p2=getenv("PATH");
      char *cp2=strdup(p2);
      char *dir=strtok(cp2,":");
      int fe=0;
      while(dir){
        char fp[300]; strcpy(fp,dir); strcat(fp,"/"); strcat(fp,t?t:"");
        if(t&&access(fp,X_OK)==0){
          printf("%s is %s\n",t,fp);
          fe=1;
          break;
        }
        dir=strtok(NULL,":");
      }
      free(cp2);
      if(!fe) printf("%s: not found\n",t?t:"");
      continue;
    }

    if(!runexec(args)) printf("%s: command not found\n",cmd);
  }
  return 0;
}
