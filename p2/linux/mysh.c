/**
 * Mysh
 * Emulate the unix shell behavior with fork(), exec(), wait(), etc. Allows
 * some basic commands, as well as redirection. Also allows the "&" paramater
 * to run a child process in the background.
 */

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
//#include <malloc.h>

// Max line size is 512 bytes
#define MAX_LINE_SIZE 512

/**
 * Let the user know we are ready for input
 */
void 
prompt() 
{
  write(STDOUT_FILENO,"mysh> ", strlen("mysh> "));
}

/**
 * Standard error message for all errors
 */
void
error()
{
  char error_message[30] = "An error has occurred\n";
  write(STDERR_FILENO, error_message, strlen(error_message));
}

/**
 * Translate the line into a series of tokens which represent the provided 
 * commands / parameters.
 */
void
tokenize(char* line, char** token)
{
  int i = 0;
  token[i++] = strtok(line, " ");

  if(token[0] != NULL){
    while (token[i-1] != NULL) {
      token[i++] = strtok(NULL, " ");
    }
  }
}

/**
 * Should the child process run in the background? Detect whether the final 
 * argument was an ampersand "&". Returns "1" if an ampersand is detected, 
 * and "0" otherwise.
 */
int
runChildInBG(char** token)
{
  int i = 0;
  char *last = NULL;

  while(token[i] != NULL) {
    last = token[i++];
  }

  if(last != NULL && last[strlen(last) - 1] == '&') {
    if (strcmp(last, "&") == 0) {
      token[i - 1] = NULL;
    } else {
      token[i - 1][strlen(last) - 1] = '\0';
    }
    // Ampersand detected: the child should run in the background
    return 1;
  }
  
  // Ampersand not detected: the child should not run in the background
  return 0;
}

/**
 * Checks whether redirection needs to be done. If there is an ">" it returns 
 * the file name. Also modifies the arguments accordingly. Else returns NULL.
 */
char*
doRedirection(char** token)
{
  char line[512];
  int i = 1;
  
  if (token[0] == NULL) {
    return strdup("\0");
  }
  
  strcpy(line, token[0]);
  
  while(token[i] != NULL) {
    strcat(line, token[i]);
    ++i;
  } 
  
  int countRedirects = 0;
  int pos;
  for (pos = 0; line[pos]; ++pos) {
    if (line[pos] == '>') {
      ++countRedirects;
    }
  }

  if (countRedirects > 1) {
    error();
    exit(1);
  }

  char* filePos = strchr(line, '>');

  if (!filePos) {
    return strdup("\0");
  }

  /* if (strcmp(token[i-1][0], ">") != 0) { */
  char* searchingLastTok = strchr(token[i-1], '>');
  char* penultimateTok = NULL;

  if (i > 2) {
    penultimateTok = strchr(token[i-2], '>');
  }

  if (searchingLastTok || penultimateTok) {

    /* write(STDOUT_FILENO,"Command: %s\n", token[0]); */
    
    int found = 0;
    int i = 0;
    while (token[i]) {
      if (strchr(token[i],'>') || found) {
        token[i] = NULL;
        found = 1;
      }
      ++i;
    }
    
    /* write(STDOUT_FILENO,"File name: %s\n", filePos+1); */
    return (strdup(filePos+1));
  } else {
    error();
    exit(-1);
  }
}

/**
 * Execute the tokenized commands / parameters
 */
void 
execute(char** argv) 
{
  if (strcmp(argv[0], "cd") == 0) {
    char* home;
    
    if (argv[1] == NULL) {
      home = getenv("HOME");
    } else {
      home = argv[1];
    }
    
    if(chdir(home) < 0){
      error(); // "home" is not a valid path
    }
    return;
  }

  // This must be done before we fork since it strips the ampersand.
  int runChildProcInBg = runChildInBG(argv);
  int rc = fork();
  
  if (rc > 0) { // This is the parent
    // The child process sould run in the background if an "&" ampersand is 
    // passed is as the final parameter.
    if (runChildProcInBg == 0){
      int stat;
      // Wait because no final ampersand "&" parameter was detected
      int pid = waitpid(rc, &stat, WUNTRACED);
      if (pid < 0) {
        error();
      }
    } 
    return;
  } else if (rc == 0) { // This is the child
    
    char* fileName = doRedirection(argv);
    /* int i = 0; */
    /* while (argv[i]) { */
    /*   write(STDOUT_FILENO,"%s ", argv[i]); */
    /*   ++i; */
    /* } */
    /* printf ("\n"); */
    /* printf ("This is the fileName after checking for argv %s\n", fileName); */

    if (strcmp(fileName, "\0") != 0) {
      /* printf ("Closing stdout\n"); */
      int close_rc = close(STDOUT_FILENO);
      if (close_rc < 0) {
        error(); // Error closing file
        exit(1);
      }
      
      // Open a new file
      int fd = open(fileName, O_RDWR | O_TRUNC | O_CREAT, S_IRWXU);
      /* write(STDOUT_FILENO,"File descriptor number of new opened file = %d\n", fd); */
      /* write(STDOUT_FILENO,"File descriptor number of stdout = %d\n\n", STDOUT_FILENO); */
 
      if (fd < 0) {
        error(); // Error opening file
        exit(1);
      }
    }

    if (strcmp(argv[0], "pwd") == 0) {
      if(argv[1] != NULL){ error(); exit(0); } // Prevent additional arguments

      char cwd[256];
      if (getcwd(cwd, sizeof(cwd)) == NULL) {
        error();
        exit(1);
      } else {
        printf("%s\n", cwd);
      }
      exit(0);
    } else if (strcmp(argv[0], "wait") == 0) {
      // Nothing to do (this directive is for the parent process)
      exit(0);
    } else {
      /* For standard commands, which we represent exactly as the user 
      specified, without modification. */
      
      char* execStr;
      execStr = (char*)malloc(strlen(argv[0]) + /* strlen("/bin/") */ + 1);
      /* strcat(execStr, "/bin/"); */
      strcat(execStr, argv[0]);
      argv[0] = execStr;
      execvp(*argv, argv);
      error(); // We got an unrecognized command in argv[0]
      exit(1);
    }
    
    /*// Close the FD associated with stdout

    execvp("ls", exec_args);
    write(STDOUT_FILENO,"If you're reading this something went wrong!\n");*/

  } else if(rc < 0){ // Failed to fork

    fprintf(stderr, "fork() failed\n");
    exit(1);

  }
}

/**
 * Prompt the user, parse the input and pass it to execute.
 */
int
main(int argc, char *argv[]) 
{
  char line[MAX_LINE_SIZE];

  char* fileName;
  FILE* fp = NULL;

  // Batch file handling
  if (argc == 2) {
    fileName = argv[1];
    fp = fopen(fileName, "r");
    if (!fp) {
      error();
      exit(1);
    }
  } else if (argc > 2) {
    error();
    exit(1);
  } else {
    fp = stdin;
  }

  while (1) {
    if (argc == 1) {
      prompt();
    }
    
    if(fgets(line, MAX_LINE_SIZE, fp)){

      if (argc == 2) {
        write(STDOUT_FILENO, line, strlen(line));
      }

      // Replace new line with a null-terminator
      size_t length = strlen(line) - 1;
      if (line[length] == '\n'){
        line[length] = '\0';
      }

      // Tokenize the string into a series of commands / parameters
      char* token[20];
      tokenize(line, token);

      // Prevent the program from crashing if the user only enters whitespace
      if(token[0] == NULL) continue;


      if (strcmp(token[0], "exit") == 0) {
        // We only expect one argument
        if(token[1] != NULL){ error(); exit(0); }
        // Exit must be done outside the forked process
        exit(0);
      } else if(strcmp(token[0], "wait") == 0) {
        // We only expect one argument
        if(token[1] != NULL){ error(); exit(0); }
        // If we fork, then this 'wait' will not work as expected

        int stat;
        int done = 0;
        while (done != -1) {
          done = waitpid(-1, &stat, P_ALL);
        }

        if (done == -1) {
          if (errno == ECHILD) continue; // no more child processes
          else error();
        }
        /* (void) waitpid(-1, &stat, WUNTRACED); */
      } else {
        // Run the user request
        execute(token);
      }
    } else {
      // Batch file handling
      if (argc == 2) {
        fclose(fp);
        exit(0);
      }
    }
  }

  return 0;
}
