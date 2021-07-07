#ifndef BACKGROUND_TASKS_H
#define BACKGROUND_TASKS_H

typedef void (*taskFunction)();

int taskIndex = 0;
int taskCap = 5;
taskFunction *tasks = NULL;
bool stopped = false;

void stopBackgroundTasks()
{
    stopped = true;
}
void resumeBackgroundTasks()
{
    stopped = false;
}

// a background task is just any book-keeping task that should be run
// while we are executing a large delay that could otherwise block something
// like an http request handler
void backgroundTasks()
{
    if (tasks == NULL || stopped)
    {
        return;
    }

    for (int i = 0; i < taskIndex; i++)
    {
        if (stopped)
        {
            break;
        }
        tasks[i]();
    }
}

// https://stackoverflow.com/a/31521586/626810
void registerBackgroundTask(taskFunction func)
{
    if (tasks == NULL)
    {
        tasks = new taskFunction[taskCap];
    }

    tasks[taskIndex] = func;
    taskIndex++;

    if (taskIndex == taskCap)
    {
        // double up
        int newCap = taskCap * 2;
        taskFunction *newTasks = new taskFunction[newCap];
        for (int i = 0; i < taskIndex; i++)
        {
            newTasks[i] = tasks[i];
        }

        tasks = newTasks;
        taskCap = newCap;
    }
}

// sleep small ms inc at a time, performing background tasks in between
// this is needed due to the single-threaded nature of arduino and the necessity
// of some foreground tasks to function as intended
// (like updating displays and waiting for N seconds in between)
const unsigned long delayBucketMs = 20;
void delayWithBackgroundTasks(unsigned long ms)
{
    unsigned long int totalWaitMs = 0;
    do
    {
        unsigned long taskStart = millis();
        backgroundTasks();
        totalWaitMs += millis() - taskStart;

        if (totalWaitMs < ms)
        {
            delay(delayBucketMs);
            totalWaitMs += delayBucketMs;
        }
    } while (totalWaitMs < ms);
}
#endif