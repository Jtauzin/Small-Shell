#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>

/* Declare constants and globals */
#define INPUT_MAX 2048
bool isRunning = true;
char* userInput;				// we need to keep this global and make copies for tasks
int childExitStatus;
bool childExitNormally = true;
bool foregroundOnly = false;
bool SIGINT_called = false;
int backgroundIDs[50] = { 0 };
int backgroundCount = 0;
int currentPID = 0;
int currentStatus = 0;
struct sigaction SIGTSTP_action = { 0 };
struct sigaction SIGINT_action = { 0 };
/* Function to ensure all data in string byte allocation is empty */
void initializeString(char* string, int size)
{
	int index = 0;
	while (index < size) {
		string[index] = '\0';							// simply cycle through and make sure every index is a null terminator
		index++;
	}
}

/* Funtion to determine if input needs to be ignored or not by returning a bool */
bool ignoreInput()
{
	char* input = malloc(strlen(userInput) * sizeof(char) + 1);
	initializeString(input, strlen(userInput) * sizeof(char) + 1);
	strcpy(input, userInput);
	char* buffer;
	char* token;
	token = strtok_r(input, " ", &buffer);
	if (token == NULL)									// if token is null, has a # icon or is just a newline, ignore it
	{
		return true;
	}
	else if (token[0] == '#' || token[0] == '\n')
	{
		return true;
	}
	return false;
}

/* Function we will use to override the SIGTSTP signal */
void handleSIGTSTP()
{
	write(1, "\n\0", 2);								// note we will use write instead of printf since it is a re-entrant function
	char* enteringMessage = "Exiting foreground only mode.\n";
	char* exitingMessage = "Now entering foreground only mode.\n";
	char* colon = ": ";
	int status;
	bool addColon = false;
	if (currentPID == 0) {
		addColon = true;
	}
	else
	{
		currentPID = waitpid(currentPID, &status, 0);	// if we have a forground process running, wait for it to complete
	}
	if (foregroundOnly)
	{
		foregroundOnly = false;
		write(1, enteringMessage, strlen(enteringMessage) + 1);
	}
	else
	{
		foregroundOnly = true;
		write(1, exitingMessage, strlen(exitingMessage) + 1);
	}
	if (addColon)
	{
		write(1, colon, strlen(colon) + 1);
	}
	currentPID = 0;
}

/* Function to handle SIGINT signal */
void handleSIGINT()
{
	write(1, "\n\0", 2);
	if (currentPID != 0)
	{
		SIGINT_called = true;
	}
	else
	{
		char* colon = ": ";
		write(1, colon, strlen(colon) + 1);
	}
}

/* Function to expand occurances of $$ in our user input */
void expand$$()
{
	// count the number of occurances of $$
	char* input = malloc(INPUT_MAX * sizeof(char) + 1);
	initializeString(input, INPUT_MAX * sizeof(char));
	char* inputPointer = input;
	strcpy(input, userInput);
	if (strstr(input, "$$") != NULL) 
	{
		int count = 0;
		int numPid = getpid();
		char* pid = malloc(7 * sizeof(char) + 1);
		sprintf(pid, "%d", getpid());
		// get count of instances
		while (strstr(input, "$$") != NULL)
		{
			input = strstr(input, "$$");
			input += 2;
			count++;
		}
		memset(inputPointer, 0, strlen(inputPointer));
		free(inputPointer);
		// allocate memory for new input and expanded input
		char expandedInput[INPUT_MAX + 1];
		initializeString(expandedInput, INPUT_MAX);
		char* input = malloc(INPUT_MAX * sizeof(char) + 1);
		initializeString(input, INPUT_MAX * sizeof(char));
		char* inputPointer = input;
		//expandedPointer = expandedInput;
		strcpy(input, userInput);
		// extract the front of the string
		int i = 0;
		while (input[i] != '\0')
		{
			// extract string up until first $$
			if (input[i] == '$' && input[i+1] == '$')
			{
				if (i != 0)
				{
					strncpy(expandedInput, input, i);
				}
				break;
			}
			i++;
		}
		// cat expansions to expanded string
		while (strstr(input, "$$") != NULL)
		{
			input = strstr(input, "$$");
			input += 2;
			strcat(expandedInput, pid);
			int i = 0;
			while (input[i] != '\0')
			{
				// extract string up until first $$
				if (input[i] == '$' && input[i + 1] == '$')
				{
					if (i != 0)
					{
						char* holder = malloc(strlen(input) * sizeof(char));
						stpncpy(holder, input, i);
						strcat(expandedInput, holder);
					}
					break;
				}
				i++;
			}
		}
		if (input != NULL)
		{
			strcat(expandedInput, input);
		}
		free(inputPointer);
		free(userInput);
		userInput = malloc(INPUT_MAX * sizeof(char) + 1);
		initializeString(userInput, INPUT_MAX * sizeof(char));
		strcpy(userInput, expandedInput);
	}
}

/* Function to strip out that ugly newline character */
void stripNewLineChar(char* input)
{
	if (input[strlen(input) - 1] == '\n')
	{
		input[strlen(input) - 1] = '\0';
	}
}

/* Function to delete completed PIDs from the list */
void cleanBackgroundIDList(int deleteCount)
{
	// we can use a quick sorting method to move the PIDs with the value 0 away from PIDs that have a value and are running
	for (int i = 0; i < backgroundCount; i++)
	{
		int j = i;
		while (j >= 0 && backgroundIDs[j] < backgroundIDs[j + 1])
		{
			int holder = backgroundIDs[j];
			backgroundIDs[j] = backgroundIDs[j + 1];
			backgroundIDs[j + 1] = holder;
			j--;
		}
	}
	backgroundCount -= deleteCount;
}

/* Function to check background PIDs for completion */
void checkBackgroundStatus()
{
	int childStatus;
	int deleteCount = 0;
	for (int i = 0; i < backgroundCount; i++) 
	{
		if (waitpid(backgroundIDs[i], &childStatus, WNOHANG) > 0)
		{
			if (WIFEXITED(childStatus))
			{
				printf("Background pid %d exited with status %d\n", backgroundIDs[i], WEXITSTATUS(childStatus));
			}
			if (WIFSIGNALED(childStatus))
			{
				printf("Background pid %d terminated with signal %d\n", backgroundIDs[i], WTERMSIG(childStatus));
			}
			// set value to 0 if complete for sorting method
			backgroundIDs[i] = 0;
			// we need to know how many were deleted to adjust our in process ID count
			deleteCount += 1;
		}
	}
	cleanBackgroundIDList(deleteCount);
}

/* Function to run our built in commands */
void runBuiltInCommand(char* command, char* args)
{
	// run the exit command
	if (strcmp(command, "exit") == 0)
	{
		isRunning = false;
	}
	// run the change directory 'cd' command
	else if (strcmp(command, "cd") == 0)
	{
		size_t sizebuf = 255;
		char* currentdir = malloc(sizeof(char) * sizebuf);
		char* homeDir = getenv("HOME");
		if (args == NULL)
		{
			chdir(getenv("HOME"));
			if (getcwd(currentdir, sizebuf) == NULL)
			{
				perror("Error: ");
			}
		}
		else
		{
			chdir(args);
			if (getcwd(currentdir, sizebuf) == NULL)
			{
				perror("Error: ");
			}
		}
	}
	// run our status command
	else if (strcmp(command, "status") == 0)
	{
		if (childExitNormally)
		{
			printf("Exit Status: %d\n", childExitStatus);
		}
		else
		{
			printf("terminated by signal: %d\n", childExitStatus);
		}
		fflush(stdout);
	}
	fflush(stdout);
}

/* Function to run non built in commands */
void runGeneralCommand()
{
	// allocate and sanitize memory
	char* commandWithArgs[513];
	memset(commandWithArgs, 0, 513);
	char* input = malloc(INPUT_MAX * sizeof(char));
	initializeString(input, INPUT_MAX * sizeof(char));
	bool hasInput = false;
	bool hasOutput = false;
	char* inputfile;
	char* outputfile;

	// initialize variables
	char* buffer;
	int i = 0;
	int childStatus;
	int inputFD = 0;
	int outputFD = 0;
	int dupInput = dup(0);
	int dupOutput = dup(1);
	bool isBackground = false;
	bool isEcho = false;
	strcpy(input, userInput);
	char* token = strtok_r(input, " ", &buffer);

	//check if echo command if so we don't want to process any & variables
	if (strcmp(token, "echo") == 0)
	{
		isEcho = true;
	}
	// claim args
	while (token != NULL)
	{
		if (strcmp(token, "&") == 0)
		{
			if (!foregroundOnly && !isEcho)
			{
				isBackground = true;
			}
		}

		// handle input declarations
		else if (strcmp(token, "<") == 0)
		{
			hasInput = true;
			token = strtok_r(NULL, " ", &buffer);
			inputfile = malloc(strlen(token) * sizeof(char) + 1);
			initializeString(inputfile, strlen(token) * sizeof(char));
			strcpy(inputfile, token);
			inputFD = open(token, O_RDONLY, 0770);
			dup2(inputFD, 0);
		}

		// handle output declarations
		else if (strcmp(token, ">") == 0)
		{
			hasOutput = true;
			token = strtok_r(NULL, " ", &buffer);
			outputfile = malloc(strlen(token) * sizeof(char) + 1);
			initializeString(outputfile, strlen(token) * sizeof(char));
			strcpy(outputfile, token);
			outputFD = open(token, O_WRONLY | O_CREAT | O_TRUNC, 0770);
			dup2(outputFD, 1);
		}

		else
		{
			if (!hasInput && !hasOutput)
			{
				commandWithArgs[i] = token;
				i++;
			}
		}
		token = strtok_r(NULL, " ", &buffer);
	}

	// run processes
	pid_t childPid = -5;
	childPid = fork();
	switch (childPid)
	{
	// print error if necessary
	case -1:
		printf("Error creating child process.\n");
		break;
	// run chile process
	case 0:
		signal(SIGTSTP, SIG_IGN);										// ignore all SIGTSTP
		if (isBackground)
		{
			signal(SIGINT, SIG_IGN);									// background only ignore SIGINT
		}
		if (hasInput && inputFD < 0)
		{
			printf("Error: cannot open file for input.\n");
			fflush(stdout);
			exit(1);
		}
		if (hasOutput && outputFD < 0)
		{
			printf("Error: cannot open file for output.\n");
			fflush(stdout);
			exit(1);
		}
		// print PID then set to /dev/null per instructions
		if (isBackground)
		{
			if (!hasOutput)
			{
				outputFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0770);
				dup2(outputFD, 1);
			}
			if (!hasInput)
			{
				inputFD = open("/dev/null", O_RDONLY, 0770);
				dup2(inputFD, 0);
			}
		}
		if (execvp(commandWithArgs[0], commandWithArgs) != 0)			// we use execvp since it utilized the PATH environment variable
		{
			printf("That command was invalid.\n");
			fflush(stdout);
			exit(EXIT_FAILURE);
		}
		break;
	// run parent process
	default:
		fflush(stdout);
		if (isBackground)
		{
			currentPID = 0;
			dup2(dupOutput, 1);
			printf("Background pid is: %d\n", childPid);
			dup2(outputFD, 1);
			backgroundIDs[backgroundCount] = childPid;
			childPid = waitpid(childPid, &childStatus, WNOHANG);		// don't wait for background process
			backgroundCount++;
			break;
		}
		else
		{
			currentPID = childPid;
			waitpid(childPid, &childStatus, 0);							// wait for foreground process
			if (WIFEXITED(childStatus))
			{
				childExitStatus = WEXITSTATUS(childStatus);
				childExitNormally = true;
				currentPID = 0;
			}
			if (WIFSIGNALED(childStatus))
			{
				childExitStatus = WTERMSIG(childStatus);
				childExitNormally = false;
				if (SIGINT_called)
				{
					SIGINT_called = false;
					printf("terminated by signal: %d\n", childExitStatus);
				}
				currentPID = 0;
			}
		}
		break;
	}
	// return control to the user and close files
	if (hasInput)
	{
		close(inputFD);
	}
	if (hasOutput)
	{
		close(outputFD);
	}
	dup2(dupInput, 0);
	dup2(dupOutput, 1);
	close(dupInput);
	close(dupOutput);
}

/* Function to analyze and utilize user input */
void analyzeInput()
{
	// first check if we need to ignore our input
	if (!ignoreInput())
	{
		// if not ignored, get rid of the new line character and expand out $$ variable
		stripNewLineChar(userInput);
		expand$$();
		char* input = malloc(INPUT_MAX * sizeof(char));
		initializeString(input, INPUT_MAX * sizeof(char));
		char* inputPointer = input;
		strcpy(input, userInput);
		char* buffer;
		char* token = strtok_r(input, " ", &buffer);
		// check if we need to run a built in command
			if (strcmp(token, "exit") == 0 || strcmp(token, "cd") == 0 || strcmp(token, "status") == 0)
			{
				char* command = malloc(strlen(token) * sizeof(char));
				initializeString(command, strlen(token) * sizeof(char));
				strcpy(command, token);
				token = strtok_r(NULL, " ", &buffer);
				// if the command contains args, we need to extract them in their original format.
				if (token != NULL)
				{
					char* args = malloc(strlen(input) * sizeof(char));
					initializeString(args, strlen(input) * sizeof(char));
					int i = 0;
					while (token != NULL)
					{
						// make sure we take in all args, this includes spaces in the args themselves.
						// some files can have spaces in the name. strtok removes the spaces, so we need to replace them.
						if (i == 0)
						{
							stpcpy(args, token);
							i++;
						}
						else
						{
							strcat(args, token);
						}
						token = strtok_r(NULL, " ", &buffer);
						// replace the arg space if there is another argument
						if (token != NULL)
						{
							strcat(args, " ");
						}
					}
					runBuiltInCommand(command, args);
					free(args);
				}
				// if there are no args, just run the command and pass NULL for args
				else
				{
					runBuiltInCommand(command, NULL);
				}
				free(inputPointer);
				free(command);
			}
			// if not built in, run via exec functions
			else
			{
				free(inputPointer);
				runGeneralCommand();
			}
	}
}

/* Function to get our input from the user */
void getInput() 
{
	userInput = malloc((sizeof(char) * INPUT_MAX) + 1);
	initializeString(userInput, (sizeof(char) * INPUT_MAX));
	size_t length = 0;
    printf(": ");
	getline(&userInput, &length, stdin);
	fflush(stdout);
}

/* Function main, used to run our program */
int main(int argc, char* argv[])
{

	// handle SIG overrides here
	SIGINT_action.sa_handler = handleSIGINT;
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = SA_RESTART;
	sigaction(SIGINT, &SIGINT_action, NULL);

	SIGTSTP_action.sa_handler = handleSIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	// run our main program loop here
	while (isRunning)
	{
		checkBackgroundStatus();
		getInput();
		analyzeInput();
		fflush(stdout);
	}
	return EXIT_SUCCESS;
}